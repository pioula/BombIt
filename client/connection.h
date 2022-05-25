#ifndef CONNECTION_H
#define CONNECTION_H
#include <string>
#include <variant>
#include <optional>
#include <exception>
#include <functional>
#include <unordered_set>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <arpa/inet.h>

#include "message_types.h"

namespace as = boost::asio;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

using datagram_size_t = uint16_t;

constexpr datagram_size_t DATAGRAM_SIZE = 65507;

using data_t = boost::array<char, DATAGRAM_SIZE>;
using datagram_t = struct datagram_t {
    data_t buf;
    datagram_size_t len;
};
using flex_buf_t = std::vector<char>;

using host_address_t = struct host_address {
    std::string host;
    std::string port;
};
const host_address_t INVALID_ADDRESS = {"", ""};

// Klasa abstrakcyjna obsługująca komunikację z serwerami jako klient.
class Client {
public:
    // Metoda wczytująca bajty bufora data.
    // data - bufor na który są wczytywane bajty.
    virtual void read_some(datagram_t &data) const = 0;
    // Metoda wysyłająca datagram data do serwera.
    // data - bufor, który zostaje wysłany
    virtual void send(const datagram_t &data) const = 0;
};

// Klasa przedstawiająca klienta komunikującego się z serwerem UDP.
class UDPClient : public Client {
protected:
    as::io_context io_context;
    udp::resolver resolver;
    udp::endpoint endpoint;
    mutable udp::socket socket;

    // Konstruktor tworzący socketa do komunikacji.
    // address - adres serwera UDP
    // port - port na którym nasłuchuje klient
    UDPClient(const host_address &address, const port_t &port) :
            resolver(udp::resolver(io_context)),
            socket(udp::socket(io_context, udp::endpoint(udp::v6(), port))) {
        endpoint = *resolver.resolve(address.host, address.port).begin();
    }

    static UDPClient* singleton;
public:
    UDPClient(UDPClient &other) = delete;

    void operator=(const UDPClient &) = delete;

    // Metoda inicjująca singleton tej klasy.
    // address - adres serwera UDP
    // port - port na którym nasłuchuje klient
    static void init(const host_address &address,
                                   const port_t &port);
    // Metoda zwracająca singleton tej klasy.
    static UDPClient *get_instance();

    void read_some(datagram_t &data) const override {
        try {
            data.len =
                static_cast<datagram_size_t>(
                    socket.receive(as::buffer(data.buf)));
        }
        catch (std::exception &err) {
            std::cerr << "Błąd połaczenia z gui "
                      << err.what() << std::endl;
            exit(1);
        }
    }

    void send(const datagram_t &data) const override {
        try {
            socket.send_to(as::buffer(data.buf, data.len), endpoint);
        }
        catch (std::exception &err) {
            std::cerr << "Błąd połaczenia z gui "
                      << err.what() << std::endl;
            exit(1);
        }
    }
};

// Klasa przedstawiająca klienta komunikującego się z serwerem TCP.
class TCPClient : public Client {
protected:
    as::io_context io_context;
    tcp::resolver resolver;
    tcp::resolver::results_type endpoints;
    mutable tcp::socket socket;

    // Konstruktor tworzący socketa do komunikacji.
    // address - adres serwera TCP
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

    // Metoda inicjująca singleton tej klasy.
    // address - adres serwera TCP
    static void init(const host_address &address);
    // Metoda zwracająca singleton tej klasy.
    static TCPClient *get_instance();

    void read_some(datagram_t &data) const override {
        boost::system::error_code err;
        data.len = static_cast<datagram_size_t>(socket.read_some(
                as::buffer(data.buf), err));
        if (err) {
            std::cerr << "Błąd połaczenia z serwerem "
                << err.what() << std::endl;
            exit(1);
        }
    }

    void send(const datagram_t &data) const override {
        try {
            socket.send(as::buffer(data.buf, data.len));
        }
        catch (std::exception &err) {
            std::cerr << "Błąd połaczenia z serwerem "
                    << err.what() << std::endl;
            exit(1);
        }
    }
};

// Klasa pomagająca w czytaniu z serwera.
class DatagramReader {
private:
    Client* client;
    datagram_t data{};
    size_t read_ptr;

    // Metoda zwracająca kolejne bytes bajtów otrzymanych od serwera.
    // bytes - liczba bajtów do pozyskania
    // return - bufor zapełniony bytes bajtami od serwera
    flex_buf_t prepare_buf(const size_t bytes) {
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
        n = static_cast<uint8_t>(buf[0]);
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

// Klasa pomagająca w wysyłaniu komunikatów do serwera.
class DatagramWriter {
private:
    Client* client;
    datagram_t data;

    // Metoda sprawdzająca czy jest wolnych bytes bajtów do zapisania w buforze.
    // Jeżeli nie ma, to wysyła bufor do serwera i zwalnia miejsce.
    // bytes - liczba bajtów do zapisania
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

    // Metoda czyszcząca bufor do zapisu.
    void clear() {
        data.len = 0;
    }

    DatagramWriter* write(const player_t &player) {
        return write(player.name)->write(player.address);
    }

    DatagramWriter* write(const std::string &str) {
        write(static_cast<uint8_t>(str.length()));
        prepare_buf(str.length());
        std::copy_n(str.begin(), str.length(), data.buf.begin() + data.len);
        data.len = static_cast<datagram_size_t>(data.len + str.length());
        return this;
    }

    DatagramWriter* write(const uint8_t n) {
        prepare_buf(sizeof(uint8_t));
        memcpy(data.buf.begin() + data.len, &n, sizeof(uint8_t));
        data.len = static_cast<datagram_size_t>(data.len + sizeof(uint8_t));
        return this;
    }

    DatagramWriter* write(const uint16_t n) {
        prepare_buf(sizeof(uint16_t));
        uint16_t net_n = htons(n);
        memcpy(data.buf.begin() + data.len, &net_n, sizeof(uint16_t));
        data.len = static_cast<datagram_size_t>(data.len + sizeof(uint16_t));
        return this;
    }

    DatagramWriter* write(const uint32_t n) {
        prepare_buf(sizeof(uint32_t));
        uint32_t net_n = htonl(n);
        memcpy(data.buf.begin() + data.len, &net_n, sizeof(uint32_t));
        data.len = static_cast<datagram_size_t>(data.len + sizeof(uint32_t));
        return this;
    }

    DatagramWriter* write(const name_t &name) {
        write(name.len);
        prepare_buf(name.len);
        std::copy_n(name.name.begin(), name.len, data.buf.begin() + data.len);
        data.len = static_cast<datagram_size_t>(data.len + name.len);
        return this;
    }

    DatagramWriter* write(const position_t &position) {
        return write(position.x)->write(position.y);
    }

    DatagramWriter* write(const bomb_t &bomb) {
        return write(bomb.position)->write(bomb.timer);
    }

    template<class T, class V>
    DatagramWriter* write(const std::unordered_map<T, V> &m) {
        write(static_cast<container_size_t>(m.size()));
        for (const auto &item: m) {
            write(item.first)->write(item.second);
        }
        return this;
    }

    DatagramWriter* write(const position_set &s) {
        write(static_cast<container_size_t>(s.size()));
        for (const auto &item: s) {
            write(item);
        }
        return this;
    }

    // Metoda, która wyssyła zapisany bufor do serwera przez klienta.
    void send() {
        client->send(data);
    }
};

#endif // CONNECTION_H