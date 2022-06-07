#include "connection.h"

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