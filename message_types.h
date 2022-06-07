#ifndef TYPES_H
#define TYPES_H
#include <cstdint>
#include <boost/array.hpp>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <variant>

using std::string;

using player_num_t = uint8_t;
using name_t = struct name_t {
    boost::array<char, 256> name;
    uint8_t len;
};
using player_t = struct player_t {
    name_t name;
    name_t address;
};

using score_t = uint32_t;
using message_id_t = uint8_t;
using turn_t = uint16_t;
using explosion_radius_t = uint16_t;
using player_map_t = std::unordered_map<player_num_t, player_t>;
using player_list_t = std::unordered_set<player_num_t>;
using port_t = uint16_t;
using coords_t = uint16_t;
using container_size_t = uint32_t;
using bomb_id_t = uint32_t;
using game_time_t = uint16_t;
using direction_t = uint8_t;
using turn_dur_t = uint64_t;
using block_count_t = uint16_t;
using seed_t = uint32_t;
using simple_name_t = std::string;
using scores_t = std::unordered_map<player_num_t, score_t>;

using message_length_t = uint16_t;

using position_t = struct position_t {
    coords_t x;
    coords_t y;
    bool operator==(const position_t& other) const {
        return x == other.x && y == other.y;
    };
};

class PositionHash {
public:
    size_t operator()(const position_t &p) const {
        return ((size_t)p.x << 16) | p.y;
    }
};

using position_set = std::unordered_set<position_t, PositionHash>;

using bomb_t = struct bomb_t {
    position_t position;
    game_time_t timer;
};

using hello_t = struct hello_t {
    name_t server_name;
    player_num_t players_count;
    coords_t size_x;
    coords_t size_y;
    game_time_t game_length;
    explosion_radius_t explosion_radius;
    game_time_t bomb_timer;
};

using accepted_player_t = struct {
    player_num_t id;
    player_t player;
};

using bomb_placed_t = struct {
    bomb_id_t bomb_id;
    position_t position;
};

using bomb_exploded_t = struct {
    bomb_id_t bomb_id;
    player_list_t robots_destroyed;
    position_set blocks_destroyed;
};

using player_moved_t = struct {
    player_num_t player_id;
    position_t position;
};

using block_placed_t = struct {
    position_t position;
};


using event_t = std::variant<bomb_placed_t, bomb_exploded_t, player_moved_t,
                            block_placed_t>;
using event_list_t = std::vector<event_t>;

using game_turn_t = struct {
    turn_t turn;
    event_list_t events;
};

enum Direction {
    UP = 0,
    RIGHT = 1,
    DOWN = 2,
    LEFT = 3
};

using move_t = struct move_t {
    message_id_t m_id;
    Direction direction;
};

using join_t = struct join_t {
    name_t name;
};

enum PlayerAction {
    PLACE_BLOCK,
    PLACE_BOMB
};

using player_action_t = std::variant<PlayerAction, move_t>;

constexpr message_length_t GC_PLACE_BOMB_LENGTH = 1;
constexpr message_length_t GC_PLACE_BLOCK_LENGTH = 1;
constexpr message_length_t GC_MOVE_LENGTH = 2;
constexpr direction_t MAX_DIRECTION = 3;

// Komunikaty przesyłane gui do klienta.
constexpr message_id_t GC_PLACE_BOMB = 0;
constexpr message_id_t GC_PLACE_BLOCK = 1;
constexpr message_id_t GC_MOVE = 2;

// Komunikaty przesyłane od klienta do gui.
constexpr message_id_t CG_LOBBY = 0;
constexpr message_id_t CG_GAME = 1;

// Komunikaty przesyłane od klienta do serwera.
constexpr message_id_t CS_JOIN = 0;
constexpr message_id_t CS_PLACE_BOMB = 1;
constexpr message_id_t CS_PLACE_BLOCK = 2;
constexpr message_id_t CS_MOVE = 3;

// Komunikaty przesyłane od serwera do klienta.
constexpr message_id_t SC_HELLO = 0;
constexpr message_id_t SC_ACCEPTED_PLAYER = 1;
constexpr message_id_t SC_GAME_STARTED = 2;
constexpr message_id_t SC_TURN = 3;
constexpr message_id_t SC_GAME_ENDED = 4;

// Zdarzenia wysyłane od serwera do klienta.
constexpr message_id_t BOMB_PLACED = 0;
constexpr message_id_t BOMB_EXPLODED = 1;
constexpr message_id_t PLAYER_MOVED = 2;
constexpr message_id_t BLOCK_PLACED = 3;

// Funkcja zamieniająca string na tym name_t.
// argumenty:
// - s - string poddawany konwersji
// return - name_t stworzony z stringa s
inline name_t string_to_name(const std::string &s) {
    name_t res;
    copy_n(s.begin(), s.length(),
           res.name.begin());
    res.len = s.length();
    return res;
}

#endif // TYPES_H