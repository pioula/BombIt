#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <functional>
#include <vector>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/program_options.hpp>
#include <boost/thread.hpp>

#include "utils.h"

using std::cout;
using std::endl;
using std::string;
using std::optional;
using std::nullopt;
using std::variant;
using std::get;
using std::visit;
using std::function;
using std::exception;
using std::vector;
using std::cerr;
using std::unordered_map;
using std::copy_n;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace as = boost::asio;
namespace po = boost::program_options;

using player_num_t = uint8_t;
using coords_t = uint16_t;
using game_time_t = uint16_t;
using score_t = uint32_t;
using message_id_t = uint8_t;
using bomb_id_t = uint32_t;

/// Client -> GUI messages
constexpr message_id_t CG_LOBBY = 0;
constexpr message_id_t CG_GAME = 1;

/// GUI -> Client messages
constexpr message_id_t GC_PLACE_BOMB = 0;
constexpr message_id_t GC_PLACE_BLOCK = 1;
constexpr message_id_t GC_MOVE = 2;

/// Client -> Server messages
constexpr message_id_t CS_JOIN = 0;
constexpr message_id_t CS_PLACE_BOMB = 1;
constexpr message_id_t CS_PLACE_BLOCK = 2;
constexpr message_id_t CS_MOVE = 3;

/// Server -> Client messages
constexpr message_id_t SC_HELLO = 0;
constexpr message_id_t SC_ACCEPTED_PLAYER = 1;
constexpr message_id_t SC_GAME_STARTED = 2;
constexpr message_id_t SC_TURN = 3;
constexpr message_id_t SC_GAME_ENDED = 4;

enum State {
    IN_LOBBY,
    IN_GAME,
};

enum Direction {
    UP = 0,
    RIGHT = 1,
    DOWN = 2,
    LEFT = 3,
};

using player_t = struct player_t {
    name_t name;
    name_t address;
};

using player_map_t = unordered_map<player_num_t, player_t>;

using command_parameters_t = struct command_parameters {
    host_address_t gui_address;
    string player_name;
    port_t port = 0;
    host_address_t server_address;
};

/// additional structs
using server_info_t = struct server_info_t {
    name_t server_name;
    player_num_t players_count;
    coords_t size_x;
    coords_t size_y;
    game_time_t game_length;
};

using position_t = struct position_t {
    coords_t x;
    coords_t y;
};

using bomb_t = struct bomb_t {
    position_t position;
    game_time_t timer;
};

/// Client -> GUI server structs
using game_settings_t = struct game_settings_t {
    server_info_t server_info;
    uint16_t explosion_radius;
    game_time_t bomb_timer;
    player_map_t players;
};

using game_info_t = struct game_info_t {
    server_info_t server_info;
    uint16_t turn;
    player_map_t players;
    unordered_map<player_num_t, position_t> player_positions;
    size_t blocks_size;
    vector<position_t> blocks;
    size_t bombs_size;
    vector<bomb_t> bombs;
    size_t explosions_size;
    vector<position_t> explosions;
    unordered_map<player_num_t, score_t> scores;
};

/// GUI server -> Client structs
struct move_t { // Used also for Client -> server
    Direction direction;
};

/// Client -> Server
using join_t = struct join_t {
    name_t name;
};

/// Server -> Client
using hello_t = struct hello_t {
    server_info_t server_info;
    uint16_t explosion_radius;
    game_time_t bomb_timer;
};

using accepted_player_t = struct accepted_player_t {
    player_num_t player_id;
    player_t player;
};

using game_started_t = struct game_started_t {
    player_map_t players;
};

using bomb_placed_t = struct bomb_placed_t {
    bomb_id_t bomb_id;
    position_t position;
};

using bomb_exploded_t = struct bomb_exploded_t {
    bomb_id_t bomb_id;
    size_t robots_destroyed_size;
    vector<player_num_t> robots_destroyed;
    size_t blocks_destroyed_size;
    vector<position_t> blocks_destroyed;
};

using player_moved_t = struct player_moved_t {
    player_num_t player_id;
    position_t position;
};

using block_placed_t = struct block_placed_t {
    position_t position;
};

using event_t = variant<bomb_placed_t, bomb_exploded_t, player_moved_t, block_placed_t>;

using turn_t = struct turn_t {
    uint16_t turn;
    size_t events_size;
    vector<event_t> events;
};

using game_ended_t = struct game_ended_t {
    size_t scores_size;
    unordered_map<player_num_t, score_t> scores;
};

optional<command_parameters_t> parse_parameters(int argc, char *argv[]) {
    command_parameters_t command_parameters;
    bool with_help = false;

    vector<flag_t> flags{
        { "help", "h", nullopt, false, "produce help message",
            [&](po::variables_map &vm, auto &desc) {
                cout << desc << "\n";
                with_help = true;
            }}, // Help must be first in array flags
        { "gui-address", "d", po::value<string>(), true, "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
            [&](po::variables_map &vm, auto &desc) {
                cout << "gui\n";
                command_parameters.gui_address = parse_host_address(vm["gui-address"].as<string>());
            }},
        { "player-name", "n", po::value<string>(), true, "string",
            [&](po::variables_map &vm, auto &desc) {
                cout << "player\n";
                command_parameters.player_name = vm["player-name"].as<string>();
            }},
        { "port", "p", po::value<port_t>(), true, "Port na którym klient nasłuchuje komunikatów od GUI",
            [&](po::variables_map &vm, auto &desc) {
                cout << "port\n";
                command_parameters.port = vm["port"].as<port_t>();
            }},
        { "server-address", "s", po::value<string>(), true, "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
            [&](po::variables_map &vm, auto &desc) {
                cout << "server\n";
                command_parameters.server_address = parse_host_address(vm["server-address"].as<string>());
            }}
    };

    parse_command_line(argc, argv, flags);

    if (!with_help)
        return command_parameters;
    else
        return nullopt;
}

void join(datagram_t &buf, const string &name) {
    buf[0] = CS_JOIN;
    copy_string(buf, name, 1);
}

void from_gui_to_server(const command_parameters_t &cp) {
    UDPClient* gui_handler = UDPClient::get_instance(cp.gui_address, cp.port);
    TCPClient* server_handler = TCPClient::get_instance(cp.server_address);

    datagram_t gui_buf;
    datagram_t server_buf;

    gui_handler->receive(gui_buf);
    join(server_buf, cp.player_name);
    server_handler->send(server_buf);
    for (;;) {
        gui_handler->receive(gui_buf);
        gui_buf[0]++;
        server_handler->send(gui_buf);
    }
}
/*
[0] Lobby {
        server_name: String,
        players_count: u8,
        size_x: u16,
        size_y: u16,
        game_length: u16,
        explosion_radius: u16,
        bomb_timer: u16,
        players: Map<PlayerId, Player>
    },
    [1] Game {
        server_name: String,
        size_x: u16,
        size_y: u16,
        game_length: u16,
        turn: u16,
        players: Map<PlayerId, Player>,
        player_positions: Map<PlayerId, Position>,
        blocks: List<Position>,
        bombs: List<Bomb>,
        explosions: List<Position>,
        scores: Map<PlayerId, Score>,
    },

 [0] Hello {
        server_name: String,
        players_count: u8,
        size_x: u16,
        size_y: u16,
        game_length: u16,
        explosion_radius: u16,
        bomb_timer: u16,
    },
    [1] AcceptedPlayer {
        id: PlayerId,
        player: Player,
    },
 */
hello_t parse_hello(datagram_t server_buf) {
    hello_t res;
    size_t act_pos = 1;
    parse_string(server_buf, res.server_info.server_name, act_pos);
    parse_u8(server_buf, res.server_info.players_count, act_pos);
    parse_u16(server_buf, res.server_info.size_x, act_pos);
    parse_u16(server_buf, res.server_info.size_y, act_pos);
    parse_u16(server_buf, res.server_info.game_length, act_pos);
    parse_u16(server_buf, res.explosion_radius, act_pos);
    parse_u16(server_buf, res.bomb_timer, act_pos);

    return res;
}


void from_server_to_gui(const command_parameters_t &cp) {
    UDPClient* gui_handler = UDPClient::get_instance(cp.gui_address, cp.port);
    TCPClient* server_handler = TCPClient::get_instance(cp.server_address);

    datagram_t gui_buf;
    datagram_t server_buf;

    size_t len = server_handler->receive(server_buf);
    for (size_t i = 0; i < len; i++) {
        cout << server_buf[i];
    }
    puts("");
    hello_t server_info = parse_hello(server_buf);
    cout << server_info.server_info.server_name.data() << " " << (int)server_info.server_info.players_count << " " << server_info.server_info.game_length << " " << server_info.server_info.size_x << " " << server_info.server_info.size_y << " " << server_info.explosion_radius << " " << server_info.bomb_timer << " \n";
    puts("JAJO");
}

int main(int argc, char *argv[]) {
    command_parameters_t cp;
    try {
        if (auto cp_option = parse_parameters(argc, argv))
            cp = *cp_option;
        else
            return 0;
    }
    catch (MissingFlag &exp) {
        cerr << exp.what() << "\n";
        return 1;
    }

    from_server_to_gui(cp);

    cout << "End!" << endl;
}
