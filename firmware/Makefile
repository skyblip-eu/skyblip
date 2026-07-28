# Host unit-test + simulator build. Depends on nothing but a C++ compiler.
#
#   make test        build + run all host doctest suites
#   make simulator   terminal simulator          (alias: make sim)
#   make browser     browser (WASM) simulator    (alias: make web)
#   make dev         serve the browser simulator at sim.skyblip.localhost:5114
#   make serve       serve the browser simulator with live reload
#   make watch       incremental rebuild on save (WATCH=browser|simulator|test)
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
SRC := $(filter-out $(wildcard hardware/parts/*/test_*.cpp),$(SRC))

# Test sources (mirrored tree).
TEST_SRC := $(wildcard test/*.cpp test/*/*.cpp hardware/parts/*/test_*.cpp)

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) $(TEST_SRC))

BIN := $(BUILD)/skyblip_tests

# The simulator: the same product, on the host platform, inside simulator/world.
SIMULATOR_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC) simulator/frontends/terminal.cpp)
SIMULATOR_BIN := $(BUILD)/skyblip_simulator

EMCC ?= emcc
# 5114 = SImuLAtor, the same leet trick as the website's 8119 = BLIP.
SIMULATOR_PORT ?= 5114
SIMULATOR_HOST ?= sim.skyblip.localhost
WEB_SRC := $(SRC) simulator/frontends/browser.cpp
WEB_DIST := $(BUILD)/browser
WEB_PAGE := simulator/frontends/browser/index.html
WEB_OUT := $(WEB_DIST)/skyblip_simulator.js
# Incremental browser build: compile to objects under build/web/ and link, so
# editing one file rebuilds one file (a single emcc pass over all sources costs
# ~30 s every save; this makes the edit->reload loop a couple of seconds).
WEB_BUILD := $(BUILD)/web
WEB_OBJ := $(patsubst %.cpp,$(WEB_BUILD)/%.o,$(WEB_SRC))
WEB_CXXFLAGS := -std=c++17 -O2 -I. -Ivendor -fno-exceptions -fno-rtti -MMD -MP
# Single line: a `\`-continuation here injects a space that breaks the arg.
WEB_EXPORTS := _simulator_setup,_simulator_step,_simulator_button,_simulator_backlight,_simulator_power,_simulator_set_range,_simulator_set_fix,_simulator_set_pps,_simulator_load_scenario,_simulator_mode,_simulator_capabilities,_simulator_failures,_simulator_set_sats,_simulator_set_alt,_simulator_set_speed,_simulator_set_track,_simulator_set_climb,_simulator_set_position,_simulator_add_aircraft,_simulator_add_threat,_simulator_clear_traffic,_simulator_aircraft_count,_simulator_fb,_simulator_fb_w,_simulator_fb_h,_simulator_fb_stride,_simulator_backlight_on,_simulator_powered,_simulator_page,_simulator_fix_valid,_simulator_sats,_simulator_lat_1e7,_simulator_lon_1e7,_simulator_alt_m,_simulator_speed_q,_simulator_track_c9,_simulator_climb_e8,_simulator_traffic_count,_simulator_alarm_level,_simulator_vibro_ms,_simulator_rx_ok,_simulator_rx_bad,_main

.PHONY: test all clean simulator sim browser web serve watch dev
all: test

test: $(BIN)
	$(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS)

simulator: $(SIMULATOR_BIN)
	@echo "run: ./$(SIMULATOR_BIN)"

sim: simulator

$(SIMULATOR_BIN): $(SIMULATOR_OBJ)
	$(CXX) $(CXXFLAGS) $(SIMULATOR_OBJ) -o $@ $(LDFLAGS)

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

# Static server for the browser simulator that reloads the page when the .wasm/.js
# changes. Falls back to python3 (manual refresh) if node isn't available.
serve: browser
	@if command -v npx >/dev/null 2>&1; then \
	  echo "serving http://$(SIMULATOR_HOST):$(SIMULATOR_PORT) (auto-reload)"; \
	  npx --yes live-server $(WEB_DIST) --port=$(SIMULATOR_PORT) --no-browser; \
	else \
	  echo "serving http://$(SIMULATOR_HOST):$(SIMULATOR_PORT) (manual refresh; install node for auto-reload)"; \
	  python3 -m http.server -d $(WEB_DIST) $(SIMULATOR_PORT); \
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
