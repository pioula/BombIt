#include <iostream>
#include <string>
#include <optional>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <exception>
#include <memory>
#include <array>
#include <queue>
#include <variant>
#include <random>
#include <sstream>

#include <boost/program_options.hpp>
#include <boost/thread.hpp>

#include "connection.h"
#include "message_types.h"
#include "command_parser.h"
#include "blocking_queue.h"
#include "latch.h"

using std::cout;
using std::copy_n;
using std::endl;
using std::string;
using std::optional;
using std::nullopt;
using std::function;
using std::vector;
using std::cerr;
using std::unordered_map;
using std::unordered_set;
using std::exception;
using std::vector;
using std::shared_ptr;
using std::make_shared;
using std::move;
using std::array;
using std::queue;
using std::static_pointer_cast;
using std::variant;
using std::holds_alternative;
using std::get;
using std::visit;
using std::minstd_rand;
using std::make_pair;

namespace po = boost::program_options;
namespace as = boost::asio;

namespace {

    template<typename ... Ts>
    struct Overload : Ts ... {
        using Ts::operator() ...;
    };
    template<class... Ts> Overload(Ts...) -> Overload<Ts...>;

    constexpr player_num_t NUMBER_OF_CLIENTS = 25;

    // Struktura przetrzymująca parametry przekazane podczas włączenia programu.
    using command_parameters_t = struct command_parameters {
        game_time_t bomb_timer;
        player_num_t players_count;
        turn_dur_t turn_duration;
        explosion_radius_t explosion_radius;
        block_count_t initial_blocks;
        game_time_t game_length;
        simple_name_t server_name;
        port_t port;
        seed_t seed;
        coords_t size_x;
        coords_t size_y;
    };

    // Funkcja przetwarzająca parametry przekazane podczas włączenia programu.
    // argc - liczba parametrów
    // argv - kolejne parametry
    // return - Jeżeli użyto flagi help to nullopt, w przeciwnym wypadku
    //          przetworzone parametry
    optional<command_parameters_t> parse_parameters(int argc, char *argv[]) {
        command_parameters_t command_parameters;
        bool with_help = false;

        vector<flag_t> flags{
                {"bomb-timer",       "b", po::value<game_time_t>(),        true,
                                                                                  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.bomb_timer =
                                    vm["bomb-timer"].as<game_time_t>();
                        }},
                {"players-count",    "c", po::value<uint16_t>(),       true,
                                                                                  "<u8>",
                        [&](po::variables_map &vm) {
                            command_parameters.players_count =
                                    static_cast<player_num_t>(vm["players-count"].as<uint16_t>());
                        }},
                {"turn-duration",    "d", po::value<turn_dur_t>(),         true,
                                                                                  "<u64, milisekundy>",
                        [&](po::variables_map &vm) {
                            command_parameters.turn_duration =
                                    vm["turn-duration"].as<turn_dur_t>();
                        }},
                {"explosion-radius", "e", po::value<explosion_radius_t>(), true,
                                                                                  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.explosion_radius =
                                    vm["explosion-radius"].as<explosion_radius_t>();
                        }},
                {"help",             "h", nullopt,                         false, "Wypisuje jak używać programu",
                        [&](po::options_description &desc) {
                            cout << desc << endl;
                            with_help = true;
                        }},
                {"initial-blocks",   "k", po::value<block_count_t>(),      true,  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.initial_blocks =
                                    vm["initial-blocks"].as<block_count_t>();
                        }},
                {"game-length",      "l", po::value<game_time_t>(),        true,  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.game_length =
                                    vm["game-length"].as<game_time_t>();
                        }},
                {"server-name",      "n", po::value<string>(),             true,  "<String>",
                        [&](po::variables_map &vm) {
                            command_parameters.server_name =
                                    vm["server-name"].as<string>();
                        }},
                {"port",             "p", po::value<port_t>(),             true,
                                                                                  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.port = vm["port"].as<port_t>();
                        }},
                {"seed",             "s", po::value<seed_t>(),             false,
                                                                                  "<u32, parametr opcjonalny>",
                        [&](po::variables_map &vm) {
                            command_parameters.seed =
                                    vm["seed"].as<seed_t>();
                        }},
                {"size-x",           "x", po::value<coords_t>(),           true,
                                                                                  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.size_x =
                                    vm["size-x"].as<coords_t>();
                        }},
                {"size-y",           "y", po::value<coords_t>(),           true,
                                                                                  "<u16>",
                        [&](po::variables_map &vm) {
                            command_parameters.size_y =
                                    vm["size-y"].as<coords_t>();
                        }}
        };

        parse_command_line(argc, argv, flags);

        if (!with_help)
            return command_parameters;
        else
            return nullopt;
    }


    constexpr message_id_t RESET_SERVER = 255;
    using address_t = string;
    using server_id_t = uint8_t;

    using server_join_t = struct {
        join_t join;
        name_t client_address;
    };

    using simple_message_t = uint8_t;
    using game_master_message_t = struct {
        server_id_t server_id;
        variant<server_join_t, move_t, simple_message_t> message;
    };
    using gm_queue_t = BlockingQueue<game_master_message_t>;

    using server_message_t = struct {
        message_id_t id;
        variant<nullptr_t, hello_t, accepted_player_t, player_map_t, game_turn_t,
                scores_t> data;
    };
    using server_queue_t = BlockingQueue<server_message_t>;
    using  server_queue_list_t = array<server_queue_t, NUMBER_OF_CLIENTS>;

    void receive_from_client(gm_queue_t &game_master_queue, DatagramReader &dr,
                             server_id_t server_id, address_t client_address) {
        game_master_message_t gm_mess;
        gm_mess.server_id = server_id;
        while (true) {
            message_id_t m;
            dr.read(m);
            cout << static_cast<uint16_t>(m) << " M" << endl;
            switch (m) {
                case CS_JOIN:
                    join_t join;
                    dr.read(join.name);
                    server_join_t server_join;
                    server_join.join = join;
                    server_join.client_address = string_to_name(client_address);
                    gm_mess.message = server_join;
                    game_master_queue.push(gm_mess);
                    break;
                case CS_PLACE_BOMB:
                case CS_PLACE_BLOCK:
                    gm_mess.message = m;
                    game_master_queue.push(gm_mess);
                    break;
                case CS_MOVE:
                    move_t move;
                    direction_t d;
                    dr.read(d);
                    if (d > MAX_DIRECTION)
                        throw exception();
                    move.direction = static_cast<Direction>(d);
                    gm_mess.message = move;
                    game_master_queue.push(gm_mess);
                    break;
                default:
                    throw exception();
            }
        }
    }

    void send_hello(hello_t &hello, DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_HELLO)
                ->write(hello.server_name)
                ->write(hello.players_count)
                ->write(hello.size_x)
                ->write(hello.size_y)
                ->write(hello.game_length)
                ->write(hello.explosion_radius)
                ->write(hello.bomb_timer)
                ->send();
    }

    void send_accepted_player(accepted_player_t &player, DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_ACCEPTED_PLAYER)
                ->write(player.id)
                ->write(player.player)
                ->send();
    }

    void send_game_started(player_map_t &players , DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_GAME_STARTED)
                ->write(players)
                ->send();
    }

    void send_turn(game_turn_t &turn, DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_TURN)
                ->write(turn.turn)
                ->write(static_cast<container_size_t>(turn.events.size()));
        for (auto &event : turn.events) {
            visit(Overload {
                    [&](bomb_placed_t &e) {
                        dw.write(BOMB_PLACED)
                                ->write(e.bomb_id)
                                ->write(e.position);
                    },
                    [&](bomb_exploded_t &e) {
                        dw.write(BOMB_EXPLODED)
                                ->write(e.bomb_id)
                                ->write(e.robots_destroyed)
                                ->write(e.blocks_destroyed);
                    },
                    [&](player_moved_t &e) {
                        dw.write(PLAYER_MOVED)
                                ->write(e.player_id)
                                ->write(e.position);
                    },
                    [&](block_placed_t &e) {
                        dw.write(BLOCK_PLACED)
                                ->write(e.position);
                    }
            }, event);
        }
        dw.send();
    }

    void send_game_ended(scores_t &scores , DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_GAME_ENDED)
                ->write(scores)
                ->send();
    }

    void send_to_client(gm_queue_t &game_master_queue, DatagramWriter &dw,
                        server_queue_t &server_queue) {
        while (true) {
            server_message_t m = server_queue.pop();
            cout << static_cast<uint32_t>(m.id) << " lol" << endl;
            switch (m.id) {
                case SC_HELLO:
                    send_hello(get<hello_t>(m.data), dw);
                    break;
                case SC_ACCEPTED_PLAYER:
                    send_accepted_player(get<accepted_player_t>(m.data), dw);
                    break;
                case SC_GAME_STARTED:
                    send_game_started(get<player_map_t>(m.data), dw);
                    break;
                case SC_TURN:
                    send_turn(get<game_turn_t>(m.data), dw);
                    break;
                case SC_GAME_ENDED:
                    send_game_ended(get<scores_t>(m.data), dw);
                    break;
                default:
                    continue;
            }
        }
    }

    void client_connect(gm_queue_t &gq, server_queue_t &sq, const server_id_t id) {
        game_master_message_t reset_message;
        reset_message.server_id = id;
        reset_message.message = RESET_SERVER;
        gq.push(reset_message);
        while (true) {
            server_message_t m = sq.pop();
            if (m.id != RESET_SERVER)
                continue;
            break;
        }
    }

    enum GameState {
        GAME,
        LOBBY
    };

    class Player {
    private:
        position_t position;
        optional<player_action_t> action;
        score_t score;
        accepted_player_t player_info;
    public:
        Player(accepted_player_t &player) :
                player_info(player), score(0), action(nullopt) {}

        accepted_player_t get_player_info() { return player_info; }
        position_t get_position() { return position; }
        optional<player_action_t> get_action() { return action; }

        void set_position(position_t p) {
            position = p;
        }

        void set_action(optional<player_action_t> act) {
            action = act;
        }

        void inc_score() {
            score++;
        }

        score_t get_score() {
            return score;
        }
    };

    class GameMaster {
    private:
        boost::mutex mutex;
        boost::condition_variable for_game;

        const game_time_t bomb_timer;
        const player_num_t players_count;
        const turn_dur_t turn_duration;
        const explosion_radius_t explosion_radius;
        const block_count_t initial_blocks;
        const game_time_t game_length;
        name_t server_name;
        const coords_t x;
        const coords_t y;
        minstd_rand random;

        GameState game_state;

        vector<game_turn_t> game_turns;
        unordered_map<server_id_t, player_num_t> playing_servers;
        vector<Player> players;
        position_set blocks;
        unordered_map<bomb_id_t, bomb_t> bombs;
        bomb_id_t bomb_count;
        turn_t current_turn;

        // Metoda sprawdzająca, czy dane współrzędne mogą się
        // znajdować na planszy.
        // position - współrzędne sprawdzanego pola.
        // return - wartość prawda/fałsz, czy podano prawidłowe współrzędne
        bool is_position_valid(const position_t position) const {
            return position.x < x && position.y < y;
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję bomby w danym
        // kierunku.
        // scanner - pole na którym jest bomba (nie jest ono sprawdzane)
        // shifter - funkcja przesuwająca skaner na odpowiednie miejsce
        void handle_explosion_stripe(position_t scanner,
                                     const function<void(position_t&)>
                                     &shifter, position_set &explosions) {
            for (explosion_radius_t i = 0;
                 i < explosion_radius; i++) {
                shifter(scanner);
                if (!is_position_valid(scanner)) break;
                explosions.insert(scanner);
                if (blocks.contains(scanner)) break;
            }
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję danej bomby.
        // bomb_position - miejsce w którym znajduje się bomba.
        position_set handle_explosions(const position_t &bomb_position) {
            position_set explosions;
            explosions.insert(bomb_position);
            if (!blocks.contains(bomb_position)) {
                handle_explosion_stripe(bomb_position,
                                        [](position_t &p){p.x++;}, explosions);
                handle_explosion_stripe(bomb_position,
                                        [](position_t &p){p.x--;}, explosions);
                handle_explosion_stripe(bomb_position,
                                        [](position_t &p){p.y++;}, explosions);
                handle_explosion_stripe(bomb_position,
                                        [](position_t &p){p.y--;}, explosions);
            }

            return explosions;
        }

        hello_t create_hello() {
            return {server_name, players_count, x, y, game_length, explosion_radius,
                    bomb_timer};
        }

        void reset_server(server_id_t server_id, server_queue_t &server_q) {
            server_message_t reset_message = {RESET_SERVER, nullptr};

            playing_servers.erase(server_id);

            server_q.push(reset_message);
            server_message_t hello_message{SC_HELLO, create_hello()};
            server_q.push(hello_message);
            if (game_state == LOBBY) {
                for (auto &player: players) {
                    server_message_t accepted_player_m{SC_ACCEPTED_PLAYER,
                                                       player.get_player_info()};
                    server_q.push(accepted_player_m);
                }
            }
            else {
                puts("Jestem");
                server_message_t game_started{SC_GAME_STARTED, nullptr};
                player_map_t join_players;
                for (size_t i = 0; i < players.size(); i++) {
                    join_players[i] = players[i].get_player_info().player;
                }
                game_started.data = join_players;
                server_q.push(game_started);
                for (const auto &game_turn: game_turns) {
                    server_message_t game_turn_m{SC_TURN, game_turn};
                    server_q.push(game_turn_m);
                }
            }
        }

        bool is_playing(server_id_t server_id) {
            return game_state == GAME && playing_servers.contains(server_id);
        }

        void handle_place_bomb(server_id_t server_id) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(PlayerAction::PLACE_BOMB);
            }
        }

        void handle_place_block(server_id_t server_id) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(PlayerAction::PLACE_BLOCK);
            }
        }

        void handle_move(server_id_t server_id, move_t &move) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(move);
            }
        }

        position_t random_position() {
            return {static_cast<coords_t>(random() % x),
                    static_cast<coords_t>(random() % y)};
        }

        void send_next_turn(game_turn_t &gt, server_queue_list_t &queues) {
            server_message_t game_turn_m{SC_TURN, gt};
            for (auto &queue: queues)
                queue.push(game_turn_m);
        }

        void start_game(server_queue_list_t &queues) {
            server_message_t game_started{SC_GAME_STARTED, nullptr};
            player_map_t join_players;
            for (size_t i = 0; i < players.size(); i++) {
                join_players[i] = players[i].get_player_info().player;
            }
            game_started.data = join_players;;
            game_turn_t turn;
            turn.turn = current_turn++;
            for (size_t i = 0; i < players.size(); i++) {
                position_t new_position = random_position();
                players[i].set_position(new_position);
                player_moved_t pm{static_cast<player_num_t>(i), new_position};
                turn.events.push_back(pm);
            }

            for (auto &queue: queues) {
                queue.push(game_started);
            }

            for (block_count_t i = 0; i < initial_blocks; i++) {
                while (true) {
                    position_t new_position = random_position();
                    if (blocks.contains(new_position)) continue;
                    blocks.insert(new_position);
                    block_placed_t bp{new_position};
                    turn.events.push_back(bp);
                    break;
                }
            }

            game_state = GAME;
            send_next_turn(turn, queues);
            game_turns.push_back(turn);
            for_game.notify_one();
        }

        void handle_join(server_queue_list_t &queues,
                         server_join_t &join, server_id_t id) {
            if (game_state == LOBBY && !playing_servers.contains(id)) {
                player_t player;
                player.name = join.join.name;
                player.address = join.client_address;
                playing_servers[id] = static_cast<player_num_t>(players.size());
                accepted_player_t accepted_player{
                        static_cast<player_num_t>(players.size()), player};
                players.emplace_back(accepted_player);

                server_message_t sm{SC_ACCEPTED_PLAYER, accepted_player};
                for (auto &queue: queues)
                    queue.push(sm);

                if (players.size() == players_count) {
                    start_game(queues);
                }
            }
        }

        void handle_player_action(player_num_t id, player_action_t acc,
                                  position_set &new_blocks, event_list_t &events) {
            visit(Overload {
                    [&](PlayerAction &action) {
                        switch (action) {
                            case PLACE_BLOCK:
                                new_blocks.insert(players[id].get_position());
                                events.push_back(
                                        block_placed_t{players[id].get_position()});
                                break;
                            case PLACE_BOMB:
                                events.push_back(
                                        bomb_placed_t{bomb_count,
                                                      players[id].get_position()});
                                bombs[bomb_count++] = {players[id].get_position(),
                                                       bomb_timer};
                                break;
                        }
                    },
                    [&](move_t &move) {
                        position_t np = players[id].get_position();
                        switch (move.direction) {
                            case UP:
                                np = {np.x, static_cast<coords_t>(np.y + 1)};
                                break;
                            case RIGHT:
                                np = {static_cast<coords_t>(np.x + 1), np.y};
                                break;
                            case DOWN:
                                np = {np.x, static_cast<coords_t>(np.y - 1)};
                                break;
                            case LEFT:
                                np = {static_cast<coords_t>(np.x - 1), np.y};
                                break;
                        }
                        if (is_position_valid(np) && !blocks.contains(np)) {
                            players[id].set_position(np);
                            events.push_back(player_moved_t{id, np});
                        }
                    }
            }, acc);
        }

        void clear_game_state() {
            game_state = LOBBY;
            game_turns.clear();
            playing_servers.clear();
            players.clear();
            blocks.clear();
            bombs.clear();
            bomb_count = 0;
            current_turn = 0;
        }

        void end_game(server_queue_list_t &queues) {
            scores_t scores;
            for (size_t i = 0; i < players.size(); i++) {
                scores[i] = players[i].get_score();
            }
            server_message_t sm{SC_GAME_ENDED, scores};
            for (auto &queue: queues) {
                queue.push(sm);
            }
            clear_game_state();
        }
    public:
        GameMaster(const command_parameters_t &cp) :
                bomb_timer(cp.bomb_timer),
                players_count(cp.players_count),
                turn_duration(cp.turn_duration),
                explosion_radius(cp.explosion_radius),
                initial_blocks(cp.initial_blocks),
                game_length(cp.game_length),
                server_name(string_to_name(cp.server_name)),
                x(cp.size_x),
                y(cp.size_y),
                random(cp.seed) {
            clear_game_state();
        }

        void make_turn(server_queue_list_t &server_queues) {
            {
                boost::unique_lock<boost::mutex> lock(mutex);
                while (game_state != GAME)
                    for_game.wait(lock);
            }

            boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(turn_duration));
            boost::unique_lock<boost::mutex> lock(mutex);
            position_set destroyed_blocks;
            unordered_set<player_num_t> destroyed_robots;
            unordered_set<bomb_id_t> exploded_bombs;
            game_turn_t gm;
            gm.turn = current_turn++;

            for (auto &bomb: bombs) {
                bomb.second.timer--;
                if (bomb.second.timer == 0) {
                    bomb_exploded_t bomb_exploded_event;
                    bomb_exploded_event.bomb_id = bomb.first;
                    position_set explosions =
                            handle_explosions(bomb.second.position);

                    for (const auto &explosion: explosions) {
                        if (blocks.contains(explosion)) {
                            destroyed_blocks.insert(explosion);
                            bomb_exploded_event.blocks_destroyed.insert(explosion);
                        }
                    }
                    for (size_t i = 0; i < players.size(); i++) {
                        if (explosions.contains(players[i].get_position())) {
                            destroyed_robots.insert(static_cast<player_num_t>(i));
                            bomb_exploded_event
                                    .robots_destroyed
                                    .insert(static_cast<player_num_t>(i));
                        }
                    }
                    gm.events.push_back(bomb_exploded_event);
                    exploded_bombs.insert(bomb.first);
                }
            }

            for (const auto &bomb: exploded_bombs) {
                bombs.erase(bomb);
            }

            for (const auto &block: destroyed_blocks) {
                blocks.erase(block);
            }

            position_set new_blocks;
            for (size_t i = 0; i < players.size(); i++) {
                if (destroyed_robots.contains(static_cast<player_num_t>(i))) {
                    players[i].set_position(random_position());
                    players[i].inc_score();
                    player_moved_t player_moved_event{static_cast<player_num_t>(i),
                                                      players[i].get_position()};
                    gm.events.push_back(player_moved_event);
                }
                else {
                    if (players[i].get_action()) {
                        handle_player_action(i, *players[i].get_action(),
                                             new_blocks, gm.events);
                    }
                }
                players[i].set_action(nullopt);
            }

            for (const auto &block: new_blocks) {
                blocks.insert(block);
            }

            send_next_turn(gm, server_queues);
            game_turns.push_back(gm);
            if (current_turn > game_length) {
                end_game(server_queues);
            }
        }

        void handle_server_message(game_master_message_t &m,
                                   server_queue_list_t &queues) {
            boost::unique_lock<boost::mutex> lock(mutex);
            visit(Overload {
                    [&](server_join_t &join) {
                        handle_join(queues, join, m.server_id);
                    },
                    [&](move_t &move) {
                        handle_move(m.server_id, move);
                    },
                    [&](simple_message_t &sm) {
                        switch (sm) {
                            case RESET_SERVER:
                                reset_server(m.server_id, queues[m.server_id]);
                                break;
                            case CS_PLACE_BOMB:
                                handle_place_bomb(m.server_id);
                                break;
                            case CS_PLACE_BLOCK:
                                handle_place_block(m.server_id);
                                break;
                        }
                    }
            }, m.message);
        }
    };

    void handle_servers(const command_parameters_t &cp) {
        as::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v6(), cp.port));
        boost::mutex mutex;
        vector<boost::thread> servers;
        gm_queue_t game_master_queue;
        server_queue_list_t server_queues;

        for (server_id_t i = 0; i < NUMBER_OF_CLIENTS; i++) {
            servers.emplace_back(
                    [&mutex, &io_context, &acceptor, &game_master_queue,
                            &server_queues]
                            (server_id_t id) {
                        while (true) {
                            mutex.lock();
                            tcp::socket socket(io_context);
                            acceptor.accept(socket);
                            mutex.unlock();

                            socket.set_option(tcp::no_delay(true));
                            std::ostringstream client_address;
                            client_address << socket.remote_endpoint();
                            TCPConnection con(socket);
                            DatagramWriter writer(&con);
                            DatagramReader reader(&con);

                            client_connect(game_master_queue,
                                           server_queues[id], id);

                            Latch threads_still_running(1);

                            boost::thread receiver{[&]() {
                                try {
                                    receive_from_client(game_master_queue, reader, id,
                                                        client_address.str());
                                }
                                catch (exception &err) {
                                    threads_still_running.decrease();
                                }
                            }};
                            boost::thread sender{[&]() {
                                try {
                                    send_to_client(game_master_queue, writer,
                                                   server_queues[id]);
                                }
                                catch (exception &err) {
                                    threads_still_running.decrease();
                                }
                            }};

                            threads_still_running.wait();
                            sender.interrupt();
                            receiver.interrupt();

                            socket.close();
                        }
                    }, i
            );
        }

        GameMaster gm(cp);
        boost::thread clock{[&]() {
            while (true) {
                gm.make_turn(server_queues);
            }
        }};

        while (true) {
            game_master_message_t m = game_master_queue.pop();
            gm.handle_server_message(m, server_queues);
        }
    }
}

int main(int argc, char *argv[]) {
    command_parameters_t cp;
    cp.seed = 0;
    try {
        if (auto cp_option = parse_parameters(argc, argv))
            cp = *cp_option;
        else
            return 0;
    }
    catch (MissingFlag &exp) {
        cerr << exp.what() << endl;
        return 1;
    }
    catch (exception &err) {
        cerr << err.what() << endl;
        return 1;
    }
    cout << "jajco" << endl;
    try {
        handle_servers(cp);
    }
    catch (exception &err) {
        cerr << err.what() << endl;
        return 1;
    }
}