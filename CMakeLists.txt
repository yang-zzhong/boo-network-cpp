CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

PROJECT(mongoohttp)

ADD_EXECUTABLE(testmongoose main.cpp http_client.hpp http_server.hpp routing.hpp 3rd/mongoose.h 3rd/mongoose.c 3rd/json.hpp ws_client.hpp)