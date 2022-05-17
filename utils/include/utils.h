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
    std::string description;
    std::function<void(po::variables_map&, po::options_description&)> handler;
};

bool parse_command_line(int argc, char *argv[], std::vector<flag_t> &flags);

#endif // UTILS_H
