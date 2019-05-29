CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(mongoohttp)

ADD_EXECUTABLE(testmongoose main.cpp http_client.hpp http_server.hpp routing.hpp mongoose.h mongoose.c json.hpp ws_client.hpp)