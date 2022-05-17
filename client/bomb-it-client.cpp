#include <iostream>
#include <string>
#include <optional>
#include <variant>
#include <functional>
#include <vector>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/program_options.hpp>

#include <utils.h>

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

using boost::asio::ip::tcp;

namespace as = boost::asio;
namespace po = boost::program_options;

namespace network {
    using host_address = struct host_address {
        string host;
        string port;
    };

    const host_address INVALID_ADDRESS = {"", ""};

    host_address parse_host_address(string &host) {
        if (auto port_pos = host.find_last_of(':') + 1) {
            return {host.substr(0, port_pos - 1), host.substr(port_pos)};
        }
        else {
            return INVALID_ADDRESS;
        }
    }
}

int main(int argc, char *argv[]) {
    puts("Hello!");
    int jajco = 0;
    vector<flag_t> flags{
        { "help", "h", nullopt, "produce help message",
            [&](auto &vm, auto &desc) {
                cout << desc << "\n";
                jajco = 5;
            }}, // Help must be first in array flags
        { "gui-address", "d", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
            [](auto &vm, auto &desc) {
                cout << "gui\n";
                network::host_address addr = {"a", "b"};
            }},
        { "player-name", "n", po::value<string>(), "string",
            [](auto &vm, auto &desc) {
                cout << "player\n";
            }},
        { "port", "p", po::value<uint16_t>(), "Port na którym klient nasłuchuje komunikatów od GUI",
            [](auto &vm, auto &desc) {
                cout << "port\n";
            }},
        { "server-address", "s", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>",
            [](auto &vm, auto &desc) {
                cout << "server\n";
            }}
    };

    try {
        parse_command_line(argc, argv, flags);
    }
    catch(MissingFlag &exp) {
        cout << exp.what();
        return 1;
    }

    cout << jajco << "\n";

//    try {
//        as::io_context io_context;
//
//        tcp::resolver resolver(io_context);
//        auto endpoints = resolver.resolve("localhost", "2137");
//
//        tcp::socket socket(io_context);
//        as::connect(socket, endpoints);;
//
//        for (;;) {
//            boost::array<char, 128> buf;
//            boost::system::error_code error;
//
//            size_t len = socket.read_some(as::buffer(buf), error);
//            if (error == as::error::eof)
//                break;
//            else if (error)
//                throw boost::system::system_error(error);
//
//            cout.write(buf.data(), len);
//        }
//    }
//    catch (exception& e) {
//        std::cerr << e.what() << std::endl;
//    }
//
//    cout << "End!" << endl;
}
