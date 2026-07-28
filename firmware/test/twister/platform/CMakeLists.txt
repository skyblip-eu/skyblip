cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../hardware)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(platform_adapters LANGUAGES C CXX)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_sources(app PRIVATE src/main.cpp)
