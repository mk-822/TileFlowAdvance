#ifndef TILE_FLOW_GAME_H
#define TILE_FLOW_GAME_H

#include <array>
#include <cstdint>

namespace tf
{
constexpr int lanes = 5;
constexpr int rows = 5;
constexpr int colors = 6;
constexpr int belt_cells = 20;
constexpr int cell_length = 29000;
constexpr int belt_length = belt_cells * cell_length;
constexpr int max_tiles = 72;
constexpr int place_frames = 10;
constexpr int highlight_frames = 24;
constexpr int settle_frames = 36;
constexpr int throw_frames = 27;

enum class Difficulty { easy, normal, hard };
enum class Phase { ready, playing, paused, clear, over };
enum class Goal { flow, diagonal, tiles, horizontal, points };
enum class EndReason { drops, full };
enum class EventType { move, place, toss, blocked, catch_tile, miss, tumble, match, clear, over };

struct Choice
{
    int skip;
    int drops;
    int bonus;
};

inline constexpr Choice choices[] = {{0, 3, 0}, {5, 4, 100000}, {10, 5, 200000}};

struct Wave
{
    const char* title;
    Goal goal;
    int target;
};

Wave wave_info(int number);
const char* goal_label(Goal goal);

struct Event
{
    EventType type;
    int tile = -1;
    int lane = -1;
    uint32_t cells = 0;
    int chain = 0;
    int lines = 0;
    int longest = 0;
    int points = 0;
};

struct Tile
{
    int color = 0;
    int lane = 0;
    int distance = 0;
    int flight = 0;
    bool active = false;
    bool arrived = false;
};

struct Column
{
    std::array<int, rows> ids{};
    int size = 0;
};

constexpr uint32_t cell_bit(int lane, int row)
{
    return uint32_t(1) << (lane * rows + row);
}

class Game
{
public:
    using EventHandler = void (*)(void*, const Event&);

    explicit Game(EventHandler handler = nullptr, void* context = nullptr);
    void seed(uint32_t value);
    void begin(Difficulty difficulty, int choice_index);
    void pause();
    void move(int direction);
    void place();
    void toss();
    void step(bool fast);

    [[nodiscard]] Phase phase() const { return _phase; }
    [[nodiscard]] Difficulty difficulty() const { return _difficulty; }
    [[nodiscard]] int wave_number() const { return _wave; }
    [[nodiscard]] const Wave& wave() const { return _wave_data; }
    [[nodiscard]] int score() const { return _score; }
    [[nodiscard]] int progress() const { return _progress; }
    [[nodiscard]] int drop_limit() const { return _drop_limit; }
    [[nodiscard]] int drops_remaining() const { return _drop_limit - _drops; }
    [[nodiscard]] int wave_bonus() const { return _wave_bonus; }
    [[nodiscard]] bool can_skip() const { return _phase == Phase::clear && _wave % 5 == 0; }
    [[nodiscard]] EndReason end_reason() const { return _end_reason; }
    [[nodiscard]] int lane() const { return _lane; }
    [[nodiscard]] const auto& tiles() const { return _tiles; }
    [[nodiscard]] const auto& pool() const { return _pool; }
    [[nodiscard]] const Column& stack() const { return _stack; }
    [[nodiscard]] int belt_size() const { return _belt_size; }
    [[nodiscard]] int belt_id(int index) const { return _belt[index]; }
    [[nodiscard]] uint32_t highlighted() const { return _clear_cells; }
    [[nodiscard]] bool belt_stopped() const;

private:
    enum class Action { place, toss };
    enum class Resolution { idle, landing, highlight, settling };

    struct PendingAction
    {
        Action action;
        int lane;
    };

    struct Line
    {
        int lane;
        int row;
        int direction;
        int length;
    };

    EventHandler _handler;
    void* _context;
    uint32_t _rng = 1;
    Phase _phase = Phase::ready;
    Difficulty _difficulty = Difficulty::normal;
    EndReason _end_reason = EndReason::drops;
    int _wave = 1;
    Wave _wave_data = wave_info(1);
    int _score = 0;
    int _progress = 0;
    int _drops = 0;
    int _drop_limit = 3;
    int _wave_bonus = 0;
    int _lane = 2;
    int _elapsed = 0;
    int _spawn_in = 0;
    int _opening_index = 0;
    int _bag_size = 0;
    int _belt_size = 0;
    int _pending_size = 0;
    int _line_count = 0;
    int _resolution_frames = 0;
    int _chain = 1;
    int _match_points = 0;
    int _match_progress = 0;
    uint32_t _clear_cells = 0;
    Resolution _resolution = Resolution::idle;
    Column _stack{};
    std::array<Column, lanes> _pool{};
    std::array<Tile, max_tiles> _tiles{};
    std::array<int, max_tiles> _belt{};
    std::array<int, colors * 3> _bag{};
    std::array<PendingAction, rows> _pending{};
    // Five rows, five columns and five diagonals of length >= 3 in each direction.
    std::array<Line, 20> _lines{};

    void emit(const Event& event) const;
    int random(int limit);
    void setup_wave();
    int speed() const;
    int spawn_interval() const;
    int draw_color();
    bool add_to_belt(int color, int lane, int distance);
    void feed_belt();
    void advance_belt(int travel);
    void arrive(int id);
    void end(EndReason reason);
    void request(Action action);
    void apply_pending();
    void apply(Action action, int lane);
    void return_tile(int lane);
    int color_at(int lane, int row) const;
    void find_lines();
    void score_lines();
    void extend_line(int lane, int row, int color);
    void resolve();
    void clear_lines();
};
}

#endif
