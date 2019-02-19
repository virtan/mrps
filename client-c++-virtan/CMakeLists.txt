cmake_minimum_required(VERSION 3.0)
project(client)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED date_time system)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} "client.cc")
target_compile_options(${PROJECT_NAME}
    PRIVATE
    "-std=c++17"
    "-Wall")
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${Boost_LIBRARIES}
    Threads::Threads)
