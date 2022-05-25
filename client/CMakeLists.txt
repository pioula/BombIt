cmake_minimum_required(VERSION 3.0)
project(robots-client)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wconversion -Werror -O2")

find_package(Boost REQUIRED program_options system thread)
include_directories(${BOOST_INCLUDE_DIRS})
message("boost lib: ${Boost_LIBRARIES}")
message("boost inc:${Boost_INCLUDE_DIR}")

add_library(command_parser command_parser.cpp command_parser.h)
add_library(connection connection.cpp connection.h message_types.h)
add_executable(${PROJECT_NAME} bomb-it-client.cpp message_types.h)
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} connection command_parser)

install(TARGETS DESTINATION .)