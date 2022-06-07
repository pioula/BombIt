#include <iostream>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "blocking_queue.h"
#include <exception>
#include <iostream>

using boost::asio::ip::tcp;

int main(int argc, char* argv[])
{
    BlockingQueue<int> q;

//  try
//  {
//    if (argc != 2)
//    {
//      std::cerr << "Usage: client <host>" << std::endl;
//      return 1;
//    }
//
//    boost::asio::io_context io_context;
//
//    tcp::resolver resolver(io_context);
//    tcp::resolver::results_type endpoints =
//      resolver.resolve(argv[1], "5566");
//
//    tcp::socket socket(io_context);
//    boost::asio::connect(socket, endpoints);
//
//      boost::array<char, 128> buf{'J', 'A', 'J', 'C', 'O'};
//      boost::system::error_code error;
//      socket.send(boost::asio::buffer(buf, 5));
//  }
//  catch (std::exception& e)
//  {
//    std::cerr << e.what() << std::endl;
//  }

    try
    {
        boost::asio::io_context io_context;

        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 5566));

        std::vector<boost::thread> v;
        boost::thread t1{[&](){
            for (;;)
            {
                puts("Czekam");
                tcp::socket socket(io_context);
                acceptor.accept(socket);

                puts("jajco");
            }
        }};
        puts("Hi!");
        sleep(1);
        puts("Budzimy spiocha");
        t1.interrupt();
        t1.interrupt();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

  return 0;
}