cmake_minimum_required(VERSION 3.1)

project(MIMUW_SIK_TCP_POLL)

set(CMAKE_CPP_STANDARD 20)
set(CMAKE_CPP_FLAGS "-g -Wall -Wextra -Wconversion -Werror -O2")

add_executable(bomb-it-client bomb-it-client.cpp)

install(TARGETS DESTINATION .)
