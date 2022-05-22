#ifndef UTILS_H
#define UTILS_H
#include <string>
#include <variant>
#include <optional>
#include <exception>
#include <functional>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/program_options.hpp>

#include <utils.h>


constexpr uint16_t UDP_DATAGRAM_SIZE = 65515;

using port_t = uint16_t;
using datagram_t = boost::array<char, UDP_DATAGRAM_SIZE>;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;
using boost::asio::ip::address;

namespace as = boost::asio;
namespace po = boost::program_options;

using values_t = std::variant<po::typed_value<std::string>*, po::typed_value<uint16_t>*>;

struct MissingFlag : public std::exception {
    const char *what() const throw() {
        return "Flag is missing! Type ./bomb-it-client --help";
    }
};

using flag_t = struct flag {
    std::string long_name;
    std::string short_name;
    std::optional<values_t> value_type;
    bool required;
    std::string description;
    std::function<void(po::variables_map&, po::options_description&)> handler;
};

using host_address_t = struct host_address {
    std::string host;
    std::string port;
};

const host_address_t INVALID_ADDRESS = {"", ""};

bool parse_command_line(int argc, char *argv[], std::vector<flag_t> &flags);

host_address_t parse_host_address(const std::string &host);

class UDPClient {
private:
    as::io_context io_context;
    udp::resolver resolver;
    udp::endpoint endpoint;
    udp::socket socket;
public:
    UDPClient(const host_address &address, const port_t port) :
            resolver(udp::resolver(io_context)),
            socket(udp::socket(io_context, udp::endpoint(udp::v6(), port))) {
        endpoint = *resolver.resolve(address.host, address.port).begin();
    }

    ~UDPClient() {
        socket.close();
    }

    size_t receive(datagram_t &arr) {
        return socket.receive(as::buffer(arr));
    }

    void send(const datagram_t &buffer) {
        socket.send_to(as::buffer(buffer), endpoint);
    }
};

class TCPClient {
private:
    as::io_context io_context;
    tcp::resolver resolver;
    tcp::resolver::results_type endpoints;
    tcp::socket socket;
public:
    TCPClient(const host_address &address) :
            resolver(tcp::resolver(io_context)),
            socket(tcp::socket(io_context)) {
        socket.open(tcp::v6());
        socket.set_option(tcp::no_delay(true));
        endpoints = resolver.resolve(address.host, address.port);
        as::connect(socket, endpoints);
    }

    ~TCPClient() {
        socket.close();
    }

    size_t receive(datagram_t &buffer) {
        return socket.read_some(as::buffer(buffer));
    }

    void send(datagram_t buffer) {
        socket.send(as::buffer(buffer));
    }
};

#endif // UTILS_H
