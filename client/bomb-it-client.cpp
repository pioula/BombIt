#include <iostream>
#include <string>
#include <boost/program_options.hpp>
#include <optional>
#include <variant>
#include "err.h"

using std::cout;
using std::endl;
using std::string;
using std::optional;
using std::nullopt;
using std::variant;
using std::get;
using std::visit;

namespace po = boost::program_options;

namespace parser {
    const string ERROR_MESSAGE = "Missing flag! Type ./bomb-it-client -h for help\n";
    using values_t = variant<po::typed_value<string>*, po::typed_value<uint16_t>*>;

    struct flag {
        string long_name;
        string short_name;
        optional<values_t> value_type;
        string description;
    } flags[] = {
            { "gui-address", "d", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>" },
            { "help", "h", nullopt, "produce help message" },
            { "player-name", "n", po::value<string>(), "string" },
            { "port", "p", po::value<uint16_t>(), "Port na którym klient nasłuchuje komunikatów od GUI" },
            { "server-address", "s", po::value<string>(), "<(nazwa hosta):(port) lub (IPv4):(port) lub (IPv6):(port)>" }
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

        if (vm.count("help")) {
            cout << desc << "\n";
            return true;
        }

        if (vm.count("gui-address")) {
            cout << "gui" << "\n";
        }
        else {
            cout << ERROR_MESSAGE;
        }

        if (vm.count("player-name")) {
            cout << "player-name" << "\n";
        }
        else {
            cout << ERROR_MESSAGE;
        }

        if (vm.count("port")) {
            cout << "port" << "\n";
        }
        else {
            cout << ERROR_MESSAGE;
        }

        return true;
    }
}

int main(int argc, char *argv[]) {
    puts("Hello!");
    parser::parse_command_line(argc, argv);
    cout << "End!" << endl;
}
