#include <ctime>
#include <iostream>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

int main()
{
    try
    {
        boost::asio::io_context io_context;

        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 2137));

        for (;;)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::string message = "Witaj ważnaku!";

            boost::system::error_code ignored_error;
            boost::asio::write(socket, boost::asio::buffer(message), ignored_error);
        }
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}