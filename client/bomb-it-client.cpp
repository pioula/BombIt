#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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
using std::unordered_set;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace as = boost::asio;
namespace po = boost::program_options;

using player_num_t = uint8_t;
using score_t = uint32_t;
using message_id_t = uint8_t;
using container_size_t = uint32_t;
using turn_t = uint16_t;

constexpr player_num_t MAX_PLAYER_ID = 25;

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

constexpr message_id_t BOMB_PLACED = 0;
constexpr message_id_t BOMB_EXPLOADED = 1;
constexpr message_id_t PLAYER_MOVED = 2;
constexpr message_id_t BLOCK_PLACED = 3;

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

using game_ended_t = struct game_ended_t {
    size_t scores_size;
    unordered_map<player_num_t, score_t> scores;
};

void printf_datagram(datagram_t &d, size_t len) {
    for (size_t i = 0; i < len; i++) {
        cout << d[i] << " ";
    }
    puts("");
}

void printf_dataHandler(DatagramHandler &d) {
    for (size_t i = 0; i < d.get_len(); i++) {
        cout << ((uint16_t)(*d.get_buf())[i])%256 << " " << ((uint8_t)(*d.get_buf())[i]) << "\n";
    }
    puts("");
}

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

DatagramHandler join(const string &name) {
    DatagramHandler buf;
    message_id_t tmp = CS_JOIN;
    return *buf.write(tmp)->write(name);
}

void from_gui_to_server(const command_parameters_t &cp) {
    UDPClient* gui_handler = UDPClient::get_instance(cp.gui_address, cp.port);
    TCPClient* server_handler = TCPClient::get_instance(cp.server_address);

    datagram_t gui_buf;

    gui_handler->receive(gui_buf);
    puts("1: Jestem");
    DatagramHandler join_mes = join(cp.player_name);
    puts("1: Dalej jestem");
    printf_dataHandler(join_mes);
    server_handler->send(join_mes);
    puts("1: Wysłałem");
    for (;;) {
        size_t len = gui_handler->receive(gui_buf);
        gui_buf[0]++;
        server_handler->send(gui_buf, len);
    }
}

hello_t handle_hello(DatagramHandler &hello_buf) {
    hello_t res;
    message_id_t m;
    hello_buf.read(m)
        ->read(res.server_info.server_name)
        ->read(res.server_info.players_count)
        ->read(res.server_info.size_x)
        ->read(res.server_info.size_y)
        ->read(res.server_info.game_length)
        ->read(res.explosion_radius)
        ->read(res.bomb_timer);

    return res;
}

class LobbyBuf {
private:
    DatagramHandler lobby_buf;
    DatagramHandler player_map;
    container_size_t number_of_players;
public:
    LobbyBuf(const datagram_t &hello_buf, const size_t hello_len) :
        number_of_players(0),
        lobby_buf(DatagramHandler(hello_buf, hello_len)) {}

    DatagramHandler add_player(DatagramHandler accepted_player) {
        number_of_players++;
        player_map.write(number_of_players)
            ->write(*accepted_player.get_buf(),
                         accepted_player.get_len() - 1, sizeof(message_id_t));
        return lobby_buf.concat(player_map);
    }
};

class GameInfo {
private:
    server_info_t server_info;
    DatagramHandler players;
    unordered_map<player_num_t, position_t> player_positions;
    position_set blocks;
    unordered_map<bomb_id_t, bomb_t> bombs;
    unordered_map<player_num_t, score_t> scores;
    turn_t current_turn;
    game_time_t base_bomb_time;
    vector<position_t> explosions;

    void handle_bomb_placed(DatagramHandler &turn) {
        bomb_id_t bomb_id;
        bomb_t new_bomb;
        new_bomb.timer = base_bomb_time;
        turn.read(bomb_id)->read(new_bomb.position);
        bombs[bomb_id] = new_bomb;
    }

    void handle_bomb_exploaded(DatagramHandler &turn) {
        bomb_id_t bomb_id;
        turn.read(bomb_id);
        explosions.push_back(bombs[bomb_id].position);
        player_num_t robots_destroyed;
        turn.read(robots_destroyed);

        for (player_num_t i = 0; i < robots_destroyed; i++) {
            player_num_t player_id;
            turn.read(player_id);
            if (!scores.contains(player_id)) scores[player_id] = 0;
            scores[player_id]++;
        }

        container_size_t block_count;
        turn.read(block_count);

        for (container_size_t i = 0; i < block_count; i++) {
            position_t position;
            turn.read(position);
            blocks.erase(position);
        }
    }

    void handle_player_moved(DatagramHandler &turn) {
        player_num_t id;
        position_t pos;
        turn.read(id)->read(pos);
        player_positions[id] = pos;
    }

    void handle_block_placed(DatagramHandler &turn) {
        position_t pos;
        turn.read(pos);
        blocks.insert(pos);
    }

    DatagramHandler generate_game_info() {
        DatagramHandler game_info;
        game_info.write(server_info.server_name)
            ->write(server_info.size_x)
            ->write(server_info.size_y)
            ->write(server_info.game_length)
            ->write(current_turn)
            ->write(*players.get_buf(), players.get_len(), sizeof(message_id_t));

        game_info.write((player_num_t)player_positions.size());
        for (const auto &position: player_positions) {
            game_info.write(position.first) // player_id
                ->write(position.second); // position
        }

        game_info.write((container_size_t)blocks.size());
        for (const auto &position: blocks) {
            game_info.write(position);
        }

        game_info.write((container_size_t)bombs.size());
        for (const auto &bomb: bombs) {
            game_info.write(bomb.second);
        }

        game_info.write((container_size_t)explosions.size());
        for (const auto &position: explosions) {
            game_info.write(position);
        }

        game_info.write((player_num_t)scores.size());
        for (const auto &score: scores) {
            game_info.write(score.first) // player_id
                ->write(score.second); // score
        }

        return game_info;
    }

public:
    GameInfo(const server_info_t _server_info, const game_time_t _base_bomb_time) :
        server_info(_server_info), base_bomb_time(_base_bomb_time) {};

    void set_gamers(const DatagramHandler &gamers) {
        players = gamers;
    }

    DatagramHandler handle_turn(DatagramHandler &turn) {
        explosions.clear();
        container_size_t events_count;
        turn.read(current_turn)->read(events_count);
        for (container_size_t i = 0; i < events_count; i++) {
            message_id_t event_id;
            turn.read(event_id);
            switch (event_id) {
                case BOMB_PLACED:
                    handle_bomb_placed(turn);
                    break;
                case BOMB_EXPLOADED:
                    handle_bomb_exploaded(turn);
                    break;
                case PLAYER_MOVED:
                    handle_player_moved(turn);
                    break;
                case BLOCK_PLACED:
                    handle_block_placed(turn);
                    break;
                default:
                    continue;
            }
        }

        return generate_game_info();
    }

};

void printf_hello(hello_t &h) {
    cout << h.server_info.server_name.name.data() << " " <<  (int)h.server_info.players_count << " " << h.server_info.size_x << " " << h.server_info.size_y << " " << h.server_info.game_length << " " << h.explosion_radius << " " << h.bomb_timer << "\n";
}

void from_server_to_gui(const command_parameters_t &cp) {
    UDPClient* gui_handler = UDPClient::get_instance(cp.gui_address, cp.port);
    TCPClient* server_handler = TCPClient::get_instance(cp.server_address);

    datagram_t gui_buf;
    datagram_t server_buf;

    size_t hello_len = server_handler->receive(server_buf);
    DatagramHandler hello_buf(server_buf, hello_len);

    LobbyBuf lobby_buf(server_buf, hello_len);

    hello_t hello = handle_hello(hello_buf);
    printf_hello(hello);

    State current_state = State::IN_LOBBY;

    GameInfo game_info(hello.server_info, hello.bomb_timer);

    for (;;) {
        datagram_t read_buf;
        puts("2: czekam");
        size_t len = server_handler->receive(read_buf);
        puts("2: siema");
        DatagramHandler message_reader(read_buf, len);
        printf_dataHandler(message_reader);
        uint8_t message;
        message_reader.read(message);
        puts("XD?");
        cout << "2: mess: " << (int)message << " " << (int)SC_TURN << endl;
        switch (message) {
            case SC_ACCEPTED_PLAYER:
                if (current_state == State::IN_GAME) continue;
                puts("2: Accepted Player");
                message_reader = lobby_buf.add_player(message_reader);
                gui_handler->send(message_reader);
                break;
            case SC_GAME_STARTED:
                puts("2: Game Started");
                if (current_state == State::IN_GAME) continue;
                current_state = State::IN_GAME;
                game_info.set_gamers(message_reader);
                break;
            case SC_TURN:
                puts("2: Turn");
                if (current_state == State::IN_LOBBY) continue;
                message_reader = game_info.handle_turn(message_reader);
                gui_handler->send(message_reader);
                break;
            case SC_GAME_ENDED:
                puts("2: Game Ended");
                if (current_state == State::IN_LOBBY) continue;
                current_state = State::IN_LOBBY;
                break;
            default:
                continue;
        }
    }
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

    boost::thread t1{from_gui_to_server, cp};
    boost::thread t2{from_server_to_gui, cp};

    t1.join();
    cout << "End!" << endl;
}
