# Compiled into EVERY Zephyr image built for this board, MCUboot included:
# zephyr/boards/CMakeLists.txt does add_subdirectory(${BOARD_DIR}). That is the
# point: board.c powers the rail the external secondary slot hangs off, and the
# bootloader needs it before the application exists.
zephyr_library()
zephyr_library_sources(board.c)
