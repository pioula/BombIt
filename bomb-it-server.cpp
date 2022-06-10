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

// Implementacja robots-server. Serwer jest zrealizowany wielowątkowo.
// Istnieje NUMBER_OF_CLIENTS serwerów odbierających i wysyłających bezmyślnie
// komunikaty do klienta. Odbierane komunikaty od klientów przesyłają do obiektu
// GameMaster, który je obsługuje. Komunikacja między serwerami, a game masterem
// jest zrealizowana przez kolejki blokujące. Każdy serwer i game master
// mają swoje kolejki.
namespace {
    // Maksymalna liczba podłączonych klientów.
    constexpr player_num_t NUMBER_OF_CLIENTS = 25;
    // Dodatkowy rodzaj wiadomości, mówiący o tym czy nowy klient został
    // podłączony do jakiegoś serwera, ale jeszcze nie przesłał
    // żadnego komunikatu.
    constexpr message_id_t RESET_SERVER = 255;

    // Wyjątek zwracany w wypadku podania zbyt dużej liczby graczy.
    struct TooManyClients : public std::exception {
        const char *what() const throw() {
            return "Too many clients!";
        }
    };

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
        variant<nullptr_t, hello_t, accepted_player_t, player_map_t,
            game_turn_t, scores_t> data;
    };
    using server_queue_t = BlockingQueue<server_message_t>;
    using  server_queue_list_t = array<server_queue_t, NUMBER_OF_CLIENTS>;

    // Szablon pomocniczy wykorzystywany w pattern matchingu.
    template<typename ... Ts>
    struct Overload : Ts ... {
        using Ts::operator() ...;
    };
    template<class... Ts> Overload(Ts...) -> Overload<Ts...>;

    enum GameState {
        GAME,
        LOBBY
    };

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
        command_parameters.seed = 0;  // Domyślny seed.
        bool with_help = false;

        vector<flag_t> flags{
            {"bomb-timer", "b", po::value<game_time_t>(), true, "<u16>",
                [&](po::variables_map &vm) {
                    command_parameters.bomb_timer =
                        vm["bomb-timer"].as<game_time_t>();
                }},
            {"players-count", "c", po::value<uint16_t>(), true, "<u8>",
                [&](po::variables_map &vm) {
                    // uint8_t jest interpretowany jako char, więc trzeba
                    // czytać uint16_t.
                    uint16_t arg = vm["players-count"].as<uint16_t>();
                    if (arg > UINT8_MAX)
                        throw TooManyClients();
                    command_parameters.players_count =
                        static_cast<player_num_t>(
                            vm["players-count"].as<uint16_t>());
                }},
            {"turn-duration", "d", po::value<turn_dur_t>(), true,
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
            {"help", "h", nullopt, false, "Wypisuje jak używać programu",
                [&](po::options_description &desc) {
                    cout << desc << endl;
                    with_help = true;
                }},
            {"initial-blocks", "k", po::value<block_count_t>(), true, "<u16>",
                [&](po::variables_map &vm) {
                    command_parameters.initial_blocks =
                        vm["initial-blocks"].as<block_count_t>();
                }},
            {"game-length", "l", po::value<game_time_t>(), true, "<u16>",
                [&](po::variables_map &vm) {
                    command_parameters.game_length =
                        vm["game-length"].as<game_time_t>();
                }},
            {"server-name", "n", po::value<string>(), true, "<String>",
                [&](po::variables_map &vm) {
                    command_parameters.server_name =
                            vm["server-name"].as<string>();
                }},
            {"port", "p", po::value<port_t>(), true, "<u16>",
                [&](po::variables_map &vm) {
                    command_parameters.port = vm["port"].as<port_t>();
                }},
            {"seed", "s", po::value<seed_t>(), false,
                "<u32, parametr opcjonalny>",
                [&](po::variables_map &vm) {
                    command_parameters.seed =
                        vm["seed"].as<seed_t>();
                }},
            {"size-x", "x", po::value<coords_t>(), true, "<u16>",
                [&](po::variables_map &vm) {
                    command_parameters.size_x =
                        vm["size-x"].as<coords_t>();
                }},
            {"size-y", "y", po::value<coords_t>(), true, "<u16>",
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

    // Funkcja odbierająca komunikaty od klienta i przesyłająca je do game
    // mastera.
    // - game_master_queue - kolejka na której słucha game master
    // - dr - reader od klienta
    // - server_id - id tego serwera
    // - client_address - adres klienta
    void receive_from_client(gm_queue_t &game_master_queue, DatagramReader &dr,
                             server_id_t server_id, address_t client_address) {
        game_master_message_t gm_mess;
        gm_mess.server_id = server_id;
        while (true) {
            message_id_t m;
            dr.read(m);
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

    // Funkcja wysyłająca komunikat hello do klienta.
    // - hello - komunikat do przesłania
    // - dw - writer do klienta
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

    // Funkcja wysyłająca komunikat accepted_player do klienta.
    // - player - komunikat do przesłania
    // - dw - writer do klienta
    void send_accepted_player(accepted_player_t &player, DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_ACCEPTED_PLAYER)
                ->write(player.id)
                ->write(player.player)
                ->send();
    }

    // Funkcja wysyłająca komunikat game_started do klienta.
    // - players - lista graczy biorących udział w grze
    // - dw - writer do klienta
    void send_game_started(player_map_t &players , DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_GAME_STARTED)
                ->write(players)
                ->send();
    }

    // Funkcja wysyłająca komunikat turn do klienta.
    // - turn - komunikat do przesłania
    // - dw - writer do klienta
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

    // Funkcja wysyłająca komunikat game_ended do klienta.
    // - scores - wyniki graczy po zakończonej grze
    // - dw - writer do klienta
    void send_game_ended(scores_t &scores , DatagramWriter &dw) {
        dw.clear();
        dw.write(SC_GAME_ENDED)
                ->write(scores)
                ->send();
    }

    // Funkcja wysyłająca komunikaty do klienta.
    // - dw - writer do klienta
    // - server_queue - kolejka na której nasłuchuje dany serwer.
    void send_to_client(DatagramWriter &dw, server_queue_t &server_queue) {
        while (true) {
            server_message_t m = server_queue.pop();
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

    // Funkcja obsługująca moment, gdy nowy klient jest podłączany
    // do serwera. Przekazuje informację game masterowi,, że pojawił się nowy
    // klient i czyści stare komunikaty z kolejki.
    // - gq - kolejka na której  nasłuchuje game master
    // - sq - kolejka mna której nasłuchuje dany wątek
    // - id - id serwera dla którego została wywołana funkcja
    void client_connect(gm_queue_t &gq, server_queue_t &sq,
                        const server_id_t id) {
        game_master_message_t reset_message;
        reset_message.server_id = id;
        reset_message.message = RESET_SERVER;
        // Przesyłany jest komunikat RESET_SERVER, a następnie jest oczekiwanie
        // na potwierdzenie od serwera w postaci tego samego komunikatu.
        gq.push(reset_message);
        while (true) {
            server_message_t m = sq.pop();
            if (m.id != RESET_SERVER)
                continue;
            break;
        }
    }

    // Klasa przedstawiająca gracza,, czyli klienta który wysłał pomyślnie
    // komunikat join.
    class Player {
    private:
        position_t position; // Pozycja gracza.
        accepted_player_t player_info;
        score_t score;
        // Akcja jaką wykonał gracz w tej turze.
        optional<player_action_t> action;

    public:
        Player(accepted_player_t &player) :
                player_info(player), score(0), action(nullopt) {}

        accepted_player_t get_player_info() const { return player_info; }
        position_t get_position() const { return position; }
        optional<player_action_t> get_action() const { return action; }
        score_t get_score() const { return score; }

        void set_position(const position_t p) {
            position = p;
        }

        void set_action(const optional<player_action_t> act) {
            action = act;
        }

        void inc_score() {
            score++;
        }
    };

    // Klasa opisująca game mastera, czyli klasę, której obiekt zarządza
    // całą grą.
    class GameMaster {
    private:
        boost::mutex mutex;
        boost::condition_variable for_game;

        // Ustawienia gry.
        const game_time_t bomb_timer;
        const player_num_t players_count;
        const turn_dur_t turn_duration;
        const explosion_radius_t explosion_radius;
        const block_count_t initial_blocks;
        const game_time_t game_length;
        name_t server_name;
        const coords_t x;
        const coords_t y;
        mutable minstd_rand random;

        // Obiekty wykorzystywane przy zarządzaniu stanem.
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
        // - position - współrzędne sprawdzanego pola.
        // return - wartość prawda/fałsz, czy podano prawidłowe współrzędne
        bool is_position_valid(const position_t position) const {
            return position.x < x && position.y < y;
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję bomby w danym
        // kierunku.
        // - scanner - pole na którym jest bomba (nie jest ono sprawdzane)
        // - shifter - funkcja przesuwająca skaner na odpowiednie miejsce
        // - explosions - struktura na którą są wrzucane zniszczone pola.
        void handle_explosion_stripe(position_t scanner,
                                     const function<void(position_t&)> &shifter,
                                     position_set &explosions) const {
            for (explosion_radius_t i = 0;
                 i < explosion_radius; i++) {
                shifter(scanner);
                if (!is_position_valid(scanner)) break;
                explosions.insert(scanner);
                if (blocks.contains(scanner)) break;
            }
        }

        // Metoda znajdująca wszystkie pola narażone na eksplozję danej bomby.
        // - bomb_position - miejsce w którym znajduje się bomba.
        // return - zniszczone pola
        position_set handle_explosions(const position_t &bomb_position) const {
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

        // Metoda zwracająca komunikat hello na podstawie informacji o serwerze.
        // return - hello
        hello_t create_hello() const {
            return {server_name, players_count, x, y, game_length,
                    explosion_radius, bomb_timer};
        }

        // Metoda zwracająca komunikat game_started.
        // return - game_started
        server_message_t create_game_started() const {
            server_message_t game_started{SC_GAME_STARTED, nullptr};
            player_map_t join_players;
            for (size_t i = 0; i < players.size(); i++) {
                join_players[static_cast<player_num_t>(i)]
                    = players[i].get_player_info().player;
            }
            game_started.data = join_players;
            return game_started;
        }

        // Metoda resetująca serwer, czyli odłączająca go od gracza,
        // jeżeli ten wysłał komunikat join, a następnie przesyłająca
        // nowe komunikaty dla nowego klienta.
        // - server_id - id serwera do zresetowania
        // - server_q - kolejka serwera o id server_id
        void reset_server(const server_id_t server_id,
                          server_queue_t &server_q) {
            server_message_t reset_message = {RESET_SERVER, nullptr};

            playing_servers.erase(server_id);

            server_q.push(reset_message);
            server_message_t hello_message{SC_HELLO, create_hello()};
            server_q.push(hello_message);
            if (game_state == LOBBY) {
                for (auto &player: players) {
                    server_message_t accepted_player_m
                        {SC_ACCEPTED_PLAYER,
                        player.get_player_info()};
                    server_q.push(accepted_player_m);
                }
            }
            else {
                server_message_t game_started = create_game_started();
                for (const auto &game_turn: game_turns) {
                    server_message_t game_turn_m{SC_TURN, game_turn};
                    server_q.push(game_turn_m);
                }
            }
        }

        // Metoda sprawdzająca, czy server_id obsługuje grającego klienta
        // - server_id - id serwera do sprawdzenia
        // return - true/false
        bool is_playing(const server_id_t server_id) const {
            return game_state == GAME && playing_servers.contains(server_id);
        }

        // Metoda obsługująca komunikat CS_PLACE_BOMB.
        // - server_id - id serwera, który odebrał komunikat.
        void handle_place_bomb(const server_id_t server_id) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(PlayerAction::PLACE_BOMB);
            }
        }

        // Metoda obsługująca komunikat CS_PLACE_BLOCK.
        // - server_id - id serwera, który odebrał komunikat.
        void handle_place_block(const server_id_t server_id) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(PlayerAction::PLACE_BLOCK);
            }
        }

        // Metoda obsługująca komunikat CS_MOVE.
        // - server_id - id serwera, który odebrał komunikat
        // - move_t - komunikat odebrany od klienta
        void handle_move(const server_id_t server_id, const move_t &move) {
            if (is_playing(server_id)) {
                players.at(playing_servers[server_id])
                        .set_action(move);
            }
        }

        // Metoda obsługująca komunikat CS_JOIN.
        // - queues - lista kolejek na których nasłuchują serwery
        // - join - komunikat odebrany od klienta
        // - id - id serwera, który odebrał komunikat
        void handle_join(server_queue_list_t &queues,
                         const server_join_t &join, const server_id_t id) {
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

        // Metoda obsługująca ruchy klienta podczas gry.
        // - id - id gracza, który wyykonał akcję.
        // - act - akcja gracza
        // - new_blocks - struktura na którą zostanie dodany blok, jeżeli
        //                  akcja to PLACE_BLOCK
        // - events - struktura na którą zostanie wrzucony event
        void handle_player_action(const player_num_t id,
                                  const player_action_t act,
                              position_set &new_blocks, event_list_t &events) {
            visit(Overload {
                [&](const PlayerAction &action) {
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
                [&](const move_t &move) {
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
            }, act);
        }

        position_t random_position() const {
            return {static_cast<coords_t>(random() % x),
                    static_cast<coords_t>(random() % y)};
        }

        // Metoda rozsyłająca nową turę do wszystkich serwerów.
        // - gt - aktualna tura
        // - queues - kolejki na których nasłuchują serwery.
        void send_next_turn(game_turn_t &gt, server_queue_list_t &queues) {
            server_message_t game_turn_m{SC_TURN, gt};
            for (auto &queue: queues)
                queue.push(game_turn_m);
        }

        // Metoda inicjująca grę, przesyłająca komunikat game_started do
        // serwerów oraz zerową turę.
        // - queues - kolejki na której nasłuchują serwery
        void start_game(server_queue_list_t &queues) {
            server_message_t game_started = create_game_started();
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
                position_t new_position = random_position();
                if (blocks.contains(new_position)) continue;
                blocks.insert(new_position);
                block_placed_t bp{new_position};
                turn.events.push_back(bp);
            }

            game_state = GAME;
            send_next_turn(turn, queues);
            game_turns.push_back(turn);
            for_game.notify_one(); // Budzenie wątku wykonującego make_turn().
        }

        // Metoda czyszcząca stan po zakończonej grze.
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

        // Metoda wysyłająca punktacje po zakończonej grze i czyszcząca stan.
        // - queues - kolejki na których nasłuchują serwery.
        void end_game(server_queue_list_t &queues) {
            scores_t scores;
            for (size_t i = 0; i < players.size(); i++) {
                scores[static_cast<player_num_t>(i)]
                    = players[i].get_score();
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

        // Metoda przeprowadzająca kolejną turę
        // - server_queues - lista kolejek na której nasłuchują serwery.
        void make_turn(server_queue_list_t &server_queues) {
            {
                boost::unique_lock<boost::mutex> lock(mutex);
                while (game_state != GAME)
                    for_game.wait(lock); // Czekanie na rozpoczęcie.
            }

            boost::this_thread::sleep_for(
                    boost::chrono::milliseconds(turn_duration));
            boost::unique_lock<boost::mutex> lock(mutex);
            // Zniszczone bloki w tej turze.
            position_set destroyed_blocks;
            // Zniszczone roboty w tej turze.
            unordered_set<player_num_t> destroyed_robots;
            // Bomby, które wybuchły w tej turze.
            unordered_set<bomb_id_t> exploded_bombs;
            // Bloki postawione przez graczy.
            position_set new_blocks;
            game_turn_t gm;
            gm.turn = current_turn++;

            // Obsługa bomb.
            for (auto &bomb: bombs) {
                bomb.second.timer--;
                if (bomb.second.timer == 0) {
                    bomb_exploded_t bomb_exploded_event;
                    bomb_exploded_event.bomb_id = bomb.first;
                    position_set explosions =
                            handle_explosions(bomb.second.position);

                    // Usuwanie bloków.
                    for (const auto &explosion: explosions) {
                        if (blocks.contains(explosion)) {
                            destroyed_blocks.insert(explosion);
                            bomb_exploded_event.blocks_destroyed
                                .insert(explosion);
                        }
                    }
                    // Niszczenie robotów.
                    for (size_t i = 0; i < players.size(); i++) {
                        if (explosions.contains(players[i].get_position())) {
                            destroyed_robots
                                .insert(static_cast<player_num_t>(i));
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

            // Obsługa akcji graczy.
            for (size_t i = 0; i < players.size(); i++) {
                if (destroyed_robots.contains(static_cast<player_num_t>(i))) {
                    players[i].set_position(random_position());
                    players[i].inc_score();
                    player_moved_t player_moved_event{
                        static_cast<player_num_t>(i),
                        players[i].get_position()};
                    gm.events.push_back(player_moved_event);
                }
                else {
                    if (players[i].get_action()) {
                        handle_player_action(static_cast<player_num_t>(i),
                                             *players[i].get_action(),
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

        // Metoda obsługująca komunikat odebrany od serwera.
        // - m - wiadomość od serwera
        // - queues - lista wszystkich kolejek na których nasłuchują serwery.
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

    // Funkcja obsługująca serwery komunikujące się z game masterem i klientami.
    // - cp - wczytane parametry programu
    void handle_servers(const command_parameters_t &cp) {
        as::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v6(), cp.port));
        boost::mutex mutex;
        vector<boost::thread> servers;
        // Kolejka na której nasłuchuje game master.
        gm_queue_t game_master_queue;
        // Kolejki na których nasłuchują serwery.
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

                            // Wątek czytający od klienta.
                            boost::thread receiver{[&]() {
                                try {
                                    receive_from_client(game_master_queue,
                                                        reader, id,
                                                        client_address.str());
                                }
                                catch (exception &err) {
                                    threads_still_running.decrease();
                                }
                            }};
                            // Wątek wysyłający do klienta
                            boost::thread sender{[&]() {
                                try {
                                    send_to_client(writer, server_queues[id]);
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
        // Wątek obsługujący kolejne tury gry.
        boost::thread clock{[&]() {
            while (true) {
                gm.make_turn(server_queues);
            }
        }};

        // Obsługa serwerów w gaame masterze
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
    catch (exception &exp) {
        cerr << exp.what() << endl;
        return 1;
    }

    try {
        handle_servers(cp);
    }
    catch (exception &err) {
        cerr << err.what() << endl;
        return 1;
    }
}