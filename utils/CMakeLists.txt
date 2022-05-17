cmake_minimum_required(VERSION 3.0)
project(utils)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED program_options system thread)
include_directories(${BOOST_INCLUDE_DIRS})
message("boost lib: ${Boost_LIBRARIES}")
message("boost inc:${Boost_INCLUDE_DIR}")

file(GLOB SOURCES src/*)
add_library(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<INSTALL_INTERFACE:include>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE
            ${Boost_LIBRARIES}
        )