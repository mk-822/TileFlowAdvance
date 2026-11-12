#include "view.h"

#include <algorithm>
#include "bn_blending.h"
#include "bn_color.h"
#include "bn_format.h"
#include "bn_regular_bg_items_clear_popup.h"
#include "bn_regular_bg_items_over_popup.h"
#include "bn_regular_bg_items_play_board.h"
#include "bn_regular_bg_items_result_board.h"
#include "bn_regular_bg_items_title_screen.h"
#include "bn_regular_bg_items_wave_banner.h"
#include "bn_sprite_items_belt_roll.h"
#include "bn_sprite_items_drop_meter.h"
#include "bn_sprite_items_glyphs.h"
#include "bn_sprite_items_goal_fill.h"
#include "bn_sprite_items_paddle.h"
#include "bn_sprite_items_ripple.h"
#include "bn_sprite_items_spark.h"
#include "bn_sprite_items_tiles.h"
#include "bn_sprite_tiles_ptr.h"

namespace tf
{
namespace
{
constexpr auto glyph_widths = []
{
    std::array<int8_t, 95> widths;
    widths.fill(6);
    widths[0] = 4;
    return widths;
}();
constexpr bn::sprite_font font(bn::sprite_items::glyphs, {}, bn::span(glyph_widths.data(), int(glyph_widths.size())));

constexpr bn::color rgb(int r, int g, int b)
{
    return bn::color(r, g, b);
}

// Color slots are shared between the belt tiles, pool tiles and clear effects.
constexpr bn::color tile_colors[colors + 1][16] = {
    {{}, rgb(23,31,31), rgb(3,27,29), rgb(2,16,22), rgb(1,7,12), rgb(12,25,29)},
    {{}, rgb(31,26,31), rgb(31,11,24), rgb(21,5,16), rgb(12,2,10), rgb(29,16,25)},
    {{}, rgb(31,29,20), rgb(31,22,8), rgb(24,12,3), rgb(13,5,1), rgb(31,23,13)},
    {{}, rgb(27,25,31), rgb(19,15,31), rgb(11,8,24), rgb(6,3,14), rgb(22,18,31)},
    {{}, rgb(26,31,22), rgb(16,29,12), rgb(8,20,6), rgb(4,10,3), rgb(19,29,14)},
    {{}, rgb(31,25,23), rgb(31,13,10), rgb(24,7,6), rgb(12,2,3), rgb(31,19,15)},
    {{}, rgb(31,31,31), rgb(31,28,22), rgb(22,26,31), rgb(9,17,23), rgb(10,16,24)}
};

constexpr bn::color ink[4][16] = {
    {{}, rgb(29,31,31)},
    {{}, rgb(10,29,31)},
    {{}, rgb(16,21,26)},
    {{}, rgb(31,23,12)}
};

constexpr const char* difficulty_labels[] = {"EASY", "NORMAL", "HARD"};
constexpr const char* difficulty_underlines[] = {"----", "------", "----"};
constexpr const char* starting_waves[] = {"01", "06", "11"};
constexpr int screen_x = 120;
constexpr int screen_y = 80;
constexpr int board_priority = 3;
constexpr int board_sprite_priority = 2;
constexpr int overlay_priority = 0;
constexpr int banner_travel = 204;
constexpr int banner_slide_frames = 18;
constexpr int belt_poses = 40;
constexpr int belt_graphics = bn::sprite_items::belt_roll.tiles_item().graphics_count();
constexpr int belt_sizes = belt_graphics / belt_poses;
constexpr int belt_depth_layers = 4;
constexpr int match_ripple_frame_ticks = 4;
static_assert(belt_graphics % belt_poses == 0 && belt_cells % belt_sizes == 0);
static_assert(bn::sprite_items::tiles.tiles_item().graphics_count() == colors + 1);

int column_x(int lane)
{
    return 20 + lane * 22;
}

bn::fixed_point pool_point(int lane, int row)
{
    return {column_x(lane), 140 - row * 9};
}

bn::fixed_point belt_point(const Tile& tile)
{
    const int depth = (belt_length - tile.distance) * 1024 / belt_length;
    const bn::fixed t = bn::fixed::from_data(depth * 4);
    return {64 + (bn::fixed(13.6) + t * bn::fixed(8.8)) * (tile.lane - 2),
            10 + t * (43 + t * 28)};
}

void position(bn::sprite_ptr& sprite, bn::fixed_point point)
{
    sprite.set_position(point.x() - screen_x, point.y() - screen_y);
}

bool is_result(Screen screen)
{
    return screen == Screen::clear || screen == Screen::over;
}

bn::string<12> wave_text(int wave)
{
    return bn::format<12>("{}{}", wave < 10 ? "0" : "", wave);
}

bn::string<32> intro_goal(const Wave& wave)
{
    switch(wave.goal)
    {
    case Goal::flow: return bn::format<32>("MAKE {} FLOWS", wave.target);
    case Goal::diagonal: return bn::format<32>("{} DIAGONAL FLOWS", wave.target);
    case Goal::tiles: return bn::format<32>("PROCESS {} TILES", wave.target);
    case Goal::horizontal: return bn::format<32>("{} HORIZONTAL FLOWS", wave.target);
    case Goal::points: return bn::format<32>("REACH {} POINTS", wave.target);
    default: return {};
    }
}

bn::string<16> score_text(int score)
{
    bn::string<16> result;
    for(int place = 100000; score < place && place > 1; place /= 10)
    {
        result += '0';
    }
    result += bn::to_string<12>(score);
    return result;
}
}

View::View() :
    _background(bn::regular_bg_items::title_screen.create_bg(8, 48)),
    _screen_effects(_background),
    _panel_camera(bn::camera_ptr::create(0, 0)),
    _paddle(bn::sprite_items::paddle.create_sprite()),
    _drops(bn::sprite_items::drop_meter.create_sprite()),
    _font(font)
{
    _background.set_priority(board_priority);
    _background.set_blending_bottom_enabled(true);
    _paddle.set_visible(false);
    _paddle.set_bg_priority(board_sprite_priority);
    _paddle.set_camera(_screen_effects.camera());
    _drops.set_visible(false);
    _drops.set_bg_priority(board_sprite_priority);
    _drops.set_camera(_screen_effects.camera());
    position(_drops, {215, 117});
    _font.set_bg_priority(board_sprite_priority);
    _font.set_z_order(-10);
    for(const auto& palette : tile_colors)
    {
        _palettes.push_back(bn::sprite_palette_item(palette, bn::bpp_mode::BPP_4).create_palette());
    }
}

void View::start_wave(const Game& game)
{
    _screen_effects.reset();
    for(TileSprite& tile : _tiles)
    {
        tile = {};
    }
    for(Spark& item : _sparks)
    {
        item.sprite.reset();
    }
    _match_ripples.clear();
    _paddle_x = column_x(game.lane());
    _clock = 0;
    _next_spark = 0;
    _message_time = 0;
    _intro_frame = 0;
}

void View::animate_tile(int id, Motion motion, int duration, bn::fixed_point fallback)
{
    TileSprite& tile = _tiles[id];
    tile.from = tile.sprite ? tile.sprite->position() + bn::fixed_point(screen_x, screen_y) : fallback;
    tile.motion = motion;
    tile.remaining = duration;
    tile.duration = duration;
}

void View::announce(bn::string_view message, int time)
{
    _message = message;
    _message_time = time;
    ++_status;
}

void View::event(const Game& game, const Event& event)
{
    const int x = column_x(event.lane);
    switch(event.type)
    {
    case EventType::place:
        animate_tile(event.tile, Motion::land, place_frames, {_paddle_x, 85});
        break;
    case EventType::toss:
        animate_tile(event.tile, Motion::toss, throw_frames, {_paddle_x, 85});
        break;
    case EventType::catch_tile:
        animate_tile(event.tile, Motion::catch_tile, 6, {x, 83});
        spark({x, 86}, game.tiles()[event.tile].color, 1, -5);
        break;
    case EventType::miss:
    {
        _screen_effects.miss();
        announce("TILE MISSED", 74);
        const int color = game.tiles()[event.tile].color;
        spark({x, 86}, color, -3, -3);
        spark({x, 86}, color, 3, -2);
        spark({x, 86}, color, 1, 0);
        break;
    }
    case EventType::blocked:
        announce("NO SPACE", 50);
        break;
    case EventType::match:
    {
        if(event.chain > 1)
        {
            announce(bn::format<32>("{}X CHAIN +{}", event.chain, event.points), 86);
        }
        else if(event.lines > 1)
        {
            announce(bn::format<32>("{}X FLOW +{}", event.lines, event.points), 86);
        }
        else if(event.longest > 3)
        {
            announce(bn::format<32>("{} TILE +{}", event.longest, event.points), 86);
        }
        else
        {
            announce(bn::format<32>("FLOW +{}", event.points), 86);
        }
        _match_ripples.clear();
        _match_ripple_age = 0;
        const auto& ripple_item = bn::sprite_items::ripple;
        const auto ripple_tiles = ripple_item.tiles_item().create_tiles(0);
        int matched = 0;
        int center_x = 0;
        int center_y = 0;
        for(int lane = 0; lane < lanes; ++lane)
        {
            const Column& column = game.pool()[lane];
            bool matched_below = false;
            for(int row = 0; row < column.size; ++row)
            {
                const int id = column.ids[row];
                const auto point = pool_point(lane, row);
                if(event.cells & cell_bit(lane, row))
                {
                    center_x += point.x().integer();
                    center_y += point.y().integer();
                    ++matched;
                    const int color = game.tiles()[id].color;
                    // Free the disappearing tile's sprite slot for its ripple.
                    _tiles[id] = {};
                    auto& ripple = _match_ripples.emplace_back(
                            bn::sprite_ptr::create(ripple_item.shape_size(), ripple_tiles, _palettes[color]));
                    ripple.set_bg_priority(board_sprite_priority);
                    ripple.set_camera(_screen_effects.camera());
                    ripple.set_z_order(-8);
                    position(ripple, point);
                    spark(point, color, (lane + row) % 2 ? 4 : -4, -5 - row);
                    matched_below = true;
                }
                else if(matched_below)
                {
                    animate_tile(id, Motion::land, 17, point);
                }
            }
        }
        _screen_effects.ripple(center_x / matched, center_y / matched, false);
        break;
    }
    case EventType::clear:
        _screen_effects.ripple(screen_x, screen_y + 12, true);
        break;
    default:
        break;
    }
}

void View::spark(bn::fixed_point origin, int palette, int vx, int vy)
{
    Spark& item = _sparks[_next_spark];
    _next_spark = (_next_spark + 1) % int(_sparks.size());
    item.sprite.reset();
    item.sprite = bn::sprite_items::spark.create_sprite();
    item.sprite->set_bg_priority(board_sprite_priority);
    item.sprite->set_camera(_screen_effects.camera());
    item.sprite->set_z_order(-8);
    item.sprite->set_palette(_palettes[palette]);
    position(*item.sprite, origin);
    item.origin = origin;
    item.x_speed = vx;
    item.y_speed = vy;
    item.age = 0;
}

void View::update_effects()
{
    if(!_match_ripples.empty())
    {
        const int age = _match_ripple_age++;
        const auto& tiles_item = bn::sprite_items::ripple.tiles_item();
        const int frame = age / match_ripple_frame_ticks;
        if(frame == tiles_item.graphics_count())
        {
            _match_ripples.clear();
        }
        else if(age && age % match_ripple_frame_ticks == 0)
        {
            const auto tiles = tiles_item.create_tiles(frame);
            for(auto& ripple : _match_ripples)
            {
                ripple.set_tiles(tiles);
            }
        }
    }
    for(Spark& item : _sparks)
    {
        if(!item.sprite)
        {
            continue;
        }
        const int age = item.age++;
        if(age == 20)
        {
            item.sprite.reset();
        }
        else
        {
            item.sprite->set_tiles(bn::sprite_items::spark.tiles_item(), age / 5);
            position(*item.sprite, item.origin + bn::fixed_point(
                    item.x_speed * age / 6, item.y_speed * age / 7 + age * age / 39));
        }
    }
}

void View::change_screen(Screen screen)
{
    if(screen == _screen)
    {
        return;
    }
    _panel_raster.reset();
    _overlay.reset();
    _intro_text.clear();
    if(is_result(_screen))
    {
        bn::blending::restore();
    }
    if(screen == Screen::title || screen == Screen::pause)
    {
        _screen_effects.reset();
    }
    if(screen != Screen::play)
    {
        _intro_frame = intro_frames;
    }

    const auto& bg = screen == Screen::title ? bn::regular_bg_items::title_screen :
                     screen == Screen::play ? bn::regular_bg_items::play_board : bn::regular_bg_items::result_board;
    _background.set_item(bg);
    _screen = screen;
    _paddle.set_visible(screen != Screen::title);
    if(is_result(screen))
    {
        _result_age = 0;
        const auto& panel = screen == Screen::clear ? bn::regular_bg_items::clear_popup : bn::regular_bg_items::over_popup;
        _overlay = panel.create_bg(8, 48);
        _overlay->set_priority(overlay_priority);
        _overlay->set_blending_top_enabled(true);
    }
    if(screen == Screen::title)
    {
        for(TileSprite& tile : _tiles)
        {
            tile = {};
        }
        for(Spark& item : _sparks)
        {
            item.sprite.reset();
        }
        _match_ripples.clear();
    }
}

void View::update_intro(const Game& game)
{
    if(_intro_frame == intro_frames)
    {
        if(!_intro_text.empty())
        {
            _overlay.reset();
            _intro_text.clear();
        }
        return;
    }
    if(_intro_frame == 0)
    {
        // The banner map is 512 pixels wide so the moving panel never wraps into view.
        _overlay = bn::regular_bg_items::wave_banner.create_bg(136, 48);
        _overlay->set_priority(overlay_priority);
        _font.set_bg_priority(overlay_priority);
        _font.remove_camera();
        _font.set_center_alignment();
        _font.set_palette_item(bn::sprite_palette_item(ink[1], bn::bpp_mode::BPP_4));
        _font.generate(0, 28 + 4 - screen_y, bn::format<20>("WAVE {}", wave_text(game.wave_number())), _intro_text);
        _font.set_palette_item(bn::sprite_palette_item(ink[0], bn::bpp_mode::BPP_4));
        _font.generate(0, 43 + 4 - screen_y, intro_goal(game.wave()), _intro_text);
        _font.set_left_alignment();
        _font.set_bg_priority(board_sprite_priority);
        _intro_offset = 0;
    }

    int offset = 0;
    if(_intro_frame < banner_slide_frames)
    {
        const int left = banner_slide_frames - _intro_frame;
        offset = banner_travel * left * left / (banner_slide_frames * banner_slide_frames);
    }
    else if(_intro_frame >= intro_frames - banner_slide_frames)
    {
        const int elapsed = _intro_frame - (intro_frames - banner_slide_frames) + 1;
        offset = -banner_travel * elapsed * elapsed / (banner_slide_frames * banner_slide_frames);
    }
    _overlay->set_x(136 + offset);
    for(bn::sprite_ptr& sprite : _intro_text)
    {
        sprite.set_x(sprite.x() + offset - _intro_offset);
    }
    _intro_offset = offset;
    ++_intro_frame;
}

void View::update_result(bn::fixed fade, bool closing)
{
    constexpr int sine[] = {0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3};
    const int skew = ((1 - fade) * (closing ? 10 : 8)).integer();
    const int shift_y = ((1 - fade) * (closing ? -8 : 7)).integer();
    _overlay->set_y(48 + shift_y);
    _overlay->set_visible(fade > bn::fixed(0.06));
    bn::blending::set_transparency_alpha(fade * bn::fixed(0.8125));

    if(skew)
    {
        for(int row = 0; row < bn::display::height(); ++row)
        {
            const int phase = (row / 3 + _result_age) % 16;
            const int band = (row / 7 + _result_age / 2) % 3 - 1;
            _panel_scroll[row] = sine[phase] * skew / 8 + band * skew / 3;
        }
        if(_panel_raster)
        {
            _panel_raster->reload_deltas_ref();
        }
        else
        {
            _panel_raster = bn::regular_bg_position_hbe_ptr::create_horizontal(
                    *_overlay, bn::span(_panel_scroll.data(), int(_panel_scroll.size())));
        }
    }
    else
    {
        _panel_raster.reset();
    }

    _panel_camera.set_position(0, -shift_y);
    const int reveal_to = ((fade - bn::fixed(0.36)) * 350).integer();
    for(auto& sprite : _text)
    {
        sprite.set_visible(sprite.y().integer() + screen_y <= reveal_to);
    }
    ++_result_age;
}

void View::draw_tile(int id, bn::fixed_point point, const bn::sprite_item& item, int graphics,
                     int palette, int depth, bool tick)
{
    TileSprite& tile = _tiles[id];
    if(tile.remaining)
    {
        const bn::fixed t = 1 - bn::fixed(tile.remaining) / tile.duration;
        const bn::fixed blend = tile.motion == Motion::land ? t * t : t;
        point = tile.from + (point - tile.from) * blend;
        if(tile.motion == Motion::toss)
        {
            point.set_y(point.y() - 92 * t * (1 - t));
        }
        if(tick)
        {
            --tile.remaining;
        }
    }
    if(!tile.sprite)
    {
        tile.sprite = bn::sprite_ptr::create(item.shape_size(), item.tiles_item().create_tiles(graphics),
                                             _palettes[palette]);
        tile.sprite->set_bg_priority(board_sprite_priority);
        tile.sprite->set_camera(_screen_effects.camera());
    }
    else
    {
        if(tile.graphics_item != &item || tile.graphics_index != graphics)
        {
            tile.sprite->set_tiles(item.tiles_item(), graphics);
        }
        tile.sprite->set_palette(_palettes[palette]);
    }
    tile.graphics_item = &item;
    tile.graphics_index = graphics;
    tile.sprite->set_z_order(depth);
    position(*tile.sprite, point);
}

void View::update_board(const Game& game, bool tick, bool fast)
{
    const bn::fixed target = column_x(game.lane());
    if(tick)
    {
        _paddle_x += std::clamp(target - _paddle_x, bn::fixed(-5), bn::fixed(5));
        update_effects();
        ++_clock;
    }
    position(_paddle, {_paddle_x, 90});
    const int paddle_graphics = game.belt_stopped() ? 2 : fast ? 1 : 0;
    _paddle.set_tiles(bn::sprite_items::paddle.tiles_item(), paddle_graphics);

    for(int id = 0; id < max_tiles; ++id)
    {
        if(!game.tiles()[id].active && _tiles[id].sprite)
        {
            _tiles[id] = {};
        }
    }
    for(int index = 0; index < game.belt_size(); ++index)
    {
        const int id = game.belt_id(index);
        const Tile& tile = game.tiles()[id];
        const int distance = belt_length - tile.distance;
        const int size = std::min(belt_sizes - 1, distance / (cell_length * belt_cells / belt_sizes));
        const int pose = distance % cell_length * belt_poses / cell_length;
        const int depth = belt_depth_layers - distance * belt_depth_layers / belt_length;
        draw_tile(id, belt_point(tile), bn::sprite_items::belt_roll, size * belt_poses + pose,
                  tile.color, tile.flight ? -6 : depth, tick);
    }
    const Column& stack = game.stack();
    for(int index = 0; index < stack.size; ++index)
    {
        const int id = stack.ids[index];
        draw_tile(id, {_paddle_x, 86 - index * 4}, bn::sprite_items::tiles, colors,
                  game.tiles()[id].color, -1 - index, tick);
    }
    for(int lane = 0; lane < lanes; ++lane)
    {
        const Column& column = game.pool()[lane];
        for(int row = 0; row < column.size; ++row)
        {
            const int id = column.ids[row];
            const int color = game.tiles()[id].color;
            const bool flash = (game.highlighted() & cell_bit(lane, row)) && (_clock / 4) % 2 == 0;
            draw_tile(id, pool_point(lane, row), bn::sprite_items::tiles, color, flash ? colors : color, 0, tick);
        }
    }
}

void View::text(int x, int y, bn::string_view message, int color)
{
    _font.set_palette_item(bn::sprite_palette_item(ink[color], bn::bpp_mode::BPP_4));
    _font.generate(x - screen_x, y + 4 - screen_y, message, _text);
}

void View::center_text(int y, bn::string_view message, int color)
{
    _font.set_center_alignment();
    text(screen_x, y, message, color);
    _font.set_left_alignment();
}

void View::draw_sound(const Display& display, int x, int y)
{
    if(display.muted)
    {
        text(x, y, "L/R VOL  MUTE", 2);
    }
    else
    {
        text(x, y, bn::format<20>("L/R VOL  {}%", display.volume * 20), 2);
    }
}

void View::draw_title(const Display& display)
{
    constexpr int difficulty_x[] = {133, 161, 201};
    constexpr int wave_x[] = {138, 172, 206};
    const bool on_difficulty = display.title_row == TitleRow::difficulty;
    const bool on_wave = display.title_row == TitleRow::wave;
    const bool on_start = display.title_row == TitleRow::start;

    text(141, 15, "DIFFICULTY", on_difficulty ? 1 : 2);
    text(141, 48, "START WAVE", on_wave ? 1 : 2);
    text(155, 127, "A  START", on_start ? 1 : 0);
    const int cursor_y[] = {15, 48, 127};
    text(133 + (on_start ? 5 : 0), cursor_y[int(display.title_row)], ">", 1);

    for(int index = 0; index < 3; ++index)
    {
        const bool selected_difficulty = int(display.difficulty) == index;
        const bool selected_wave = display.choice == index;
        text(difficulty_x[index], 27, difficulty_labels[index], selected_difficulty ? 0 : 2);
        text(wave_x[index], 60, starting_waves[index], selected_wave ? 0 : 2);
        if(selected_difficulty)
        {
            text(difficulty_x[index], 34, difficulty_underlines[index], on_difficulty ? 1 : 2);
        }
        if(selected_wave)
        {
            text(wave_x[index], 67, "--", on_wave ? 1 : 2);
        }
    }
    const Choice choice = choices[display.choice];
    if(choice.bonus)
    {
        text(133, 75, bn::format<24>("{} DROPS  +{}", choice.drops, choice.bonus), 3);
    }
    else
    {
        text(133, 75, "3 DROPS", 2);
    }
    text(133, 108, score_text(display.best));
    draw_sound(display, 133, 143);
}

void View::draw_pause(const Game& game, const Display& display)
{
    text(135, 19, "PAUSED", 1);
    text(135, 40, bn::format<24>("WAVE {}  {}", wave_text(game.wave_number()), difficulty_labels[int(game.difficulty())]), 2);
    text(135, 59, "A       DROP");
    text(135, 71, "B / UP  RETURN");
    text(135, 83, "R/DOWN  FAST");
    text(135, 96, "SELECT  MUTE", 2);
    draw_sound(display, 135, 110);
    text(135, 137, "A/START RESUME", 1);
}

void View::draw_result(const Game& game, const Display& display)
{
    const auto wave = wave_text(game.wave_number());
    const auto score = score_text(game.score());
    const int action_ink = display.result_locked ? 2 : 1;
    if(display.screen == Screen::over)
    {
        const char* reason = game.end_reason() == EndReason::drops ? "OUT OF DROPS" : "POOL IS FULL";
        center_text(59, bn::format<32>("WAVE {}  /  {}", wave, reason), 3);
        center_text(78, bn::format<24>("SCORE  {}", score));
        center_text(94, bn::format<24>("BEST   {}", score_text(display.best)), 2);
        center_text(124, "A RETRY      B MENU", action_ink);
        return;
    }

    center_text(57, bn::format<32>("WAVE {}  /  SCORE {}", wave, score));
    if(game.wave_bonus())
    {
        center_text(69, bn::format<24>("BONUS +{}", game.wave_bonus()), 3);
    }
    const int next_wave = game.wave_number() + 1;
    if(game.can_skip())
    {
        center_text(82, "NEXT WAVE  < >", 2);
        for(int index = 0; index < 3; ++index)
        {
            const bool selected = display.choice == index;
            const int x = 54 + index * 50;
            text(x, 94, bn::format<20>("{} {}", selected ? ">" : " ", wave_text(next_wave + choices[index].skip)),
                 selected ? 1 : 2);
        }
        const Choice choice = choices[display.choice];
        if(choice.bonus)
        {
            center_text(107, bn::format<24>("{} DROPS  +{}", choice.drops, choice.bonus), 3);
        }
        else
        {
            center_text(107, "3 DROPS", 2);
        }
    }
    else
    {
        center_text(84, bn::format<24>("NEXT WAVE {}", wave_text(next_wave)), 1);
        center_text(103, bn::format<16>("DROPS {}/{}", game.drops_remaining(), game.drop_limit()), 2);
    }
    center_text(124, "A NEXT WAVE", action_ink);
}

void View::draw_hud(const Game& game, const Display& display)
{
    text(170, 16, wave_text(game.wave_number()), 1);
    text(135, 30, game.wave().title);
    text(135, 44, goal_label(game.wave().goal), 2);
    const int progress = std::min(game.progress(), game.wave().target);
    const auto count = bn::format<24>("{}/{}", progress, game.wave().target);
    text(228 - count.size() * 6, 44, count, 1);
    const auto score = score_text(game.score());
    const auto best = score_text(display.best);
    text(228 - score.size() * 6, 73, score);
    text(228 - best.size() * 6, 92, best, 2);
    if(display.screen == Screen::play)
    {
        if(_message_time)
        {
            text(135, 140, _message, 1);
        }
        else if(display.fast)
        {
            text(135, 140, "FAST FORWARD", 3);
        }
        else
        {
            text(135, 140, "A DROP  B RETURN", 2);
        }
    }

    _drops.set_visible(true);
    _drops.set_tiles(bn::sprite_items::drop_meter.tiles_item(), (game.drop_limit() - 3) * 6 + game.drops_remaining());
    const int filled = int(int64_t(progress) * 91 / game.wave().target);
    for(int index = 0; index < int(_meter.size()); ++index)
    {
        const int width = std::clamp(filled - index * 16, 0, 16);
        if(width)
        {
            _meter[index] = bn::sprite_items::goal_fill.create_sprite(144 + index * 16 - screen_x, 56 - screen_y, width - 1);
            _meter[index]->set_bg_priority(board_sprite_priority);
            _meter[index]->set_camera(_screen_effects.camera());
        }
    }
}

void View::update(const Game& game, const Display& display)
{
    change_screen(display.screen);
    const bool result = is_result(display.screen);
    const bool closing = display.result_exit_frames > 0;
    bn::fixed fade = 0;
    if(result)
    {
        if(closing)
        {
            const bn::fixed t = bn::fixed(display.result_exit_frames) / display.result_exit_duration;
            fade = t * t;
        }
        else
        {
            const int duration = result_fade_in_frames(display.screen);
            const bn::fixed t = bn::fixed(std::min(_result_age, duration)) / duration;
            fade = t * (2 - t);
        }
    }
    _screen_effects.update(fade);
    if(display.screen != Screen::title)
    {
        const bool tick = display.screen != Screen::pause;
        update_board(game, tick, display.fast);
        if(tick && _message_time && --_message_time == 0)
        {
            ++_status;
        }
    }
    if(display.screen == Screen::play)
    {
        update_intro(game);
    }
    const Hud next = {display.screen, display.title_row, display.difficulty, display.choice, game.wave_number(), game.score(),
                      display.best, game.progress(), game.drops_remaining(), display.volume, _status,
                      display.muted, display.fast, display.result_locked};
    if(next != _hud)
    {
        _hud = next;
        _text.clear();
        _font.set_bg_priority(board_sprite_priority);
        _font.set_camera(_screen_effects.camera());
        _drops.set_visible(false);
        for(auto& segment : _meter)
        {
            segment.reset();
        }
        switch(display.screen)
        {
        case Screen::title: draw_title(display); break;
        case Screen::play: draw_hud(game, display); break;
        case Screen::pause: draw_pause(game, display); break;
        default:
            _font.set_bg_priority(overlay_priority);
            _font.set_camera(_panel_camera);
            draw_result(game, display);
            break;
        }
    }
    if(result)
    {
        update_result(fade, closing);
    }
}
}
