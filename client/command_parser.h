#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H
#include <variant>
#include <optional>
#include <functional>
#include <exception>

#include <boost/program_options.hpp>

#include "connection.h"

namespace po = boost::program_options;

using values_t = std::variant<po::typed_value<std::string>*,
        po::typed_value<uint16_t>*>;

// Wyjątek zwracany w wypadku braku flagi, która jest wymagana.
struct MissingFlag : public std::exception {
    const char *what() const throw() {
        return "Flag is missing! Type ./robots-client --help";
    }
};

using no_param_handler_t = std::function<void(po::options_description&)>;
using param_handler_t = std::function<void(po::variables_map&)>;

// Struktura przetrzymująca informację o fladze.
using flag_t = struct flag {
    std::string long_name;
    std::string short_name;
    std::optional<values_t> value_type;
    bool required;
    std::string description;
    std::variant<
        no_param_handler_t ,
        param_handler_t> handler;
};

// Funkcja wczytująca parametry programu.
// arg - liczba wprowadzonychh parametrów
// argv - wprowadzone parametry
// flags - flagi, jakie przyjmuje program
void parse_command_line(int argc, char *argv[], std::vector<flag_t> &flags);

// Funkcja przetwarzająca host jako string na strukturę host_address_t
// host - nieprzetworzony adres
// return - przetworzony adres
host_address_t parse_host_address(const std::string &host);

#endif // COMMAND_PARSER_H