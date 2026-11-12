#include <algorithm>
#include <array>
#include <cstdint>
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_music.h"
#include "bn_music_items.h"
#include "bn_optional.h"
#include "bn_random.h"
#include "bn_sound.h"
#include "bn_sram.h"
#include "game.h"
#include "tile_flow_sound_items.h"
#include "view.h"

namespace
{
using namespace tf;

constexpr uint32_t save_magic = 0x31414654; // TFA1
constexpr int volume_steps = 5;

struct Save
{
    uint32_t magic = save_magic;
    std::array<uint32_t, 3> best{};
    uint8_t difficulty = uint8_t(Difficulty::normal);
    uint8_t volume = volume_steps;
    uint8_t muted = 0;
    uint8_t reserved = 0;
    uint32_t checksum = 0;

    [[nodiscard]] uint32_t calculate_checksum() const
    {
        uint32_t value = magic;
        for(uint32_t score : best)
        {
            value = (value ^ score) * 16777619u;
        }
        return (value ^ difficulty ^ (uint32_t(volume) << 8) ^ (uint32_t(muted) << 16)) * 16777619u;
    }

    void load()
    {
        bn::sram::read(*this);
        if(magic != save_magic || checksum != calculate_checksum() || difficulty > 2 || volume > volume_steps ||
           muted > 1 || std::any_of(best.begin(), best.end(), [](uint32_t score) { return score > 0x7fffffffu; }))
        {
            *this = Save();
        }
    }

    void write()
    {
        checksum = calculate_checksum();
        bn::sram::write(*this);
    }
};

constexpr bn::music_item tracks[] = {
    bn::music_items::a_winter_kiss,
    bn::music_items::amanita_dream,
    bn::music_items::ambient_starfield,
    bn::music_items::bionic_girl,
    bn::music_items::cabin_fever,
    bn::music_items::drozerix_alone,
    bn::music_items::drozerix_bubble_machine,
    bn::music_items::drozerix_leisurely_voice,
    bn::music_items::drozerix_lost_without,
    bn::music_items::drozerix_master_select,
    bn::music_items::drozerix_my_dearest,
    bn::music_items::october_chip,
    bn::music_items::spc,
    bn::music_items::spectrum,
    bn::music_items::the_dim_dungeon
};
constexpr int track_count = int(std::size(tracks));

constexpr int tile_pitch[] = {0, 2, 3, 7, 9, 12};
constexpr int wave_keys = int(std::size(effects::start_wave));
static_assert(std::size(tile_pitch) == colors);
static_assert(std::size(effects::highlight) >= wave_keys * ((lanes * rows + rows) / 3));
static_assert(std::size(effects::match) == std::size(effects::highlight));
class Audio
{
public:
    void advance_rng()
    {
        _rng.update();
    }

    void set_volume(int volume, bool muted)
    {
        _volume = muted ? bn::fixed(0) : bn::fixed(volume) / volume_steps;
        if(_volume == 0)
        {
            bn::sound::stop_all();
        }
        bn::sound::set_master_volume(_volume);
        update_music_volume();
    }

    void next_track()
    {
        if(_position == track_count)
        {
            for(int index = 0; index < track_count; ++index)
            {
                _order[index] = index;
            }
            for(int index = track_count - 1; index > 0; --index)
            {
                std::swap(_order[index], _order[_rng.get_int(index + 1)]);
            }
            if(_order.front() == _last)
            {
                std::swap(_order.front(), _order[1]);
            }
            _position = 0;
        }
        _last = _order[_position++];
        tracks[_last].play(music_volume());
    }

    void menu_move() const
    {
        play(effects::move[0], -3);
    }

    void start_wave(int wave) const
    {
        play(effects::start_wave[(wave - 1) % wave_keys], 1);
    }

    void event(const Game& game, const Event& event)
    {
        const int key = (game.wave_number() - 1) % wave_keys;
        const auto tile_key = [&game, &event, key]
        {
            return key + tile_pitch[game.tiles()[event.tile].color];
        };
        const int chain_key = key + wave_keys * (event.chain - 1);
        switch(event.type)
        {
        case EventType::move: play(effects::move[key + tile_pitch[event.lane]], -3); break;
        case EventType::place: play(effects::place[tile_key()], 0); break;
        case EventType::toss: play(effects::throw_tile[key], 0); break;
        case EventType::blocked: play(bn::sound_items::blocked, -1); break;
        case EventType::catch_tile: play(effects::catch_tile[tile_key()], 0); break;
        case EventType::miss:
            // Several tiles can drop together. Replaying identical samples in phase clips the mix.
            if(!_miss_sound || !_miss_sound->active())
            {
                _miss_sound = play(bn::sound_items::miss, 1);
            }
            break;
        case EventType::tumble: play(effects::tumble[tile_key()], -2); break;
        case EventType::highlight: play(effects::highlight[chain_key], 1); break;
        case EventType::match: play(effects::match[chain_key], 2); break;
        case EventType::clear:
            _duck_frames = 96;
            update_music_volume();
            play(effects::wave_clear[key], 3);
            break;
        case EventType::over:
            if(bn::music::playing())
            {
                bn::music::stop();
            }
            _duck_frames = 0;
            play(effects::game_over[key], 3);
            break;
        default:
            break;
        }
    }

    void update()
    {
        if(_duck_frames && --_duck_frames == 0)
        {
            update_music_volume();
        }
    }

private:
    bn::random _rng;
    bn::fixed _volume = 1;
    std::array<int, track_count> _order{};
    int _position = track_count;
    int _last = -1;
    int _duck_frames = 0;
    bn::optional<bn::sound_handle> _miss_sound;

    bn::optional<bn::sound_handle> play(bn::sound_item sample, int priority) const
    {
        if(_volume > 0)
        {
            return sample.play_with_priority(priority);
        }
        return {};
    }

    [[nodiscard]] bn::fixed music_volume() const
    {
        return _volume * (_duck_frames ? bn::fixed(0.12) : bn::fixed(0.33));
    }

    void update_music_volume() const
    {
        if(bn::music::playing())
        {
            bn::music::set_volume(music_volume());
        }
    }
};

class App
{
public:
    App() : _game(handle_event, this)
    {
        _save.load();
        _difficulty = Difficulty(_save.difficulty);
        _audio.set_volume(_save.volume, _save.muted);
    }

    void frame()
    {
        ++_frame;
        _audio.advance_rng();
        if(_result_frames)
        {
            --_result_frames;
        }
        const Screen old_screen = screen();
        if(_result_exit_frames)
        {
            if(--_result_exit_frames == 0)
            {
                if(_return_to_title)
                {
                    _title = true;
                    _title_row = TitleRow::difficulty;
                }
                else
                {
                    start_wave();
                }
            }
        }
        else
        {
            input(old_screen);
        }
        const bool fast = screen() == Screen::play && (bn::keypad::down_held() || bn::keypad::r_held());
        if(old_screen == Screen::play && screen() == Screen::play)
        {
            _game.step(fast);
        }
        uint32_t& best = _save.best[int(_difficulty)];
        if(uint32_t(_game.score()) > best && !_title)
        {
            best = uint32_t(_game.score());
            _save.write();
        }
        const Screen current = screen();
        _view.update(_game, {current, _title_row, _difficulty, current == Screen::clear ? _skip_choice : _start_choice,
                            int(best), _save.volume, bool(_save.muted), fast,
                            _result_frames > 0 || _result_exit_frames > 0, _result_exit_frames});
        _audio.update();
    }

private:
    Game _game;
    View _view;
    Audio _audio;
    Save _save;
    uint32_t _frame = 0;
    Difficulty _difficulty = Difficulty::normal;
    bool _title = true;
    TitleRow _title_row = TitleRow::difficulty;
    int _start_choice = 0;
    int _skip_choice = 0;
    int _result_frames = 0;
    int _result_exit_frames = 0;
    bool _return_to_title = false;
    int _held_direction = 0;
    int _move_cooldown = 0;
    int _repeat_delay = 0;
    std::array<int, lanes - 1> _queued_moves{};
    int _queued_count = 0;

    static void handle_event(void* context, const Event& event)
    {
        auto& app = *static_cast<App*>(context);
        app._view.event(app._game, event);
        app._audio.event(app._game, event);
        if(event.type == EventType::clear || event.type == EventType::over)
        {
            app._result_frames = event.type == EventType::clear ? ScreenEffects::clear_ripple_frames : 45;
            app._skip_choice = 0;
        }
    }

    [[nodiscard]] Screen screen() const
    {
        if(_title)
        {
            return Screen::title;
        }
        switch(_game.phase())
        {
        case Phase::playing: return Screen::play;
        case Phase::paused: return Screen::pause;
        case Phase::clear: return Screen::clear;
        case Phase::over: return Screen::over;
        default: return Screen::title;
        }
    }

    void start_wave()
    {
        const bool new_run = _game.phase() != Phase::clear;
        const bool new_track = new_run || _game.can_skip();
        if(new_run)
        {
            _game.seed(_frame ^ 0x6c61785fu);
        }
        _game.begin(_difficulty, new_run ? _start_choice : _skip_choice);
        if(new_track)
        {
            _audio.next_track();
        }
        _audio.start_wave(_game.wave_number());
        _title = false;
        _view.start_wave(_game);
        clear_movement();
    }

    void exit_result(bool return_to_title)
    {
        _return_to_title = return_to_title;
        _result_exit_frames = result_fade_out_frames;
    }

    void clear_movement()
    {
        _queued_count = 0;
        _held_direction = 0;
        _move_cooldown = 0;
        _repeat_delay = 0;
    }

    bool move(int direction)
    {
        const int previous = _game.lane();
        _game.move(direction);
        if(_game.lane() != previous)
        {
            _move_cooldown = 9;
            return true;
        }
        return false;
    }

    void movement()
    {
        using namespace bn::keypad;
        const int pressed = int(right_pressed()) - int(left_pressed());
        int held = _held_direction;
        if(pressed)
        {
            held = pressed;
            if(_queued_count < int(_queued_moves.size()))
            {
                _queued_moves[_queued_count++] = pressed;
            }
        }
        else if(!(left_held() && right_held()))
        {
            held = int(right_held()) - int(left_held());
        }
        if(_held_direction != held)
        {
            _held_direction = held;
            _repeat_delay = 17;
        }
        else if(_repeat_delay)
        {
            --_repeat_delay;
        }
        if(_move_cooldown && --_move_cooldown)
        {
            return;
        }
        while(_queued_count)
        {
            const int direction = _queued_moves.front();
            --_queued_count;
            for(int index = 0; index < _queued_count; ++index)
            {
                _queued_moves[index] = _queued_moves[index + 1];
            }
            if(move(direction))
            {
                return;
            }
        }
        if(_held_direction && !_repeat_delay)
        {
            move(_held_direction);
        }
    }

    void input(Screen current)
    {
        using namespace bn::keypad;
        const bool confirm = a_pressed() || start_pressed();
        const int dx = int(right_pressed()) - int(left_pressed());
        const int dy = int(down_pressed()) - int(up_pressed());
        bool settings_changed = false;
        if(select_pressed())
        {
            _save.muted ^= 1;
            settings_changed = true;
        }
        if(current == Screen::title || current == Screen::pause)
        {
            const int volume = std::clamp(int(_save.volume) + int(r_pressed()) - int(l_pressed()), 0, volume_steps);
            if(volume != _save.volume)
            {
                _save.volume = uint8_t(volume);
                settings_changed = true;
            }
        }
        if(settings_changed)
        {
            _audio.set_volume(_save.volume, _save.muted);
            _save.write();
        }
        switch(current)
        {
        case Screen::title:
        {
            const auto previous_row = _title_row;
            _title_row = TitleRow(std::clamp(int(_title_row) + dy, 0, int(TitleRow::start)));
            const auto difficulty = _title_row == TitleRow::difficulty ?
                    Difficulty(std::clamp(int(_difficulty) + dx, 0, 2)) : _difficulty;
            const int choice = _title_row == TitleRow::wave ? std::clamp(_start_choice + dx, 0, 2) : _start_choice;
            const bool launch = start_pressed() || (a_pressed() && _title_row == TitleRow::start);
            if(a_pressed() && !launch)
            {
                _title_row = TitleRow(int(_title_row) + 1);
            }
            if(difficulty != _difficulty || choice != _start_choice || _title_row != previous_row)
            {
                _audio.menu_move();
            }
            if(difficulty != _difficulty)
            {
                _difficulty = difficulty;
                _save.difficulty = uint8_t(difficulty);
                _save.write();
            }
            _start_choice = choice;
            if(launch)
            {
                start_wave();
            }
            break;
        }
        case Screen::play:
            if(start_pressed())
            {
                _game.pause();
                if(bn::music::playing())
                {
                    bn::music::pause();
                }
                clear_movement();
            }
            else
            {
                movement();
                if(a_pressed())
                {
                    _game.place();
                }
                if(b_pressed() || up_pressed())
                {
                    _game.toss();
                }
            }
            break;
        case Screen::pause:
            if(confirm)
            {
                _game.pause();
                if(bn::music::paused())
                {
                    bn::music::resume();
                }
            }
            break;
        case Screen::clear:
            if(!_result_frames)
            {
                if(_game.can_skip() && dx)
                {
                    const int choice = std::clamp(_skip_choice + dx, 0, 2);
                    if(choice != _skip_choice)
                    {
                        _skip_choice = choice;
                        _audio.menu_move();
                    }
                }
                if(confirm)
                {
                    exit_result(false);
                }
            }
            break;
        case Screen::over:
            if(!_result_frames)
            {
                if(b_pressed())
                {
                    exit_result(true);
                    _audio.menu_move();
                }
                else if(confirm)
                {
                    exit_result(false);
                }
            }
            break;
        default:
            break;
        }
    }
};
}

int main()
{
    bn::core::init();
    static App app;
    while(true)
    {
        app.frame();
        bn::core::update();
    }
}
