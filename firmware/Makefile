# Host unit-test + simulator build. Depends on nothing but a C++ compiler.
#
#   make test        build + run all host doctest suites
#   make simulator   terminal simulator
#   make browser     compile the WASM the browser page drives
#   make watch       incremental rebuild on save (WATCH=browser|simulator|test)
#
# The page itself and its dev server live in ../simulator: `make -C ../simulator
# dev`. This file compiles; it does not serve.
#   make clean
#
# core/ and ui/ must compile with NO framework headers. The device image is built
# by Zephyr/west, not here.

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

# Every layer is globbed: a file that exists is a file that is compiled and
# tested. Zephyr-only sources are inert on the host (guarded by __ZEPHYR__) and
# the silicon platform + product entry point are excluded outright.
SRC := $(filter-out hardware/platform/zephyr/%,$(wildcard core/*/*.cpp ui/*.cpp ui/*/*.cpp \
       hardware/parts/*/*.cpp runtime/*.cpp products/*/*.cpp products/*/services/*.cpp \
       simulator/world/*.cpp))
SRC := $(filter-out products/skyblip_go/main.cpp %/test_%.cpp,$(SRC))

# Test sources (mirrored tree): one folder per layer, tests never live beside
# the code they test.
TEST_SRC := $(wildcard test/*.cpp test/*/*.cpp)

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) $(TEST_SRC))

BIN := $(BUILD)/skyblip_tests

# The simulator: the same product, on the host platform, inside simulator/world.
SIMULATOR_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) simulator/frontends/terminal.cpp)
SIMULATOR_BIN := $(BUILD)/skyblip_simulator

EMCC ?= emcc
WEB_SRC := $(SRC) simulator/frontends/browser.cpp
WEB_DIST := $(BUILD)/browser
WEB_OUT := $(WEB_DIST)/skyblip_simulator.js
# Incremental browser build: compile to objects under build/web/ and link, so
# editing one file rebuilds one file (a single emcc pass over all sources costs
# ~30 s every save; this makes the edit->reload loop a couple of seconds).
WEB_BUILD := $(BUILD)/web
WEB_OBJ := $(patsubst %.cpp,$(WEB_BUILD)/%.o,$(WEB_SRC))
WEB_CXXFLAGS := -std=c++17 -O2 -I. -Ivendor -fno-exceptions -fno-rtti -MMD -MP
# Single line: a `\`-continuation here injects a space that breaks the arg.
WEB_EXPORTS := _simulator_setup,_simulator_step,_simulator_button,_simulator_touch,_simulator_backlight,_simulator_power,_simulator_set_range,_simulator_set_fix,_simulator_set_pps,_simulator_load_scenario,_simulator_mode,_simulator_capabilities,_simulator_failures,_simulator_set_sats,_simulator_set_alt,_simulator_set_speed,_simulator_set_track,_simulator_set_climb,_simulator_set_qnh,_simulator_set_airmass,_simulator_set_battery_mv,_simulator_set_external_power,_simulator_set_position,_simulator_add_aircraft,_simulator_add_threat,_simulator_clear_traffic,_simulator_aircraft_count,_simulator_fb,_simulator_fb_w,_simulator_fb_h,_simulator_fb_stride,_simulator_backlight_on,_simulator_powered,_simulator_page,_simulator_fix_valid,_simulator_sats,_simulator_lat_1e7,_simulator_lon_1e7,_simulator_alt_m,_simulator_pressure_pa,_simulator_battery_mv,_simulator_battery_percent,_simulator_battery_charging,_simulator_speed_q,_simulator_track_c9,_simulator_climb_e8,_simulator_traffic_count,_simulator_alarm_level,_simulator_vibro_ms,_simulator_rx_ok,_simulator_rx_bad,_simulator_tx_ok,_simulator_tx_busy,_simulator_slot_state,_simulator_dwell_freq,_simulator_air_count,_simulator_air_phase_ms,_simulator_air_event,_simulator_air_freq_khz,_simulator_air_rssi,_simulator_air_line,_simulator_air_deaf,_simulator_air_collisions,_simulator_add_aircraft_at,_main

.PHONY: test all clean simulator browser watch
all: test

test: $(BIN)
	$(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS)

simulator: $(SIMULATOR_BIN)
	@echo "run: ./$(SIMULATOR_BIN)"

$(SIMULATOR_BIN): $(SIMULATOR_OBJ)
	$(CXX) $(CXXFLAGS) $(SIMULATOR_OBJ) -o $@ $(LDFLAGS)

# WASM build. Requires Emscripten (https://emscripten.org). The page that loads
# it lives in ../simulator, which is also what serves it.
browser:
	@command -v $(EMCC) >/dev/null 2>&1 || { \
	  echo "emcc not found. Install Emscripten, then: make browser"; exit 1; }
	@$(MAKE) --no-print-directory $(WEB_OUT)

$(WEB_BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(EMCC) $(WEB_CXXFLAGS) -c $< -o $@

$(WEB_OUT): $(WEB_OBJ)
	@mkdir -p $(WEB_DIST)
	$(EMCC) -O2 $(WEB_OBJ) \
	  -sEXPORTED_FUNCTIONS=$(WEB_EXPORTS) \
	  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8 \
	  -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 \
	  -o $(WEB_OUT)
	@echo "built $(WEB_DIST)/ — serve it with: make -C ../simulator dev"

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ---- dev loop ---------------------------------------------------------------
# C++ has no hot-module-reload: a change always means a recompile. These make
# the loop automatic instead of manual.
#
#   make watch            rebuild + rerun host tests on every save (default)
#   make watch WATCH=browser    recompile the WASM on every save
#   make watch WATCH=simulator  rebuild the terminal simulator on every save
#
WATCH ?= test
WATCH_SRC := core ui hal hardware runtime products simulator test

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

clean:
	rm -rf $(BUILD)

# Header dependencies for BOTH object trees. Without the web half, editing a
# .h left the stale .o in place and the browser kept running old code.
-include $(OBJ:.o=.d)
-include $(WEB_OBJ:.o=.d)
