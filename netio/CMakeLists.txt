cmake_minimum_required(VERSION 3.2)
project(netio)

add_library(netio ValidatorConnector.cpp)
set_property(TARGET netio PROPERTY POSITION_INDEPENDENT_CODE ON)
