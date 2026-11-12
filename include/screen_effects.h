#ifndef TILE_FLOW_SCREEN_EFFECTS_H
#define TILE_FLOW_SCREEN_EFFECTS_H

#include <array>
#include "bn_camera_ptr.h"
#include "bn_display.h"
#include "bn_optional.h"
#include "bn_rect_window_boundaries_hbe_ptr.h"
#include "bn_regular_bg_position_hbe_ptr.h"

namespace tf
{
class ScreenEffects
{
public:
    static constexpr int clear_ripple_frames = 48;

    explicit ScreenEffects(const bn::regular_bg_ptr& board);
    [[nodiscard]] const bn::camera_ptr& camera() const { return _camera; }
    void miss();
    void ripple(int x, int y, bool clear);
    void update(bn::fixed result_fade);
    void reset();

private:
    using Boundaries = bn::pair<bn::fixed, bn::fixed>;

    bn::regular_bg_ptr _board;
    bn::camera_ptr _camera;
    std::array<Boundaries, bn::display::height()> _outer_bounds{};
    std::array<Boundaries, bn::display::height()> _inner_bounds{};
    std::array<bn::fixed, bn::display::height()> _raster{};
    bn::optional<bn::regular_bg_ptr> _ring;
    bn::optional<bn::rect_window_boundaries_hbe_ptr> _outer_window;
    bn::optional<bn::rect_window_boundaries_hbe_ptr> _inner_window;
    bn::optional<bn::regular_bg_position_hbe_ptr> _board_raster;
    int _miss_frames = 0;
    int _ripple_age = 0;
    int _ripple_duration = 0;
    int _ripple_x = 0;
    int _ripple_y = 0;
    int _ripple_range = 0;

    void update_ripple();
    void stop_ripple();
};
}

#endif
