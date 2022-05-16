cmake_minimum_required(VERSION 3.0)
project(bomb-it-client)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED program_options)
include_directories(${BOOST_INCLUDE_DIRS})
message("boost lib: ${Boost_LIBRARIES}")
message("boost inc:${Boost_INCLUDE_DIR}")

add_executable(bomb-it-client bomb-it-client.cpp)
target_link_libraries(bomb-it-client
        ${Boost_LIBRARIES}
        )


install(TARGETS DESTINATION .)

