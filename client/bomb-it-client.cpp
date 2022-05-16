#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <optional>
#include <variant>
#include <functional>

#include <boost/asio.hpp>
#include <boost/array.hpp>

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

using boost::asio::ip::tcp;

namespace as = boost::asio;
namespace po = boost::program_options;

namespace parser {
    using values_t = variant<po::typed_value<string>*, po::typed_value<uint16_t>*>;

    struct MissingFlag : public exception {
        const char *what() const throw() {
            return "Flag is missing! Type ./bomb-it-client --help";
        }
    };

    struct flag {
        string long_name;
        string short_name;
        optional<values_t> value_type;
        string description;
        function<void(po::variables_map&, po::options_description&)> handler;
    } flags[] = {
            { "help", "h", nullopt, "produce help message", [](auto &vm, auto &desc){ cout << desc << "\n"; } }, // Help must be first in array flags
            { "gui-address", "d", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>", [](auto &vm, auto &desc){ cout << "gui\n"; } },
            { "player-name", "n", po::value<string>(), "string", [](auto &vm, auto &desc){ cout << "player\n"; }},
            { "port", "p", po::value<uint16_t>(), "Port na którym klient nasłuchuje komunikatów od GUI", [](auto &vm, auto &desc){ cout << "port\n"; } },
            { "server-address", "s", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>", [](auto &vm, auto &desc){ cout << "server\n"; } }
    };

    bool parse_command_line(int argc, char *argv[]) {
        po::options_description desc("Allowed options");
        for (const auto &flag: flags) {
            string flag_names = flag.long_name;
            flag_names.append(",").append(flag.short_name);

            if (auto value_type = flag.value_type) {
                string w;
                visit([&](auto value){
                    desc.add_options()
                            (flag_names.c_str(), value, flag.description.c_str());
                }, *value_type);
            }
            else {
                desc.add_options()
                        (flag_names.c_str(), flag.description.c_str());
            }
        }

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        for (const auto &flag: flags) {
            if (vm.count(flag.long_name)) {
                flag.handler(vm, desc);
                if (flag.long_name ==  "help")
                    return true;
            }
            else {
                if (flag.long_name != "help")
                    throw MissingFlag();
            }
        }

        return true;
    }
}

int main(int argc, char *argv[]) {
    puts("Hello!");
//    try {
//        parser::parse_command_line(argc, argv);
//    }
//    catch(parser::MissingFlag &exp) {
//        cout << exp.what();
//        return 1;
//    }

    try {
        as::io_context io_context;

        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve("localhost", "2137");

        tcp::socket socket(io_context);
        as::connect(socket, endpoints);;

        for (;;) {
            boost::array<char, 128> buf;
            boost::system::error_code error;

            size_t len = socket.read_some(as::buffer(buf), error);
            if (error == as::error::eof)
                break;
            else if (error)
                throw boost::system::system_error(error);

            cout.write(buf.data(), len);
        }
    }
    catch (exception& e) {
        std::cerr << e.what() << std::endl;
    }

    cout << "End!" << endl;
}
