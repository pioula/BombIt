#include "command_parser.h"

#include <vector>
#include <string>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

using std::string;
using std::vector;
using std::holds_alternative;
using std::get;

bool parse_command_line(int argc, char *argv[], vector<flag_t> &flags) {
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
            if (holds_alternative<no_param_handler_t>(flag.handler)) {
                get<no_param_handler_t>(flag.handler)(desc);
            }
            else {
                get<param_handler_t>(flag.handler)(vm);
            }
        }
        else if (flag.required) {
            throw MissingFlag();
        }
    }

    return true;
}

host_address_t parse_host_address(const string &host) {
    if (auto port_pos = host.find_last_of(':') + 1) {
        return {host.substr(0, port_pos - 1), host.substr(port_pos)};
    }
    else {
        return INVALID_ADDRESS;
    }
}