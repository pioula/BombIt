#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <csignal>

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
using turn_t = uint16_t;
using explosion_radius_t = uint16_t;

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

enum StateType {
    IDLE,
    IN_LOBBY,
    IN_GAME,
};

class GameState {
private:
    mutable boost::mutex mutex;
    StateType state;
public:
    GameState(): state(StateType::IDLE) {};

    StateType get_state() const {
        boost::lock_guard<boost::mutex> guard(mutex);
        return state;
    }

    void set_state(const StateType &new_state) {
        boost::lock_guard<boost::mutex> guard(mutex);
        state = new_state;
    }
};

GameState game_state{};

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

using hello_t = struct hello_t {
    server_info_t server_info;
    explosion_radius_t explosion_radius;
    game_time_t bomb_timer;
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
                command_parameters.gui_address = parse_host_address(vm["gui-address"].as<string>());
            }},
        { "player-name", "n", po::value<string>(), true, "string",
            [&](po::variables_map &vm, auto &desc) {
                command_parameters.player_name = vm["player-name"].as<string>();
            }},
        { "port", "p", po::value<port_t>(), true, "Port na którym klient nasłuchuje komunikatów od GUI",
            [&](po::variables_map &vm, auto &desc) {
                command_parameters.port = vm["port"].as<port_t>();
            }},
        { "server-address", "s", po::value<string>(), true, "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
            [&](po::variables_map &vm, auto &desc) {
                command_parameters.server_address = parse_host_address(vm["server-address"].as<string>());
            }}
    };

    parse_command_line(argc, argv, flags);

    if (!with_help)
        return command_parameters;
    else
        return nullopt;
}

void send_join(const command_parameters_t &cp) {
    DatagramWriter buf(TCPClient::get_instance());
    message_id_t tmp = CS_JOIN;
    buf.write(tmp)->write(cp.player_name)->send();
}

bool validate_gui_message(datagram_t gui_buf) {
    if (gui_buf.len == 0) return false;
    message_id_t message = gui_buf.buf[0];
    switch (message) {
        case GC_PLACE_BOMB:
        case GC_PLACE_BLOCK:
            return gui_buf.len == 1;
        case GC_MOVE:
            return gui_buf.len == 2 && gui_buf.buf[1] >= 0 && gui_buf.buf[1] <= 3;
        default:
            return false;
    }
}

void from_gui_to_server(const command_parameters_t &cp) {
    UDPClient* gui_handler = UDPClient::get_instance();
    TCPClient* server_handler = TCPClient::get_instance();

    datagram_t gui_buf;
    for (;;) {
        gui_handler->read_some(gui_buf);
        if (!validate_gui_message(gui_buf)) continue;
        if (game_state.get_state() == StateType::IN_LOBBY) {
            send_join(cp);
        }
        else if (game_state.get_state() == StateType::IN_GAME){
            gui_buf.buf[0]++;
            server_handler->send(gui_buf);
        }
    }
}

hello_t handle_hello(DatagramReader &server_handler) {
    hello_t res;
    message_id_t m;
    server_handler.read(m)
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
    hello_t hello;
    player_map_t player_map{};
public:
    LobbyBuf(const hello_t &_hello) : hello(_hello) {}

    void add_player(DatagramReader &accepted_player) {
        player_num_t player_id;
        player_t player;
        accepted_player.read(player_id);
        accepted_player.read(player);
        player_map[player_id] = player;
    }

    void send(DatagramWriter &gui_handler) {
        gui_handler.clear();
        gui_handler.write(CG_LOBBY)
            ->write(hello.server_info.server_name)
            ->write(hello.server_info.players_count)
            ->write(hello.server_info.size_x)
            ->write(hello.server_info.size_y)
            ->write(hello.server_info.game_length)
            ->write(hello.explosion_radius)
            ->write(hello.bomb_timer)
            ->write(player_map)
            ->send();
    }
};

class GameInfo {
private:
    hello_t                                 game_info;
    turn_t                                  current_turn;
    player_map_t                            players{};
    unordered_map<player_num_t, position_t> player_positions{};
    position_set                            blocks{};
    unordered_map<bomb_id_t, bomb_t>        bombs{};
    position_set                            explosions{};
    unordered_map<player_num_t, score_t>    scores{};

    unordered_set<player_num_t>             destroyed_robots{};
    position_set                            destroyed_blocks{};

    void handle_bomb_placed(DatagramReader &turn) {
        bomb_id_t bomb_id;
        bomb_t new_bomb;
        new_bomb.timer = game_info.bomb_timer;
        turn.read(bomb_id)->read(new_bomb.position);
        bombs[bomb_id] = new_bomb;
    }

    bool is_position_valid(const position_t &position) const {
        return position.x < game_info.server_info.size_x &&
            position.x >= 0 && position.y >= 0 &&
            position.y < game_info.server_info.size_y;
    }

    void handle_explosion_stripe(position_t scanner,
                                 const function<void(position_t&)> &shifter) {
        for (explosion_radius_t i = 0; i < game_info.explosion_radius; i++) {
            shifter(scanner);
            if (!is_position_valid(scanner)) break;
            explosions.insert(scanner);
            if (blocks.contains(scanner)) break;
        }
    }

    void handle_explosions(const position_t &bomb_position) {
        explosions.insert(bomb_position);
        if (!blocks.contains(bomb_position)) {
            handle_explosion_stripe(bomb_position,
                                    [](position_t &p){p.x++;});
            handle_explosion_stripe(bomb_position,
                                    [](position_t &p){p.x--;});
            handle_explosion_stripe(bomb_position,
                                    [](position_t &p){p.y++;});
            handle_explosion_stripe(bomb_position,
                                    [](position_t &p){p.y--;});
        }
    }

    void handle_bomb_exploded(DatagramReader &turn) {
        bomb_id_t bomb_id;
        turn.read(bomb_id);

        position_t bomb_position = bombs[bomb_id].position;
        bombs.erase(bomb_id);

        container_size_t robots_destroyed;
        turn.read(robots_destroyed);

        for (container_size_t i = 0; i < robots_destroyed; i++) {
            player_num_t player_id;
            turn.read(player_id);
            destroyed_robots.insert(player_id);
        }

        handle_explosions(bomb_position);

        container_size_t block_count;
        turn.read(block_count);

        for (container_size_t i = 0; i < block_count; i++) {
            position_t position;
            turn.read(position);
            destroyed_blocks.insert(position);
        }
    }

    void handle_player_moved(DatagramReader &turn) {
        player_num_t id;
        position_t pos;
        turn.read(id)->read(pos);
        player_positions[id] = pos;
    }

    void handle_block_placed(DatagramReader &turn) {
        position_t pos;
        turn.read(pos);
        blocks.insert(pos);
    }

public:
    GameInfo(const hello_t _game_info) :
        game_info(_game_info) {};

    void set_gamers(DatagramReader &gamers) {
        container_size_t gamers_size;
        gamers.read(gamers_size);
        for (container_size_t i = 0; i < gamers_size; i++) {
            player_num_t player_id;
            player_t player;
            gamers.read(player_id)->read(player);
            players[player_id] = player;
            scores[player_id] = 0;
        }
    }

    void handle_turn(DatagramReader &turn) {
        explosions.clear();
        destroyed_robots.clear();
        destroyed_blocks.clear();

        for (auto &bomb: bombs) {
            bomb.second.timer--;
        }

        container_size_t events_count;
        turn.read(current_turn)->read(events_count);
        for (container_size_t i = 0; i < events_count; i++) {
            message_id_t event_id;
            turn.read(event_id);
            switch (event_id) {
                case BOMB_PLACED:
                    handle_bomb_placed(turn);
                    break;
                case BOMB_EXPLODED:
                    handle_bomb_exploded(turn);
                    break;
                case PLAYER_MOVED:
                    handle_player_moved(turn);
                    break;
                case BLOCK_PLACED:
                    handle_block_placed(turn);
                    break;
                default:
                    cerr << "Wrong event id!" << endl;
                    exit(1);
            }
        }

        for (const auto &robot: destroyed_robots) {
            if (!scores.contains(robot)) scores[robot] = 0;
            scores[robot]++;
        }

        for (const auto &block: destroyed_blocks) {
            blocks.erase(block);
        }
    }

    void send(DatagramWriter &gui_handler) {
        gui_handler.clear();
        gui_handler.write(CG_GAME)
                ->write(game_info.server_info.server_name)
                ->write(game_info.server_info.size_x)
                ->write(game_info.server_info.size_y)
                ->write(game_info.server_info.game_length)
                ->write(current_turn)
                ->write(players)
                ->write(player_positions)
                ->write(blocks);

        gui_handler.write((container_size_t)bombs.size());
        for (const auto &bomb: bombs) {
            gui_handler.write(bomb.second);
        }

        gui_handler.write(explosions)
                ->write(scores)
                ->send();
    }

};

void handle_game_ended(DatagramReader &server_handler) {
    container_size_t size;
    server_handler.read(size);
    for (container_size_t i = 0; i < size; i++) {
        player_num_t player_id;
        score_t score;
        server_handler.read(player_id)->read(score);
    }
}

void from_server_to_gui(const command_parameters_t &cp) {
    DatagramWriter gui_handler(UDPClient::get_instance());
    DatagramReader server_handler(TCPClient::get_instance());

    hello_t hello = handle_hello(server_handler);
    game_state.set_state(StateType::IN_LOBBY);
    LobbyBuf lobby_buf(hello);

    lobby_buf.send(gui_handler);

    GameInfo game_info(hello);

    for (;;) {
        uint8_t message;
        server_handler.read(message);
        switch (message) {
            case SC_ACCEPTED_PLAYER:
                if (game_state.get_state() != StateType::IN_LOBBY) continue;
                lobby_buf.add_player(server_handler);
                lobby_buf.send(gui_handler);
                break;
            case SC_GAME_STARTED:
                if (game_state.get_state() != StateType::IN_LOBBY) continue;
                game_state.set_state(StateType::IN_GAME);
                game_info.set_gamers(server_handler);
                break;
            case SC_TURN:
                if (game_state.get_state() != StateType::IN_GAME) continue;
                game_info.handle_turn(server_handler);
                game_info.send(gui_handler);
                break;
            case SC_GAME_ENDED:
                if (game_state.get_state() != StateType::IN_GAME) continue;
                handle_game_ended(server_handler);
                game_state.set_state(StateType::IN_LOBBY);
                lobby_buf = LobbyBuf(hello);
                game_info = GameInfo(hello);
                break;
            default:
                cerr << "Wrong message from server" << endl;
                exit(1);
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

    UDPClient::init(cp.gui_address, cp.port);
    TCPClient::init(cp.server_address);

    boost::thread t1{from_gui_to_server, cp};
    boost::thread t2{from_server_to_gui, cp};
    t1.join();
    t2.join();
}
