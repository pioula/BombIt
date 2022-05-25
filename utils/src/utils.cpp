#include <utils.h>
#include <iostream>
#include <cstring>

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
using std::memcpy;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace as = boost::asio;
namespace po = boost::program_options;

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
            flag.handler(vm, desc);
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

UDPClient* UDPClient::singleton = nullptr;

void UDPClient::init(const host_address &address, const port_t &port) {
    if (singleton == nullptr)
        singleton = new UDPClient(address, port);

}

UDPClient *UDPClient::get_instance() {
    return singleton;
}

TCPClient* TCPClient::singleton = nullptr;

void TCPClient::init(const host_address &address) {
    if (singleton == nullptr)
        singleton = new TCPClient(address);
}

TCPClient *TCPClient::get_instance() {
    return singleton;
}