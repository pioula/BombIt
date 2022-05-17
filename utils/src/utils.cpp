#include <utils.h>
#include <iostream>

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

using boost::asio::ip::tcp;

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
            if (flag.long_name ==  "help")
                return true;
        }
        else {
            if (flag.long_name != "help")
                throw MissingFlag();
        }
    }

    return true;
}