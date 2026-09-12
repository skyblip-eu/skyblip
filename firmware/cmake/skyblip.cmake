# Shared by every product's Zephyr build: the include root and the layers that
# go into an image. A product's own CMakeLists declares its board and adds
# itself; nothing here knows which product is being built.

set(SKYBLIP_FIRMWARE ${CMAKE_CURRENT_LIST_DIR}/..)

# Globbed per layer, exactly like the host build: a source that exists is built.
# simulator/ and the part models are host-only and never enter an image.
function(skyblip_shared_layers)
  target_include_directories(app PRIVATE ${SKYBLIP_FIRMWARE} ${SKYBLIP_FIRMWARE}/vendor)

  file(GLOB_RECURSE shared
    ${SKYBLIP_FIRMWARE}/core/*.cpp
    ${SKYBLIP_FIRMWARE}/ui/*.cpp
    ${SKYBLIP_FIRMWARE}/runtime/*.cpp
    ${SKYBLIP_FIRMWARE}/hardware/parts/*.cpp
    ${SKYBLIP_FIRMWARE}/hardware/platform/zephyr/*.cpp
  )
  list(FILTER shared EXCLUDE REGEX "/test_[^/]*\\.cpp$")
  target_sources(app PRIVATE ${shared})
endfunction()

# One product, one board. Called before find_package(Zephyr) so a mismatched
# pair is refused at configure time rather than diagnosed by the compiler.
function(skyblip_product_board board)
  # Compared bare: the first configure resolves t_echo_plus to
  # t_echo_plus/nrf52840 and caches that, and the qualified name is what a
  # reconfigure of an existing build directory hands back.
  string(REGEX REPLACE "/.*$" "" requested "${BOARD}")
  if(DEFINED BOARD AND NOT "${requested}" STREQUAL "${board}")
    message(FATAL_ERROR
      "${PROJECT_NAME}${SKYBLIP_PRODUCT_NAME} is a ${board} product: refusing to build it for ${BOARD}")
  endif()
  if(NOT DEFINED BOARD)
    set(BOARD ${board} CACHE STRING "board this product ships on" FORCE)
  endif()
endfunction()
