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
  if(DEFINED BOARD AND NOT "${BOARD}" STREQUAL "${board}")
    message(FATAL_ERROR
      "${PROJECT_NAME}${SKYBLIP_PRODUCT_NAME} is a ${board} product: refusing to build it for ${BOARD}")
  endif()
  set(BOARD ${board} CACHE STRING "board this product ships on" FORCE)
endfunction()
