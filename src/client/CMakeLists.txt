cmake_minimum_required(VERSION 3.28.1)
project(client)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost 1.85.0 REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})
if (Boost_FOUND)
    add_executable(client main.cpp)
    target_link_libraries(client PRIVATE Boost::system)
else ()
    message(FATAL_ERROR "Boost not found")
endif ()