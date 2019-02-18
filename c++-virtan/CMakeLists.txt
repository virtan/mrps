#
# Copyright (c) 2015-2016 Marat Abrarov (abrarov@gmail.com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)
#

cmake_minimum_required(VERSION 3.0)
project(server)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED system)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} "server.cc")
target_compile_options(${PROJECT_NAME}
    PRIVATE
    "-Os"
    "-march=native"
    "-mtune=native"
    "-std=c++17"
    "-Wall")
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${Boost_LIBRARIES}
    Threads::Threads)
