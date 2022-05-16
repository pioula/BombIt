cmake_minimum_required(VERSION 3.0)
project(bomb-it-server)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED program_options system thread)
include_directories(${BOOST_INCLUDE_DIRS})
message("boost lib: ${Boost_LIBRARIES}")
message("boost inc:${Boost_INCLUDE_DIR}")

add_executable(${PROJECT_NAME} bomb-it-server.cpp)
target_link_libraries(${PROJECT_NAME}
        ${Boost_LIBRARIES}
        )


install(TARGETS DESTINATION .)

