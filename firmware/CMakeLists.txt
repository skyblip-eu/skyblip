# CMakeLists.txt — Zephyr application build for skyBlip Go.
#
# This coexists with the Makefile (host unit tests) and platformio.ini (legacy
# Arduino). Zephyr/west only read this file; the other tools ignore it.
#
#   west build -b t_echo_plus firmware --sysbuild
#
# core/ + ui/ are pure C++ (no Zephyr); the devices/soc/zephyr adapters are the
# only vendor-coupled sources. devices/models/* are NEVER built here.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skyblip_go LANGUAGES C CXX)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

# Pure portable code (compiled unchanged for host, Arduino, and Zephyr).
file(GLOB_RECURSE SKB_PORTABLE
  ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp
)

target_sources(app PRIVATE
  ${SKB_PORTABLE}
  devices/drivers/sx1262.cpp
  devices/drivers/ssd1681.cpp
  devices/drivers/l76k.cpp
  devices/soc/zephyr/ble.cpp
  devices/soc/zephyr/mcumgr_hooks.cpp
  products/skyblip_go/app.cpp
  products/skyblip_go/device/main.cpp
)
