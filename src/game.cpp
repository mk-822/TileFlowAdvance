#include "game.h"

#include <algorithm>

namespace tf
{
namespace
{
constexpr Wave waves[] = {
    {"FIRST LIGHT", Goal::flow, 3},
    {"AFTERGLOW", Goal::flow, 5},
    {"REFRACTION", Goal::diagonal, 3},
    {"TIDAL FLOW", Goal::tiles, 30},
    {"HORIZON", Goal::horizontal, 4},
    {"RADIANCE", Goal::points, 2600}
};

struct DifficultyRules
{
    int speed_percent;
    int spawn_offset;
    int colors;
};

constexpr DifficultyRules difficulty_rules[] = {{80, 2700, 2}, {100, 0, 3}, {122, -2040, 4}};
constexpr int opening_lanes[] = {1, 3};
constexpr int frames_per_second = 60;
constexpr int fast_multiplier = 3;
// Spawn timers are in 1/6000-second units, belt distances in 1/30000 world units.
constexpr int spawn_tick = 100;
constexpr int extra_landing_frames = 13;

struct Direction
{
    int dx;
    int dy;
    int points;
    Goal goal;
};

constexpr Direction directions[] = {
    {0, 1, 100, Goal::flow},
    {1, 0, 250, Goal::horizontal},
    {1, 1, 500, Goal::diagonal},
    {1, -1, 500, Goal::diagonal}
};
}

Wave wave_info(int number)
{
    Wave wave = waves[(number - 1) % 6];
    const int cycle = (number - 1) / 6;
    const int increment = wave.goal == Goal::points ? 700 : wave.goal == Goal::tiles ? 5 : 1;
    wave.target += cycle * increment;
    return wave;
}

const char* goal_label(Goal goal)
{
    switch(goal)
    {
    case Goal::flow: return "FLOW";
    case Goal::diagonal: return "DIAGONALS";
    case Goal::tiles: return "TILES";
    case Goal::horizontal: return "HORIZONTAL";
    case Goal::points: return "POINTS";
    default: return "";
    }
}

Game::Game(EventHandler handler, void* context) : _handler(handler), _context(context)
{
}

void Game::seed(uint32_t value)
{
    _rng = value ? value : 1;
}

void Game::emit(const Event& event) const
{
    if(_handler)
    {
        _handler(_context, event);
    }
}

int Game::random(int limit)
{
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return int(_rng % unsigned(limit));
}

void Game::begin(Difficulty difficulty, int choice_index)
{
    if(_phase != Phase::ready && _phase != Phase::over && _phase != Phase::clear)
    {
        return;
    }
    const bool new_run = _phase != Phase::clear;
    const bool refill = new_run || can_skip();
    const Choice choice = choices[std::clamp(choice_index, 0, 2)];
    _wave = (new_run ? 1 : _wave + 1) + (refill ? choice.skip : 0);
    _wave_bonus = refill ? choice.bonus : 0;
    if(new_run)
    {
        _difficulty = difficulty;
        _score = 0;
    }
    if(refill)
    {
        _drop_limit = choice.drops;
        _drops = 0;
    }
    setup_wave();
    _phase = Phase::playing;
}

void Game::setup_wave()
{
    _wave_data = wave_info(_wave);
    _lane = 2;
    _progress = 0;
    _elapsed = 0;
    _opening_index = 0;
    _spawn_in = spawn_interval() * 45 / 100;
    _bag_size = 0;
    _belt_size = 0;
    _pending_size = 0;
    _line_count = 0;
    _clear_cells = 0;
    _resolution = Resolution::idle;
    _resolution_frames = 0;
    _stack.size = 0;
    _pool = {};
    _tiles = {};
    add_to_belt(_wave == 1 ? 0 : 1, _lane, belt_length - 4 * cell_length);
}

void Game::pause()
{
    if(_phase == Phase::playing)
    {
        _phase = Phase::paused;
    }
    else if(_phase == Phase::paused)
    {
        _phase = Phase::playing;
    }
}

void Game::move(int direction)
{
    if(_phase != Phase::playing)
    {
        return;
    }
    const int next = std::clamp(_lane + direction, 0, lanes - 1);
    if(next != _lane)
    {
        _lane = next;
        emit({EventType::move, -1, _lane});
    }
}

void Game::place()
{
    request(Action::place);
}

void Game::toss()
{
    request(Action::toss);
}

void Game::request(Action action)
{
    if(_phase != Phase::playing || _stack.size <= _pending_size)
    {
        return;
    }
    if(_resolution != Resolution::idle && (action == Action::place || _pending_size))
    {
        _pending[_pending_size++] = {action, _lane};
        apply_pending();
    }
    else
    {
        apply(action, _lane);
    }
}

bool Game::belt_stopped() const
{
    return _resolution == Resolution::highlight || _resolution == Resolution::settling;
}

void Game::apply_pending()
{
    while(_pending_size)
    {
        const auto [action, lane] = _pending.front();
        if(_resolution != Resolution::idle)
        {
            if(action != Action::place || !belt_stopped())
            {
                return;
            }
            const uint32_t column_mask = ((uint32_t(1) << rows) - 1) << (lane * rows);
            if(_pool[lane].size == rows && (_clear_cells & column_mask))
            {
                return;
            }
        }
        --_pending_size;
        for(int index = 0; index < _pending_size; ++index)
        {
            _pending[index] = _pending[index + 1];
        }
        apply(action, lane);
    }
}

void Game::apply(Action action, int lane)
{
    if(action == Action::toss)
    {
        return_tile(lane);
        return;
    }
    Column& column = _pool[lane];
    if(column.size == rows)
    {
        emit({EventType::blocked, -1, lane});
        return;
    }
    const int id = _stack.ids[--_stack.size];
    const int row = column.size++;
    column.ids[row] = id;
    if(_resolution == Resolution::idle)
    {
        _resolution = Resolution::landing;
        _resolution_frames = place_frames;
        _chain = 1;
    }
    else
    {
        const int landing = _resolution == Resolution::highlight ? extra_landing_frames : place_frames;
        _resolution_frames = std::max(_resolution_frames, landing);
    }
    emit({EventType::place, id, lane});
    if(_resolution == Resolution::highlight)
    {
        extend_line(lane, row, _tiles[id].color);
    }
}

void Game::return_tile(int lane)
{
    const int flight_travel = speed() * 44 * fast_multiplier / 100;
    for(int slot = belt_cells / 2; slot < belt_cells / 2 + 6; ++slot)
    {
        const int distance = slot * cell_length;
        bool open = true;
        for(int index = 0; index < _belt_size; ++index)
        {
            const Tile& tile = _tiles[_belt[index]];
            if(tile.lane == lane && tile.distance > distance - cell_length &&
               tile.distance < distance + flight_travel + cell_length)
            {
                open = false;
                break;
            }
        }
        if(open)
        {
            const int id = _stack.ids[--_stack.size];
            Tile& tile = _tiles[id];
            tile.lane = lane;
            tile.distance = distance;
            tile.flight = throw_frames;
            _belt[_belt_size++] = id;
            emit({EventType::toss, id, lane});
            return;
        }
    }
    emit({EventType::blocked, -1, lane});
}

int Game::speed() const
{
    const int base_speed = std::min(112500, 41400 + std::min(_wave - 1, 19) * 3900 + _elapsed * 2);
    return base_speed * difficulty_rules[int(_difficulty)].speed_percent / 100;
}

int Game::spawn_interval() const
{
    const int base_delay = 12600 - std::min(_wave, 25) * 570 + difficulty_rules[int(_difficulty)].spawn_offset;
    return std::max(4260, base_delay) * 2;
}

int Game::draw_color()
{
    if(!_bag_size)
    {
        const int color_count = std::min(colors, difficulty_rules[int(_difficulty)].colors + (_wave - 1) / 2);
        _bag_size = color_count * 3;
        for(int index = 0; index < _bag_size; ++index)
        {
            _bag[index] = index % color_count;
        }
        for(int index = _bag_size - 1; index > 0; --index)
        {
            std::swap(_bag[index], _bag[random(index + 1)]);
        }
    }
    return _bag[--_bag_size];
}

bool Game::add_to_belt(int color, int lane, int distance)
{
    for(int id = 0; id < max_tiles; ++id)
    {
        if(!_tiles[id].active)
        {
            _tiles[id] = {color, lane, distance, 0, true, false};
            _belt[_belt_size++] = id;
            return true;
        }
    }
    return false;
}

void Game::feed_belt()
{
    std::array<bool, lanes> open{};
    open.fill(true);
    for(int index = 0; index < _belt_size; ++index)
    {
        const Tile& tile = _tiles[_belt[index]];
        if(tile.distance > belt_length - cell_length)
        {
            open[tile.lane] = false;
        }
    }
    std::array<int, lanes> free_lanes{};
    int free_count = 0;
    for(int lane = 0; lane < lanes; ++lane)
    {
        if(open[lane])
        {
            free_lanes[free_count++] = lane;
        }
    }
    if(free_count)
    {
        const bool opening = _opening_index < 2;
        const int preferred = opening ? opening_lanes[_opening_index] : -1;
        const int lane = opening && open[preferred] ? preferred : free_lanes[random(free_count)];
        const int color = opening ? (_wave == 1 ? 0 : 1) : draw_color();
        if(add_to_belt(color, lane, belt_length))
        {
            _opening_index += int(opening);
            _spawn_in += spawn_interval();
            return;
        }
    }
    _spawn_in = 6 * spawn_tick;
}

int Game::color_at(int lane, int row) const
{
    if(lane >= 0 && lane < lanes && row >= 0 && row < _pool[lane].size)
    {
        return _tiles[_pool[lane].ids[row]].color;
    }
    return -1;
}

void Game::find_lines()
{
    _line_count = 0;
    for(int lane = 0; lane < lanes; ++lane)
    {
        for(int row = 0; row < _pool[lane].size; ++row)
        {
            const int color = color_at(lane, row);
            for(int index = 0; index < 4; ++index)
            {
                const Direction direction = directions[index];
                if(color_at(lane - direction.dx, row - direction.dy) == color)
                {
                    continue;
                }
                int length = 1;
                while(color_at(lane + length * direction.dx, row + length * direction.dy) == color)
                {
                    ++length;
                }
                if(length >= 3)
                {
                    _lines[_line_count++] = {lane, row, index, length};
                }
            }
        }
    }
}

void Game::score_lines()
{
    int points = 0;
    int progress = 0;
    uint32_t cells = 0;
    for(int index = 0; index < _line_count; ++index)
    {
        const Line& line = _lines[index];
        const Direction direction = directions[line.direction];
        for(int step = 0; step < line.length; ++step)
        {
            cells |= cell_bit(line.lane + step * direction.dx, line.row + step * direction.dy);
        }
        const int count = line.length - 2;
        points += direction.points * count * count * _chain;
        if(_wave_data.goal == Goal::flow || _wave_data.goal == direction.goal)
        {
            progress += count;
        }
    }
    if(_wave_data.goal == Goal::points)
    {
        progress = points;
    }
    _score += points - _match_points;
    _progress += progress - _match_progress;
    _match_points = points;
    _match_progress = progress;
    _clear_cells = cells;
}

void Game::extend_line(int lane, int row, int color)
{
    for(int index = 0; index < _line_count; ++index)
    {
        Line& line = _lines[index];
        if(line.direction == 0 && line.lane == lane && line.row + line.length == row &&
           color_at(lane, row - 1) == color)
        {
            ++line.length;
            score_lines();
            return;
        }
    }
}

void Game::clear_lines()
{
    int longest = 3;
    for(int index = 0; index < _line_count; ++index)
    {
        longest = std::max(longest, _lines[index].length);
    }
    emit({EventType::match, -1, -1, _clear_cells, _chain, _line_count, longest, _match_points});
    for(int lane = 0; lane < lanes; ++lane)
    {
        Column& column = _pool[lane];
        int remaining = 0;
        for(int row = 0; row < column.size; ++row)
        {
            const int id = column.ids[row];
            if(_clear_cells & cell_bit(lane, row))
            {
                _tiles[id].active = false;
            }
            else
            {
                column.ids[remaining++] = id;
            }
        }
        column.size = remaining;
    }
    _clear_cells = 0;
    _resolution = Resolution::settling;
    _resolution_frames = settle_frames;
    ++_chain;
}

void Game::resolve()
{
    if(_resolution == Resolution::highlight)
    {
        clear_lines();
        return;
    }
    find_lines();
    if(_line_count)
    {
        _match_points = 0;
        _match_progress = 0;
        score_lines();
        _resolution = Resolution::highlight;
        _resolution_frames = highlight_frames;
    }
    else
    {
        _resolution = Resolution::idle;
        if(std::all_of(_pool.begin(), _pool.end(), [](const Column& column) { return column.size == rows; }))
        {
            end(EndReason::full);
        }
    }
}

void Game::end(EndReason reason)
{
    _phase = Phase::over;
    _end_reason = reason;
    emit({EventType::over});
}

void Game::arrive(int id)
{
    Tile& tile = _tiles[id];
    if(!tile.arrived && _wave_data.goal == Goal::tiles)
    {
        ++_progress;
    }
    if(tile.lane == _lane && _stack.size < rows)
    {
        _stack.ids[_stack.size++] = id;
        if(!tile.arrived)
        {
            _score += 5;
            if(_wave_data.goal == Goal::points)
            {
                _progress += 5;
            }
        }
        tile.arrived = true;
        emit({EventType::catch_tile, id, tile.lane});
    }
    else
    {
        tile.active = false;
        ++_drops;
        emit({EventType::miss, id, tile.lane});
        if(!drops_remaining())
        {
            end(EndReason::drops);
        }
    }
}

void Game::advance_belt(int travel)
{
    std::array<int, max_tiles> arrivals{};
    int arrival_count = 0;
    int remaining = 0;
    int nearest = -1;
    int nearest_distance = belt_length + 1;
    for(int index = 0; index < _belt_size; ++index)
    {
        const int id = _belt[index];
        Tile& tile = _tiles[id];
        if(tile.flight)
        {
            --tile.flight;
        }
        else if(travel)
        {
            if(tile.distance < nearest_distance)
            {
                nearest = id;
                nearest_distance = tile.distance;
            }
            tile.distance -= travel;
            if(tile.distance <= 0)
            {
                // Resolve the frontmost arrivals first, preserving belt order for ties.
                int position = arrival_count++;
                while(position && _tiles[arrivals[position - 1]].distance > tile.distance)
                {
                    arrivals[position] = arrivals[position - 1];
                    --position;
                }
                arrivals[position] = id;
                continue;
            }
        }
        _belt[remaining++] = id;
    }
    _belt_size = remaining;
    if(nearest >= 0 && _tiles[nearest].distance > 0 &&
       (belt_length - nearest_distance) / cell_length != (belt_length - _tiles[nearest].distance) / cell_length)
    {
        emit({EventType::tumble, nearest, _tiles[nearest].lane});
    }
    for(int index = 0; index < arrival_count; ++index)
    {
        const int id = arrivals[index];
        if(_phase == Phase::playing)
        {
            arrive(id);
        }
        else
        {
            _tiles[id].active = false;
        }
    }
}

void Game::step(bool fast)
{
    if(_phase != Phase::playing)
    {
        return;
    }
    const bool was_stopped = belt_stopped();
    if(_resolution != Resolution::idle && --_resolution_frames <= 0)
    {
        resolve();
    }
    if(_phase != Phase::playing)
    {
        return;
    }
    if(_resolution == Resolution::idle && _progress >= _wave_data.target)
    {
        _phase = Phase::clear;
        _score += _wave * 250 + 50 * drops_remaining() + _wave_bonus;
        emit({EventType::clear});
        return;
    }
    apply_pending();
    const bool stopped = was_stopped || belt_stopped();
    const int multiplier = stopped ? 0 : fast ? fast_multiplier : 1;
    if(!stopped && _elapsed < 60000)
    {
        ++_elapsed;
    }
    advance_belt(speed() * multiplier / frames_per_second);
    if(_phase == Phase::playing && multiplier)
    {
        _spawn_in -= spawn_tick * multiplier;
        if(_spawn_in <= 0)
        {
            feed_belt();
        }
    }
}
}
