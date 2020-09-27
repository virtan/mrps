cmake_minimum_required(VERSION 3.0)
project(server)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED COMPONENTS system date_time chrono regex program_options)
find_package(Threads REQUIRED)

set(libraries )
list(APPEND libraries ${Boost_LIBRARIES})
list(APPEND libraries ${Boost_LIBRARIES} Threads::Threads)
if(WIN32)
    list(APPEND libraries "ws2_32" "mswsock")
endif()

if(NOT Boost_USE_STATIC_LIBS)
    list(APPEND compile_definitions "BOOST_ALL_DYN_LINK")
endif()
if(WIN32)
    list(APPEND compile_definitions
        WIN32
        WIN32_LEAN_AND_MEAN
        WINVER=0x0501
        _WIN32_WINNT=0x0501
        _WIN32_WINDOWS=0x0501
        _WIN32_IE=0x0600
        _UNICODE
        UNICODE
        _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -municode")
endif()

add_executable(server "server.cc")
target_compile_features(server PRIVATE cxx_std_11 cxx_auto_type)
target_compile_definitions(server PRIVATE ${compile_definitions})
target_include_directories(server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(server PRIVATE ${libraries})
