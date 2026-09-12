cmake_minimum_required(VERSION 3.20.0)

# The product IS the application, so this directory is what west builds:
#
#   west build -b t_echo_plus products/skyblip_go --sysbuild
#
# skyBlip Go ships on a T-Echo Plus. That pair is a fact about the SKU, not a
# command-line choice, so it is declared here and a different -b is refused.
set(SKYBLIP_PRODUCT_NAME skyblip_go)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/skyblip.cmake)

skyblip_product_board(t_echo_plus)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skyblip_go LANGUAGES C CXX)

skyblip_shared_layers()

file(GLOB SKYBLIP_GO_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/services/*.cpp
)
target_sources(app PRIVATE ${SKYBLIP_GO_SOURCES})

# Two devices on a bench withhold nearly every burst. This build never listens before talking, so
# the bench can say whether carrier sense is the reason. Not for air, not for merge.
target_compile_definitions(app PRIVATE SKYBLIP_BENCH_NO_LBT)
