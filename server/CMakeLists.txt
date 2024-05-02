CMAKE_MINIMUM_REQUIRED(VERSION 3.28.4)

PROJECT(videochat LANGUAGES CXX)

INCLUDE(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

SET(CMAKE_CXX_STANDARD 20)

FIND_PACKAGE(Boost REQUIRED)
FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(libpqxx REQUIRED)
find_package(redis++ REQUIRED)
FIND_PACKAGE(cassandra-cpp-driver REQUIRED)
FIND_PACKAGE(nlohmann_json REQUIRED)

ADD_EXECUTABLE(auth 
    source/auth.cpp
    source/logs.hpp
    )

TARGET_LINK_LIBRARIES(auth PRIVATE 
    ${Boost_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json
    libpqxx::pqxx
    redis++::redis++_static
    cassandra-cpp-driver::cassandra-cpp-driver
    )
