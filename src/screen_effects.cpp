#include "screen_effects.h"

#include <algorithm>
#include <cstdlib>
#include "bn_bg_palette_ptr.h"
#include "bn_color.h"
#include "bn_math.h"
#include "bn_regular_bg_items_wave_ring.h"

namespace tf
{
namespace
{
constexpr int miss_frames = 12;
constexpr bn::color backdrop(1, 1, 3);
constexpr bn::color miss_flash(31, 21, 20);
}

ScreenEffects::ScreenEffects(const bn::regular_bg_ptr& board) :
    _board(board),
    _camera(bn::camera_ptr::create(0, 0))
{
    _board.set_camera(_camera);
}

void ScreenEffects::miss()
{
    _miss_frames = miss_frames;
}

void ScreenEffects::ripple(int x, int y, bool clear)
{
    _ripple_x = x;
    _ripple_y = y;
    _ripple_age = 0;
    _ripple_duration = clear ? clear_ripple_frames : 38;
    const int far_x = std::max(x, bn::display::width() - x);
    const int far_y = std::max(y, bn::display::height() - y) * 3 / 2;
    _ripple_range = bn::sqrt(far_x * far_x + far_y * far_y) + 16;
}

void ScreenEffects::update(bn::fixed result_fade)
{
    constexpr int shake_x[] = {2, 2, -2, -2, 2, 2, -1, -1, 1, 1, 0, 0};
    constexpr int shake_y[] = {1, 1, 0, 0, -1, -1, 0, 0, 0, 0, 0, 0};
    bn::fixed flash = 0;
    if(_miss_frames)
    {
        const int age = miss_frames - _miss_frames;
        _camera.set_position(shake_x[age], shake_y[age]);
        flash = bn::fixed(0.18) * _miss_frames * _miss_frames / (miss_frames * miss_frames);
        --_miss_frames;
    }
    else
    {
        _camera.set_position(0, 0);
    }

    // Combine the dimming and the brief warm flash in the board's own palette.
    const bn::fixed dim = result_fade * bn::fixed(0.46) * (1 - flash);
    const bn::fixed fade = dim + flash;
    auto palette = _board.palette();
    if(flash > 0)
    {
        const bn::fixed warm = flash.safe_division(fade);
        const bn::color tint(backdrop.red() + ((miss_flash.red() - backdrop.red()) * warm).integer(),
                             backdrop.green() + ((miss_flash.green() - backdrop.green()) * warm).integer(),
                             backdrop.blue() + ((miss_flash.blue() - backdrop.blue()) * warm).integer());
        palette.set_fade(tint, fade);
    }
    else
    {
        palette.set_fade(backdrop, fade);
    }

    if(_ripple_duration && _ripple_age < _ripple_duration)
    {
        update_ripple();
        ++_ripple_age;
    }
    else if(_ring)
    {
        stop_ripple();
    }
}

void ScreenEffects::update_ripple()
{
    const int left = _ripple_duration - _ripple_age;
    const int radius = 9 + (_ripple_range - 9) * _ripple_age / (_ripple_duration - 1);
    const int inner_radius = std::max(0, radius - 3 - 8 * left / _ripple_duration);
    const int strength = 3 + 4 * left / _ripple_duration;
    for(int row = 0; row < bn::display::height(); ++row)
    {
        const int dy = std::abs(row - _ripple_y) * 3 / 2;
        const auto bounds = [this, dy](int r) -> Boundaries
        {
            const int distance = r * r - dy * dy;
            if(distance <= 0)
            {
                return {};
            }
            const int half = bn::sqrt(distance);
            return {_ripple_x - half, _ripple_x + half + 1};
        };
        _outer_bounds[row] = bounds(radius);
        _inner_bounds[row] = bounds(inner_radius);

        // A pair of opposing offsets briefly bends the background at the wavefront.
        const int front = radius - dy;
        const int crest = std::max(0, 12 - std::abs(front - 5));
        const int trough = std::max(0, 12 - std::abs(front + 8));
        _raster[row] = (crest - trough) * strength / 12;
    }

    if(!_ring)
    {
        _ring = bn::regular_bg_items::wave_ring.create_bg(8, 48);
        _ring->set_priority(1);
        _ring->set_blending_bottom_enabled(true);

        // WIN0 takes priority over WIN1. Show the ring only between the two ellipses.
        auto outer = bn::rect_window::external();
        auto inner = bn::rect_window::internal();
        for(auto window : {outer, inner})
        {
            window.set_boundaries(-bn::display::height() / 2, -bn::display::width() / 2,
                                  bn::display::height() / 2, -bn::display::width() / 2);
            window.set_visible(true);
        }
        bn::window::outside().set_show_bg(*_ring, false);
        inner.set_show_bg(*_ring, false);
        _outer_window = bn::rect_window_boundaries_hbe_ptr::create_horizontal(
                outer, bn::span(_outer_bounds.data(), int(_outer_bounds.size())));
        _inner_window = bn::rect_window_boundaries_hbe_ptr::create_horizontal(
                inner, bn::span(_inner_bounds.data(), int(_inner_bounds.size())));
        _board_raster = bn::regular_bg_position_hbe_ptr::create_horizontal(
                _board, bn::span(_raster.data(), int(_raster.size())));
    }
    else
    {
        _outer_window->reload_deltas_ref();
        _inner_window->reload_deltas_ref();
        _board_raster->reload_deltas_ref();
    }
}

void ScreenEffects::stop_ripple()
{
    _outer_window.reset();
    _inner_window.reset();
    _board_raster.reset();
    bn::rect_window::internal().set_visible(false);
    bn::rect_window::external().set_visible(false);
    _ring.reset();
    _ripple_duration = 0;
}

void ScreenEffects::reset()
{
    if(_ring)
    {
        stop_ripple();
    }
    _ripple_duration = 0;
    _miss_frames = 0;
    _camera.set_position(0, 0);
    auto palette = _board.palette();
    palette.set_fade_intensity(0);
}
}
