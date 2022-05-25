#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H
#include <variant>
#include <optional>
#include <functional>
#include <exception>

#include <boost/program_options.hpp>

#include "connection.h"

namespace po = boost::program_options;

using values_t = std::variant<po::typed_value<std::string>*, po::typed_value<uint16_t>*>;

struct MissingFlag : public std::exception {
    const char *what() const throw() {
        return "Flag is missing! Type ./bomb-it-client --help";
    }
};

using no_param_handler_t = std::function<void(po::options_description&)>;
using param_handler_t = std::function<void(po::variables_map&)>;

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

bool parse_command_line(int argc, char *argv[], std::vector<flag_t> &flags);

host_address_t parse_host_address(const std::string &host);

#endif // COMMAND_PARSER_H