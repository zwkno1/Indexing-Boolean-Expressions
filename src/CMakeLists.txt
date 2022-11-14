cmake_minimum_required(VERSION 3.14)

project(indexer)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(./include)

add_executable(kindex_example examples/kindex_example.cpp)
