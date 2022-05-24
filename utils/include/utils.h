#ifndef UTILS_H
#define UTILS_H
#include <string>
#include <variant>
#include <optional>
#include <exception>
#include <functional>
#include <unordered_set>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/program_options.hpp>
#include <arpa/inet.h>


constexpr uint16_t UDP_DATAGRAM_SIZE = 65507;

using name_t = struct name_t {
    boost::array<char, 256> name;
    uint8_t len;
};

using port_t = uint16_t;
using coords_t = uint16_t;
using datagram_t = boost::array<char, UDP_DATAGRAM_SIZE>;
using bomb_id_t = uint32_t;
using game_time_t = uint16_t;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;
using boost::asio::ip::address;

namespace as = boost::asio;
namespace po = boost::program_options;

using values_t = std::variant<po::typed_value<std::string>*, po::typed_value<uint16_t>*>;

using position_t = struct position_t {
    coords_t x;
    coords_t y;
    bool operator==(const position_t& other) const {
        return x == other.x && y == other.y;
    };
};

class PositionHash {
public:
    size_t operator()(const position_t &p) const {
        return ((size_t)p.x << 16) | p.y;
    }
};

using position_set = std::unordered_set<position_t, PositionHash>;

using bomb_t = struct bomb_t {
    position_t position;
    game_time_t timer;
};

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


class DatagramHandler {
private:
    datagram_t buf{};
    size_t len;
    size_t read_ptr;
public:
    DatagramHandler() : len(0), read_ptr(0) {};
    DatagramHandler(const datagram_t &_buf, const size_t _len) :
            buf(_buf), len(_len) {}

    DatagramHandler* write(const std::string &str) {
        buf[len] = (uint8_t)str.length();
        len++;
        std::copy_n(str.begin(), str.length(), buf.begin() + len);
        len += str.length();
        return this;
    }

    DatagramHandler* write(const uint8_t n) {
        memcpy(buf.begin() + len, &n, sizeof(uint8_t));
        len += sizeof(uint8_t);
        return this;
    }

    DatagramHandler* write(const uint16_t n) {
        uint16_t net_n = htons(n);
        memcpy(buf.begin() + len, &net_n, sizeof(uint16_t));
        len += sizeof(uint16_t);
        return this;
    }

    DatagramHandler* write(const uint32_t n) {
        uint32_t net_n = htonl(n);
        memcpy(buf.begin() + len, &net_n, sizeof(uint32_t));
        len += sizeof(uint32_t);
        return this;
    }

    DatagramHandler* write(const datagram_t &dat, const size_t copy_len,
                           const size_t beg_ptr = 0) {
        std::copy_n(dat.begin() + beg_ptr, copy_len, buf.begin() + len);
        len += copy_len;
        return this;
    }

    DatagramHandler* write(const name_t &name) {
        std::copy_n(name.name.begin(), name.len, buf.begin() + len);
        len += name.len;
        return this;
    }

    DatagramHandler* write(const position_t &position) {
        return write(position.x)->write(position.y);
    }

    DatagramHandler* write(const bomb_t &bomb) {
        return write(bomb.position)->write(bomb.timer);
    }

    void reset_read() {
        read_ptr = 0;
    }

    DatagramHandler* read(position_t &position) {
        return read(position.x)->read(position.y);
    }

    DatagramHandler* read(name_t &name) {
        uint8_t str_len = buf[read_ptr];
        std::copy_n(buf.begin() + read_ptr, str_len, name.name.begin());
        name.len = str_len;
        read_ptr += 1 + str_len;
        return this;
    }

    DatagramHandler* read(std::string &str) {
        uint8_t str_len = buf[read_ptr];
        std::copy_n(buf.begin() + read_ptr, str_len, str.begin());
        read_ptr += 1 + str_len;
        return this;
    }

    DatagramHandler* read(uint8_t &n) {
        n = buf[read_ptr++];
        return this;
    }

    DatagramHandler* read(uint16_t &n) {
        memcpy(&n, buf.begin() + read_ptr, sizeof(uint16_t));
        n = ntohs(n);
        read_ptr += sizeof(uint16_t);
        return this;
    }

    DatagramHandler* read(uint32_t &n) {
        memcpy(&n, buf.begin() + read_ptr, sizeof(uint32_t));
        n = ntohl(n);
        read_ptr += sizeof(uint32_t);
        return this;
    }

    datagram_t* get_buf() {
        return &buf;
    }

    size_t get_len() {
        return len;
    }

    DatagramHandler concat(DatagramHandler &d) {
        DatagramHandler res;
        return *(res.write(buf, len)->write(*(d.get_buf()), d.get_len()));
    }
};

class UDPClient {
protected:
    as::io_context io_context;
    udp::resolver resolver;
    udp::endpoint endpoint;
    udp::socket socket;

    UDPClient(const host_address &address, const port_t &port) :
            resolver(udp::resolver(io_context)),
            socket(udp::socket(io_context, udp::endpoint(udp::v6(), port))) {
        endpoint = *resolver.resolve(address.host, address.port).begin();
    }

    static UDPClient* singleton;
public:
    UDPClient(UDPClient &other) = delete;

    void operator=(const UDPClient &) = delete;

    static UDPClient *get_instance(const host_address &address,
                                   const port_t &port);
    ~UDPClient() { //TODO ogarnączy ddziała czy nie
        socket.close();
    }

    size_t receive(datagram_t &arr) { //TODO exception safety
        return socket.receive(as::buffer(arr));
    }

    void send(DatagramHandler &buffer) {
        socket.send_to(as::buffer(*buffer.get_buf(), buffer.get_len()), endpoint);
    }
};

class TCPClient {
protected:
    as::io_context io_context;
    tcp::resolver resolver;
    tcp::resolver::results_type endpoints;
    tcp::socket socket;

    TCPClient(const host_address &address) :
            resolver(tcp::resolver(io_context)),
            socket(tcp::socket(io_context)) {
        socket.open(tcp::v6());
        socket.set_option(tcp::no_delay(true));
        endpoints = resolver.resolve(address.host, address.port);
        as::connect(socket, endpoints);
    }

    static TCPClient* singleton;
public:

    TCPClient(TCPClient &other) = delete;

    void operator=(const TCPClient &) = delete;

    static TCPClient *get_instance(const host_address &address);
    ~TCPClient() {
        socket.close();
    }

    size_t receive(datagram_t &buffer) {
        return socket.read_some(as::buffer(buffer));
    }

    void send(DatagramHandler &buffer) {
        socket.send(as::buffer(*buffer.get_buf(), buffer.get_len()));
    }

    void send(datagram_t &buffer, size_t len) {
        socket.send(as::buffer(buffer, len));
    }
};


#endif // UTILS_H
