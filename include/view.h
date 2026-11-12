#ifndef TILE_FLOW_VIEW_H
#define TILE_FLOW_VIEW_H

#include "game.h"
#include "screen_effects.h"
#include "bn_fixed_point.h"
#include "bn_optional.h"
#include "bn_regular_bg_ptr.h"
#include "bn_sprite_palette_ptr.h"
#include "bn_sprite_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_string.h"
#include "bn_vector.h"

namespace tf
{
enum class Screen { title, play, pause, clear, over };
enum class TitleRow { difficulty, wave, start };

constexpr int result_fade_in_frames(Screen screen)
{
    return screen == Screen::over ? 60 : 20;
}

struct Display
{
    Screen screen;
    TitleRow title_row;
    Difficulty difficulty;
    int choice;
    int best;
    int volume;
    bool muted;
    bool fast;
    bool result_locked;
    int result_exit_frames;
    int result_exit_duration;
};

class View
{
public:
    View();
    void start_wave(const Game& game);
    void event(const Game& game, const Event& event);
    void update(const Game& game, const Display& display);

private:
    static constexpr int intro_frames = 100;

    enum class Motion { none, land, catch_tile, toss };

    struct TileSprite
    {
        bn::optional<bn::sprite_ptr> sprite;
        const bn::sprite_item* graphics_item = nullptr;
        int graphics_index = -1;
        bn::fixed_point from;
        Motion motion = Motion::none;
        int remaining = 0;
        int duration = 0;
    };

    struct Spark
    {
        bn::optional<bn::sprite_ptr> sprite;
        bn::fixed_point origin;
        int x_speed = 0;
        int y_speed = 0;
        int age = 0;
    };

    struct Hud
    {
        Screen screen = Screen::title;
        TitleRow title_row = TitleRow::difficulty;
        Difficulty difficulty = Difficulty::normal;
        int choice = -1;
        int wave = 0;
        int score = 0;
        int best = 0;
        int progress = 0;
        int drops = 0;
        int volume = 0;
        int status = 0;
        bool muted = false;
        bool fast = false;
        bool result_locked = false;

        bool operator==(const Hud&) const = default;
    };

    bn::regular_bg_ptr _background;
    ScreenEffects _screen_effects;
    bn::camera_ptr _panel_camera;
    std::array<bn::fixed, bn::display::height()> _panel_scroll{};
    bn::optional<bn::regular_bg_position_hbe_ptr> _panel_raster;
    bn::optional<bn::regular_bg_ptr> _overlay;
    bn::sprite_ptr _paddle;
    bn::sprite_ptr _drops;
    bn::sprite_text_generator _font;
    bn::vector<bn::sprite_ptr, 40> _text;
    bn::vector<bn::sprite_ptr, 10> _intro_text;
    bn::vector<bn::sprite_palette_ptr, colors + 1> _palettes;
    std::array<TileSprite, max_tiles> _tiles;
    std::array<Spark, 18> _sparks;
    bn::vector<bn::sprite_ptr, lanes * rows> _match_ripples;
    std::array<bn::optional<bn::sprite_ptr>, 6> _meter;
    bn::fixed _paddle_x = 64;
    Screen _screen = Screen::title;
    Hud _hud;
    bn::string<32> _message;
    int _message_time = 0;
    int _status = 0;
    int _clock = 0;
    int _next_spark = 0;
    int _match_ripple_age = 0;
    int _intro_frame = intro_frames;
    int _intro_offset = 0;
    int _result_age = 0;

    void change_screen(Screen screen);
    void update_intro(const Game& game);
    void update_result(bn::fixed fade, bool closing);
    void update_board(const Game& game, bool tick, bool fast);
    void draw_tile(int id, bn::fixed_point point, const bn::sprite_item& item, int graphics,
                   int palette, int depth, bool tick);
    void animate_tile(int id, Motion motion, int duration, bn::fixed_point fallback);
    void spark(bn::fixed_point origin, int palette, int vx, int vy);
    void update_effects();
    void announce(bn::string_view message, int time);
    void text(int x, int y, bn::string_view message, int ink = 0);
    void center_text(int y, bn::string_view message, int ink = 0);
    void draw_hud(const Game& game, const Display& display);
    void draw_title(const Display& display);
    void draw_pause(const Game& game, const Display& display);
    void draw_result(const Game& game, const Display& display);
    void draw_sound(const Display& display, int x, int y);
};
}

#endif
