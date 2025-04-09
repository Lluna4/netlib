cmake_minimum_required(VERSION 3.10.0)
project(netlib VERSION 0.1.0 LANGUAGES C CXX)

add_compile_options(-std=c++23)

add_library(netlib src/netlib.cpp src/utils.cpp src/comp_time_read.cpp src/comp_time_write.cpp)

