# The page you open, and the server that hands it to you. The firmware next door
# compiles the WASM this page drives; nothing here is firmware.
#
#   make dev     build + serve on sim.skyblip.localhost:5114, rebuilding on save
#   make serve   serve what is already built
#   make build   copy the page and the firmware's WASM into build/
#   make clean
#
# 5114 = SImuLAtor, the same leet trick as the website's 8119 = BLIP.
PORT ?= 5114
HOST ?= sim.skyblip.localhost

FIRMWARE := ../firmware
WASM_DIR := $(FIRMWARE)/build/browser
BUILD := build

.PHONY: dev serve build watch clean

build:
	@$(MAKE) --no-print-directory -C $(FIRMWARE) browser
	@mkdir -p $(BUILD)
	cp index.html $(BUILD)/
	cp $(WASM_DIR)/skyblip_simulator.js $(WASM_DIR)/skyblip_simulator.wasm $(BUILD)/

# Auto-reload needs node; python3 serves the same files with a manual refresh.
serve:
	@if command -v npx >/dev/null 2>&1; then \
	  echo "serving http://$(HOST):$(PORT) (auto-reload)"; \
	  npx --yes live-server $(BUILD) --port=$(PORT) --no-browser; \
	else \
	  echo "serving http://$(HOST):$(PORT) (manual refresh; install node for auto-reload)"; \
	  python3 -m http.server -d $(BUILD) $(PORT); \
	fi

# C++ has no hot reload, so a change always means a recompile; this makes the
# loop automatic. Watches the page AND the firmware sources the WASM is built
# from, because a change in either is a change to what the page shows.
watch:
	@echo "watching — Ctrl-C to stop"
	@if command -v watchexec >/dev/null 2>&1; then \
	  watchexec -w . -w $(FIRMWARE) -e cpp,h,html --debounce 400ms -- \
	    $(MAKE) --no-print-directory build; \
	else \
	  last=""; \
	  while true; do \
	    cur=$$(find . $(FIRMWARE)/core $(FIRMWARE)/ui $(FIRMWARE)/hal $(FIRMWARE)/hardware \
	           $(FIRMWARE)/runtime $(FIRMWARE)/products $(FIRMWARE)/simulator \
	           \( -name '*.cpp' -o -name '*.h' -o -name '*.html' \) \
	           -exec ls -l {} + 2>/dev/null | cksum); \
	    if [ "$$cur" != "$$last" ]; then \
	      last="$$cur"; \
	      printf '\n=== %s: rebuilding ===\n' "$$(date +%H:%M:%S)"; \
	      $(MAKE) --no-print-directory build || true; \
	    fi; \
	    sleep 1; \
	  done; \
	fi

# Watcher in the background, server in the foreground; Ctrl-C stops both.
dev: build
	@$(MAKE) --no-print-directory watch & \
	  watcher=$$!; trap "kill $$watcher 2>/dev/null" EXIT INT TERM; \
	  $(MAKE) --no-print-directory serve

clean:
	rm -rf $(BUILD)
