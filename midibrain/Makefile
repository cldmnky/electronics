.PHONY: test spice panel-preview placement release firmware

# Defaults cover the setup in firmware/README.md; override on the command
# line (PICO_SDK_PATH=/path/to/sdk make firmware) if yours lives elsewhere.
PICO_SDK_PATH ?= $(HOME)/pico/pico-sdk
FW_TOOLCHAIN := $(firstword $(wildcard $(HOME)/pico/arm-gnu-toolchain-*/bin))
firmware:
	@set -eu; export PICO_SDK_PATH="$(PICO_SDK_PATH)"; \
	[ -d "$$PICO_SDK_PATH" ] || { echo "PICO_SDK_PATH=$$PICO_SDK_PATH not found (see firmware/README.md)"; exit 1; }; \
	if [ -n "$(FW_TOOLCHAIN)" ]; then PATH="$(FW_TOOLCHAIN):$$PATH"; export PATH; fi; \
	command -v arm-none-eabi-gcc >/dev/null || { echo "missing arm-none-eabi-gcc (see firmware/README.md)"; exit 1; }; \
	cmake -S firmware/pico -B firmware/pico/build -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=MinSizeRel && \
	cmake --build firmware/pico/build -j

release:
	cd tscircuit && bash scripts/release.sh

test:
	cd tscircuit && bun run typecheck && bun run test && bun run scripts/gen-pins.ts --check && bun run build && bun run placement
	@set -eu; exe=$$(mktemp "$${TMPDIR:-/tmp}/midibrain-test.XXXXXX"); trap 'rm -f "$$exe"' EXIT; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic firmware/pico/protocol_test.cpp -o "$$exe"; "$$exe"
	$(MAKE) spice

placement:
	cd tscircuit && bun run build && bun run placement

spice:
	cd spice && python3 run_tests.py

panel-preview:
	cd tscircuit && bun run panel:preview
