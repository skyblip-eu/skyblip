cmake_minimum_required(VERSION 3.20.0)

# The board lives with the rest of the hardware layer, so Zephyr is pointed at it
# rather than at a second boards/ tree beside it.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/hardware)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skyblip_go LANGUAGES C CXX)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# Globbed per layer, exactly like the host build: a source that exists is built.
# simulator/ and the part models are host-only and never enter the image.
file(GLOB_RECURSE SKB_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hardware/parts/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hardware/platform/zephyr/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/products/skyblip_go/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/products/skyblip_go/services/*.cpp
)
list(FILTER SKB_SOURCES EXCLUDE REGEX "/test_[^/]*\\.cpp$")

target_sources(app PRIVATE ${SKB_SOURCES})
