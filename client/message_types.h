#ifndef TYPES_H
#define TYPES_H
#include <cstdint>
#include <boost/array.hpp>
#include <unordered_map>
#include <unordered_set>

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
using port_t = uint16_t;
using coords_t = uint16_t;
using container_size_t = uint32_t;
using bomb_id_t = uint32_t;
using game_time_t = uint16_t;


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

constexpr message_id_t GC_PLACE_BOMB = 0;
constexpr message_id_t GC_PLACE_BLOCK = 1;
constexpr message_id_t GC_MOVE = 2;

constexpr message_id_t CG_LOBBY = 0;
constexpr message_id_t CG_GAME = 1;

constexpr message_id_t CS_JOIN = 0;

constexpr message_id_t SC_ACCEPTED_PLAYER = 1;
constexpr message_id_t SC_GAME_STARTED = 2;
constexpr message_id_t SC_TURN = 3;
constexpr message_id_t SC_GAME_ENDED = 4;

constexpr message_id_t BOMB_PLACED = 0;
constexpr message_id_t BOMB_EXPLODED = 1;
constexpr message_id_t PLAYER_MOVED = 2;
constexpr message_id_t BLOCK_PLACED = 3;

#endif // TYPES_H