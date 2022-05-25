#ifndef UTILS_H
#define UTILS_H
#include <string>
#include <variant>
#include <optional>
#include <exception>
#include <functional>
#include <unordered_set>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/program_options.hpp>
#include <arpa/inet.h>

constexpr uint16_t DATAGRAM_SIZE = 65507;

using name_t = struct name_t {
    boost::array<char, 256> name;
    uint8_t len;
};

using port_t = uint16_t;
using coords_t = uint16_t;
using container_size_t = uint32_t;
using data_t = boost::array<char, DATAGRAM_SIZE>;
using datagram_t = struct datagram_t {
    data_t buf;
    uint16_t len;
};
using bomb_id_t = uint32_t;
using game_time_t = uint16_t;
using flex_buf_t = std::vector<char>;
using player_t = struct player_t {
    name_t name;
    name_t address;
};

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

class Client {
public:
    virtual void read_some(datagram_t &data) = 0;
    virtual void send(datagram_t &data) = 0;
    virtual void close() = 0;
};

class UDPClient : public Client {
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

    static void init(const host_address &address,
                                   const port_t &port);
    static UDPClient *get_instance();

    ~UDPClient() { //TODO ogarnączy ddziała czy nie
        socket.close();
    }

    void read_some(datagram_t &data) { //TODO exception safety
         data.len = socket.receive(as::buffer(data.buf));
    }

    void send(datagram_t &data) {
        socket.send_to(as::buffer(data.buf, data.len), endpoint);
    }

    void close() override {
        socket.close();
    }
};

class TCPClient : public Client {
protected:
    as::io_context io_context;
    tcp::resolver resolver;
    tcp::resolver::results_type endpoints;
    tcp::socket socket;

    TCPClient(const host_address &address) :
            resolver(tcp::resolver(io_context)),
            socket(tcp::socket(io_context)) {
        endpoints = resolver.resolve(address.host, address.port);
        as::connect(socket, endpoints);
        socket.set_option(tcp::no_delay(true));
    }

    static TCPClient* singleton;
public:

    TCPClient(TCPClient &other) = delete;

    void operator=(const TCPClient &) = delete;

    static void init(const host_address &address);
    static TCPClient *get_instance();

    ~TCPClient() {
        socket.close();
    }

    void read_some(datagram_t &data) override { //TODO exception safety
        data.len = socket.read_some(as::buffer(data.buf));
    }

    void send(datagram_t &data) override {
        socket.send(as::buffer(data.buf, data.len));
    }

    void close() override {
        socket.close();
    }
};


class DatagramReader {
private:
    Client* client;
    datagram_t data{};
    size_t read_ptr;

    flex_buf_t prepare_buf(size_t bytes) {
        flex_buf_t res{};
        for (size_t i = 0; i < bytes; i++) {
            if (read_ptr >= data.len) {
                client->read_some(data);
                read_ptr = 0;
                i--;
                continue;
            }
            res.push_back(data.buf[read_ptr++]);
        }
        return res;
    }

public:
    explicit DatagramReader(Client* client) : client(client), read_ptr(0) {
        data.buf = {};
        data.len = 0;
    };

    DatagramReader* read(player_t &player) {
        return read(player.name)->read(player.address);
    }

    DatagramReader* read(position_t &position) {
        return read(position.x)->read(position.y);
    }

    DatagramReader* read(name_t &name) {
        uint8_t str_len;
        read(str_len);
        flex_buf_t buf = prepare_buf(str_len);
        std::copy_n(buf.begin(), str_len, name.name.begin());
        name.len = str_len;
        return this;
    }

    DatagramReader* read(uint8_t &n) {
        flex_buf_t buf = prepare_buf(sizeof(uint8_t));
        n = buf[0];
        return this;
    }

    DatagramReader* read(uint16_t &n) {
        flex_buf_t buf = prepare_buf(sizeof(uint16_t));
        memcpy(&n, buf.data(), sizeof(uint16_t));
        n = ntohs(n);
        return this;
    }

    DatagramReader* read(uint32_t &n) {
        flex_buf_t buf = prepare_buf(sizeof(uint32_t));
        memcpy(&n, buf.data(), sizeof(uint32_t));
        n = ntohl(n);
        return this;
    }
};

class DatagramWriter {
private:
    Client* client;
    datagram_t data;

    void prepare_buf(size_t bytes) {
        if (data.len + bytes > DATAGRAM_SIZE) {
            client->send(data);
            data.len = 0;
        }
    }
public:
    explicit DatagramWriter(Client* _client) : client(_client) {
        data.buf = {};
        data.len = 0;
    };

    void clear() {
        data.len = 0;
    }

    DatagramWriter* write(const player_t &player) {
        return write(player.name)->write(player.address);
    }

    DatagramWriter* write(const std::string &str) {
        write((uint8_t)str.length());
        prepare_buf(str.length());
        std::copy_n(str.begin(), str.length(), data.buf.begin() + data.len);
        data.len += str.length();
        return this;
    }

    DatagramWriter* write(const uint8_t n) {
        prepare_buf(sizeof(uint8_t));
        memcpy(data.buf.begin() + data.len, &n, sizeof(uint8_t));
        data.len += sizeof(uint8_t);
        return this;
    }

    DatagramWriter* write(const uint16_t n) {
        prepare_buf(sizeof(uint16_t));
        uint16_t net_n = htons(n);
        memcpy(data.buf.begin() + data.len, &net_n, sizeof(uint16_t));
        data.len += sizeof(uint16_t);
        return this;
    }

    DatagramWriter* write(const uint32_t n) {
        prepare_buf(sizeof(uint32_t));
        uint32_t net_n = htonl(n);
        memcpy(data.buf.begin() + data.len, &net_n, sizeof(uint32_t));
        data.len += sizeof(uint32_t);
        return this;
    }

    DatagramWriter* write(const name_t &name) {
        write(name.len);
        prepare_buf(name.len);
        std::copy_n(name.name.begin(), name.len, data.buf.begin() + data.len);
        data.len += name.len;
        return this;
    }

    DatagramWriter* write(const position_t &position) {
        return write(position.x)->write(position.y);
    }

    DatagramWriter* write(const bomb_t &bomb) {
        return write(bomb.position)->write(bomb.timer);
    }

    template<class T, class V>
    DatagramWriter* write(std::unordered_map<T, V> &m) {
        write((container_size_t)m.size());
        for (const auto &item: m) {
            write(item.first)->write(item.second);
        }
        return this;
    }

    DatagramWriter* write(position_set &s) {
        write((container_size_t)s.size());
        for (const auto &item: s) {
            write(item);
        }
        return this;
    }

    void send() {
        client->send(data);
    }
};

#endif // UTILS_H
