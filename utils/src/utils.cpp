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

UDPClient *UDPClient::get_instance(const host_address &address, const port_t &port) {
    if (singleton == nullptr)
        singleton = new UDPClient(address, port);
    return singleton;
}

TCPClient* TCPClient::singleton = nullptr;

TCPClient *TCPClient::get_instance(const host_address &address) {
    if (singleton == nullptr)
        singleton = new TCPClient(address);
    return singleton;
}

void copy_string(datagram_t &buf, const std::string &str, size_t pos) {
    buf[pos] = (uint8_t)str.length();
    std::copy_n(str.begin(), str.length(), buf.begin() + pos + 1);
}

void parse_string(const datagram_t &buf, name_t &str, size_t &pos) {
    uint8_t len = buf[pos];
    std::copy_n(buf.begin() + 1 + pos, len, str.begin());
    pos += 1 + len;
}

void parse_u8(const datagram_t &buf, uint8_t &n, size_t &pos) {
    n = buf[pos++];
}

void parse_u16(const datagram_t &buf, uint16_t &n, size_t &pos) {
    memcpy(&n, buf.begin() + pos, sizeof(uint16_t));
    n = ntohs(n);
    pos += 2;
}
