# Host unit-test + simulator build. Depends on nothing but a C++ compiler, so it
# runs anywhere g++/clang++ is present.
#
#   make test        build + run all host doctest suites
#   make simulator   terminal simulator          (alias: make sim)
#   make browser     browser (WASM) simulator    (alias: make web)
#   make serve       serve the browser simulator with live reload
#   make watch       incremental rebuild on save (WATCH=browser|simulator|test)
#   make             alias for test
#   make clean
#
# core/ and ui/ must compile with NO framework headers (acceptance invariant).
# The device image is built by Zephyr/west, not here (see docs/MIGRATION-ZEPHYR.md).

CXX      ?= c++
# -Wconversion is intentionally OFF: fixed-point bit-math (varint/adsl) truncates
# by design. Keep the high-signal warnings on and treat them as errors.
# -MMD -MP: emit header dependency files so editing a shared header (e.g. a
# device model's struct) recompiles every TU that includes it. Without this, stale
# objects with mismatched layouts link and crash non-deterministically.
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Wshadow -Werror \
            -fno-exceptions -fno-rtti -I. -Ivendor -MMD -MP \
            -DDOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
LDFLAGS  :=

BUILD := build

# Production sources under test (pure core/ + ui/ + host fakes).
SRC := \
  core/util/intmath.cpp \
  core/util/format.cpp \
  core/util/json_min.cpp \
  core/fec/crc.cpp \
  core/fec/manchester.cpp \
  core/fec/scramble.cpp \
  core/fec/reed_solomon.cpp \
  core/protocol/adsl.cpp \
  core/protocol/nmea_out.cpp \
  core/protocol/adsl_uplink.cpp \
  core/gnss/nmea.cpp \
  core/timing/slot.cpp \
  core/traffic/table.cpp \
  core/traffic/alarm.cpp \
  core/settings/settings.cpp \
  core/comms/config.cpp \
  ui/screens/radar.cpp \
  ui/screens/altvs.cpp \
  ui/screens/status.cpp \
  ui/framebuffer.cpp \
  products/skyblip_go/app.cpp \
  devices/drivers/sx1262.cpp \
  devices/drivers/ssd1681.cpp \
  devices/drivers/l76k.cpp \
  devices/models/rf_channel.cpp \
  devices/models/display.cpp

# Test sources (mirrored tree).
TEST_SRC := test/test_main.cpp $(wildcard test/core/*.cpp) $(wildcard test/ui/*.cpp) \
            $(wildcard test/products/*.cpp) $(wildcard test/support/*.cpp)

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) $(TEST_SRC))

BIN := $(BUILD)/skyblip_tests

# Device simulator: the VIRTUAL T-Echo Plus running the real firmware.
# Two frontends over the same virtual board:
#   make simulator  terminal -> ./build/skyblip_simulator   (ASCII e-paper, keys)
#   make browser    browser  -> ./build/browser/*.wasm       (needs Emscripten)
SIM_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) products/skyblip_go/simulator/terminal.cpp)
SIM_BIN := $(BUILD)/skyblip_simulator

EMCC ?= emcc
WEB_SRC := $(SRC) products/skyblip_go/simulator/browser.cpp
WEB_DIST := $(BUILD)/browser
WEB_PAGE := products/skyblip_go/simulator/browser/index.html
WEB_OUT := $(WEB_DIST)/skyblip_simulator.js
# Incremental browser build: compile to objects under build/web/ and link, so
# editing one file rebuilds one file (a single emcc pass over all sources costs
# ~30 s every save; this makes the edit->reload loop a couple of seconds).
WEB_BUILD := $(BUILD)/web
WEB_OBJ := $(patsubst %.cpp,$(WEB_BUILD)/%.o,$(WEB_SRC))
WEB_CXXFLAGS := -std=c++17 -O2 -I. -Ivendor -fno-exceptions -fno-rtti -MMD -MP
# Single line: a `\`-continuation here injects a space that breaks the arg.
WEB_EXPORTS := _sim_setup,_sim_step,_sim_button,_sim_backlight,_sim_power,_sim_set_range,_sim_set_fix,_sim_set_sats,_sim_set_alt,_sim_set_speed,_sim_set_track,_sim_set_climb,_sim_set_position,_sim_add_aircraft,_sim_add_threat,_sim_clear_traffic,_sim_aircraft_count,_sim_fb,_sim_fb_w,_sim_fb_h,_sim_fb_stride,_sim_backlight_on,_sim_powered,_sim_page,_sim_fix_valid,_sim_sats,_sim_lat_1e7,_sim_lon_1e7,_sim_alt_m,_sim_speed_q,_sim_track_c9,_sim_climb_e8,_sim_traffic_count,_sim_alarm_level,_sim_rx_ok,_sim_rx_bad,_main

.PHONY: test all clean simulator sim browser web serve watch dev
all: test

test: $(BIN)
	$(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS)

simulator: $(SIM_BIN)
	@echo "run: ./$(SIM_BIN)"

sim: simulator

$(SIM_BIN): $(SIM_OBJ)
	$(CXX) $(CXXFLAGS) $(SIM_OBJ) -o $@ $(LDFLAGS)

# Browser build. Requires Emscripten (https://emscripten.org).
#   make browser build (incremental)
#   make serve   serve build/browser with auto-reload (needs node/npx)
#   make watch WATCH=web   rebuild on every save
browser:
	@command -v $(EMCC) >/dev/null 2>&1 || { \
	  echo "emcc not found. Install Emscripten, then: make browser"; exit 1; }
	@$(MAKE) --no-print-directory $(WEB_OUT)

web: browser

$(WEB_BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CXXFLAGS) -c $< -o $@

$(WEB_OUT): $(WEB_OBJ) $(WEB_PAGE)
	@mkdir -p $(WEB_DIST)
	cp $(WEB_PAGE) $(WEB_DIST)/
	$(EMCC) -O2 $(WEB_OBJ) \
	  -sEXPORTED_FUNCTIONS=$(WEB_EXPORTS) \
	  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
	  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 \
	  -o $(WEB_OUT)
	@echo "built $(WEB_DIST)/ — serve it and open index.html"

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- dev loop ---------------------------------------------------------------
# C++ has no hot-module-reload: a change always means a recompile. These make
# the loop automatic instead of manual.
#
#   make dev              browser loop: serve + rebuild the wasm on every save
#   make watch            rebuild + rerun host tests on every save (default)
#   make watch WATCH=browser    rebuild the browser simulator on every save
#   make watch WATCH=simulator  rebuild the terminal simulator on every save
#   make serve            build the wasm, then serve it with auto-reload
#
# `make dev` is the one-command browser loop. Doing it by hand needs BOTH halves:
# `make serve` in one terminal AND `make watch WATCH=browser` in another.
# `make watch` alone only reruns host tests — the page will not change.
WATCH ?= test
WATCH_SRC := core ui hal devices products test

watch:
	@echo "watching (target: $(WATCH)) — Ctrl-C to stop"
	@if command -v watchexec >/dev/null 2>&1; then \
	  watchexec -e cpp,h,html --debounce 400ms -- $(MAKE) --no-print-directory $(WATCH); \
	else \
	  last=""; \
	  while true; do \
	    cur=$$(find $(WATCH_SRC) \( -name '*.cpp' -o -name '*.h' -o -name '*.html' \) \
	           -exec ls -l {} + 2>/dev/null | cksum); \
	    if [ "$$cur" != "$$last" ]; then \
	      last="$$cur"; \
	      printf '\n=== %s: rebuilding ===\n' "$$(date +%H:%M:%S)"; \
	      $(MAKE) --no-print-directory $(WATCH) || true; \
	    fi; \
	    sleep 1; \
	  done; \
	fi

# Static server for the browser sim that reloads the page when the .wasm/.js
# changes. Falls back to python3 (manual refresh) if node isn't available.
serve: browser
	@if command -v npx >/dev/null 2>&1; then \
	  echo "serving http://localhost:8000 (auto-reload)"; \
	  npx --yes live-server $(WEB_DIST) --port=8000 --no-browser; \
	else \
	  echo "serving http://localhost:8000 (manual refresh; install node for auto-reload)"; \
	  python3 -m http.server -d $(WEB_DIST) 8000; \
	fi

# One command for the browser loop: watcher in the background, server in the
# foreground; Ctrl-C stops both.
dev:
	@$(MAKE) --no-print-directory watch WATCH=browser & \
	  watcher=$$!; trap "kill $$watcher 2>/dev/null" EXIT INT TERM; \
	  $(MAKE) --no-print-directory serve

clean:
	rm -rf $(BUILD)

# Header dependencies for BOTH object trees. Without the web half, editing a
# .h left the stale .o in place and the browser kept running old code.
-include $(OBJ:.o=.d)
-include $(WEB_OBJ:.o=.d)
