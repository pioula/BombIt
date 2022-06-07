#include <iostream>
#include <string>
#include <optional>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <exception>

#include <boost/program_options.hpp>
#include <boost/thread.hpp>

#include "connection.h"
#include "message_types.h"
#include "command_parser.h"

using std::cout;
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

namespace po = boost::program_options;

namespace {
    // Enumerator wskazujący na stan gracza.
    enum StateType {
        IDLE,   // Stan w którym gracz czeka na Hello od serwera.
        IN_LOBBY,
        IN_GAME,
    };

    // Klasa przechowująca stan gracza.
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

    GameState game_state{}; // Globalny stan gracza.

    // Struktura przetrzymująca parametry przekazane podczas włączenia programu.
    using command_parameters_t = struct command_parameters {
        host_address_t gui_address;
        simple_name_t player_name;
        port_t port;
        host_address_t server_address;
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
            { "gui-address", "d", po::value<string>(), true,
                "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
                [&](po::variables_map &vm) {
                    command_parameters.gui_address =
                        parse_host_address(vm["gui-address"].as<string>());
                }},
            { "help", "h", nullopt, false, "Wypisuje jak używać programu",
                [&](po::options_description &desc) {
                    cout << desc << endl;
                    with_help = true;
                }},
            { "player-name", "n", po::value<simple_name_t>(), true, "",
                [&](po::variables_map &vm) {
                    command_parameters.player_name =
                        vm["player-name"].as<simple_name_t>();
                }},
            { "port", "p", po::value<port_t>(), true,
                "Port na którym klient nasłuchuje komunikatów od GUI",
                [&](po::variables_map &vm) {
                    command_parameters.port = vm["port"].as<port_t>();
                }},
            { "server-address", "s", po::value<string>(), true,
                "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
                [&](po::variables_map &vm) {
                    command_parameters.server_address =
                        parse_host_address(vm["server-address"].as<string>());
                }}
        };

        parse_command_line(argc, argv, flags);

        if (!with_help)
            return command_parameters;
        else
            return nullopt;
    }

    // Funkcja wysyłająca komunikat JOIN do serwera.
    // player_name - imię gracza, które zostało podane podczas
    //               uruchomienia programu
    void send_join(const string &player_name) {
        DatagramWriter buf(TCPClient::get_instance());
        message_id_t tmp = CS_JOIN;
        buf.write(tmp)->write(player_name)->send();
    }

    // Funkcja sprawdzająca poprawność komunikatu wysłanego od gui do klienta.
    // gui_buf - datagram wysłany przez gui do klienta
    // return - wartość prawda/fałsz, czy komunikat jest poprawny
    bool validate_gui_message(datagram_t gui_buf) {
        if (gui_buf.len == 0) return false;
        message_id_t message = static_cast<message_id_t>(gui_buf.buf[0]);
        switch (message) {
            case GC_PLACE_BOMB:
                return gui_buf.len == GC_PLACE_BOMB_LENGTH;
            case GC_PLACE_BLOCK:
                return gui_buf.len == GC_PLACE_BLOCK_LENGTH;
            case GC_MOVE:
                return gui_buf.len == GC_MOVE_LENGTH &&
                       gui_buf.buf[1] <= MAX_DIRECTION;
            default:
                return false;
        }
    }

    // Funkcja odbierająca komunikaty od gui, przetwarzająca je i wysyłająca
    // odpowiednie komunikaty do serwera.
    // player_name - imię gracza, które zostało podane
    //               podczas uruchomienia programu
    [[noreturn]] void from_gui_to_server(const string &player_name) {
        UDPClient* gui_handler = UDPClient::get_instance();
        TCPClient* server_handler = TCPClient::get_instance();

        datagram_t gui_buf;
        for (;;) {
            gui_handler->read_some(gui_buf);
            if (!validate_gui_message(gui_buf)) continue;
            if (game_state.get_state() == StateType::IN_LOBBY) {
                send_join(player_name);
            }
            else if (game_state.get_state() == StateType::IN_GAME) {
                // Wszystkie poprawne komunikaty od gui po zwiększeniu
                // ich id o 1 są odpowiadającymi, poprawnymi komunikatami
                // wysyłanymi od klienta do serwera.
                gui_buf.buf[0]++;
                server_handler->send(gui_buf);
            }
        }
    }

    // Funkcja przetwarzająca komunikat HELLO.
    // server_handler - reader komunikatów od serwera
    // return - struktura zawierająca informację z komunikatu hello.
    hello_t handle_hello(DatagramReader &server_handler) {
        hello_t res;
        message_id_t m;
        server_handler.read(m)
                ->read(res.server_name)
                ->read(res.players_count)
                ->read(res.size_x)
                ->read(res.size_y)
                ->read(res.game_length)
                ->read(res.explosion_radius)
                ->read(res.bomb_timer);

        return res;
    }

    // Klasa prrzetrzymująca informację o lobby gry.
    // Ma możliwość wysłania LOBBY ich do gui.
    class LobbyHandler {
    private:
        hello_t hello;  // Komunikat HELLO otrzymany od serwera.
        // Mapa graczy przesłanych komunikatem ACCEPTED_PLAYER.
        player_map_t player_map{};
    public:
        LobbyHandler(const hello_t &_hello) : hello(_hello) {}

        // Metoda czytająca komunikat ACCEPTED_PLAYER i
        // aktualizująca mapę graczy.
        // accepted_player - reader serwera
        void add_player(DatagramReader &accepted_player) {
            player_num_t player_id;
            player_t player;
            accepted_player.read(player_id);
            accepted_player.read(player);
            player_map[player_id] = player;
        }

        // Metoda wysyłająca komunikat LOBBY do gui.
        // gui_handler - writer do gui
        void send(DatagramWriter &gui_handler) const {
            gui_handler.clear();
            gui_handler.write(CG_LOBBY)
                    ->write(hello.server_name)
                    ->write(hello.players_count)
                    ->write(hello.size_x)
                    ->write(hello.size_y)
                    ->write(hello.game_length)
                    ->write(hello.explosion_radius)
                    ->write(hello.bomb_timer)
                    ->write(player_map)
                    ->send();
        }
    };

    // Klasa przetrzymująca informację o grze, oraz aktualizująca jej stan.
    // Ma możliwość wysłania GAME do gui.
    class GameHandler {
    private:
        hello_t                                 game_info;
        turn_t                                  current_turn;
        player_map_t                            players{};
        unordered_map<player_num_t, position_t> player_positions{};
        position_set                            blocks{};
        unordered_map<bomb_id_t, bomb_t>        bombs{};
        position_set                            explosions{};
        scores_t                                scores{};

        // Pomocnicza struktura przetrzymująca zniszczone roboty w danej turze.
        unordered_set<player_num_t>             destroyed_robots{};
        // Pomocnicza struktura przetrzymująca zniszczone bloki w danej turze.
        position_set                            destroyed_blocks{};

        // Metoda wczytująca informacje o nowej
        // bombie od serwera i aktualizująca stan.
        // turn - reader od serwera
        void handle_bomb_placed(DatagramReader &turn) {
            bomb_id_t bomb_id;
            bomb_t new_bomb;
            new_bomb.timer = game_info.bomb_timer;
            turn.read(bomb_id)->read(new_bomb.position);
            bombs[bomb_id] = new_bomb;
        }

        // Metoda sprawdzająca, czy dane współrzędne mogą się
        // znajdować na planszy.
        // position - współrzędne sprawdzanego pola.
        // return - wartość prawda/fałsz, czy podano prawidłowe współrzędne
        bool is_position_valid(const position_t &position) const {
            return position.x < game_info.size_x && position.y < game_info.size_y;
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję bomby w danym
        // kierunku.
        // scanner - pole na którym jest bomba (nie jest ono sprawdzane)
        // shifter - funkcja przesuwająca skaner na odpowiednie miejsce
        void handle_explosion_stripe(position_t scanner,
                                     const function<void(position_t&)>
                                             &shifter) {
            for (explosion_radius_t i = 0;
                i < game_info.explosion_radius; i++) {
                shifter(scanner);
                if (!is_position_valid(scanner)) break;
                explosions.insert(scanner);
                if (blocks.contains(scanner)) break;
            }
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję danej bomby.
        // bomb_position - miejsce w którym znajduje się bomba.
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

        // Metoda obsługująca zdarzenie BOMB_EXPLODED.
        // turn - reader od serwera
        void handle_bomb_exploded(DatagramReader &turn) {
            bomb_id_t bomb_id;
            turn.read(bomb_id);

            position_t bomb_position = bombs[bomb_id].position;
            bombs.erase(bomb_id);

            container_size_t robots_destroyed;
            turn.read(robots_destroyed);

            // Aktualizuje listę zniszczonych robotów w tej turze.
            for (container_size_t i = 0; i < robots_destroyed; i++) {
                player_num_t player_id;
                turn.read(player_id);
                destroyed_robots.insert(player_id);
            }

            // Znajduje wszystkie pola narażone na eksplozję.
            handle_explosions(bomb_position);

            container_size_t block_count;
            turn.read(block_count);

            // Znajduje wszystkie zniszczone bloki.
            for (container_size_t i = 0; i < block_count; i++) {
                position_t position;
                turn.read(position);
                destroyed_blocks.insert(position);
            }
        }

        // Metoda obsługująca zdarzenie PLAYER_MOVED.
        // turn - reader od serwera
        void handle_player_moved(DatagramReader &turn) {
            player_num_t id;
            position_t pos;
            turn.read(id)->read(pos);
            player_positions[id] = pos;
        }

        // Metoda obsługująca zdarzenie BLOCK_PLACED.
        // turn - reader od serwera
        void handle_block_placed(DatagramReader &turn) {
            position_t pos;
            turn.read(pos);
            blocks.insert(pos);
        }

    public:
        // Konstruktor przyjmujący przetworzony komunikat HELLO.
        GameHandler(const hello_t _game_info) :
                game_info(_game_info) {};

        // Metoda obsługująca komunikat GAME_STARTED.
        // gamers - reader od serwera
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

        // Metoda oobsługująca komunikat TURN.
        // turn - reader od serwera.
        void handle_turn(DatagramReader &turn) {
            // Eksplozje trwają jedną turę, więc należy je wyczyścić.
            explosions.clear();
            // Czyszczę struktury pomocniczę.
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

        // Metoda wysyłająca komunikat GAME do gui.
        // gui_handler - writer do gui.
        void send(DatagramWriter &gui_handler) const {
            gui_handler.clear();
            gui_handler.write(CG_GAME)
                    ->write(game_info.server_name)
                    ->write(game_info.size_x)
                    ->write(game_info.size_y)
                    ->write(game_info.game_length)
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

// Funkcja obsługująca komunikat GAME_ENDED.
// server_handler - reader od serwera
    void handle_game_ended(DatagramReader &server_handler) {
        container_size_t size;
        server_handler.read(size);
        for (container_size_t i = 0; i < size; i++) {
            player_num_t player_id;
            score_t score;
            server_handler.read(player_id)->read(score);
        }
    }

// Funkcja odbierająca komunikaty od serwera, przetwarzająca je i wysyłająca
// odpowiednie komunikaty do gui.
    void from_server_to_gui() {
        DatagramWriter gui_handler(UDPClient::get_instance());
        DatagramReader server_handler(TCPClient::get_instance());

        hello_t hello = handle_hello(server_handler);
        game_state.set_state(StateType::IN_LOBBY);
        LobbyHandler lobby_buf(hello);

        lobby_buf.send(gui_handler);

        GameHandler game_info(hello);

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
                    lobby_buf = LobbyHandler(hello);
                    game_info = GameHandler(hello);
                    lobby_buf.send(gui_handler);
                    break;
                default:
                    cerr << "Wrong message from server" << endl;
                    exit(1);
            }
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
        cerr << exp.what() << endl;
        return 1;
    }
    catch (exception &err) {
        cerr << err.what() << endl;
        return 1;
    }

    UDPClient::init(cp.gui_address, cp.port);
    TCPClient::init(cp.server_address);

    boost::thread t1{from_gui_to_server, cp.player_name};
    boost::thread t2{from_server_to_gui};
    t1.join();
    t2.join();
}