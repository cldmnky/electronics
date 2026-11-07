.PHONY: test release gate reports electrical mechanical mechanical-export host midifx spice panel-preview firmware order

# Defaults cover the setup in firmware/README.md; override on the command
# line (PICO_SDK_PATH=/path/to/sdk make firmware) if yours lives elsewhere.
PICO_SDK_PATH ?= $(HOME)/pico/pico-sdk
FW_TOOLCHAIN := $(firstword $(wildcard $(HOME)/pico/arm-gnu-toolchain-*/bin))
BUN ?= bun
PYTHON ?= python3
firmware:
	@set -eu; export PICO_SDK_PATH="$(PICO_SDK_PATH)"; \
	[ -d "$$PICO_SDK_PATH" ] || { echo "PICO_SDK_PATH=$$PICO_SDK_PATH not found (see firmware/README.md)"; exit 1; }; \
	if [ -n "$(FW_TOOLCHAIN)" ]; then PATH="$(FW_TOOLCHAIN):$$PATH"; export PATH; fi; \
	command -v arm-none-eabi-gcc >/dev/null || { echo "missing arm-none-eabi-gcc (see firmware/README.md)"; exit 1; }; \
	cmake -S firmware/pico -B firmware/pico/build -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=MinSizeRel && \
	cmake --build firmware/pico/build -j

# PRODUCTION release: physical acceptance gate first (fails closed, exports and
# deletes nothing when an item is unresolved), then prototype verification,
# then the fab exports. See tscircuit/scripts/release.sh and docs/acceptance.md.
release:
	cd tscircuit && bash scripts/release.sh

# Ordering path: export the three boards with the shared fab pipeline (the same
# steps `make release` runs after its gate) and print the JLCPCB order sheet.
# Ordering is not blocked by the physical acceptance gate; `make release` keeps
# tracking that evidence toward production.
order:
	cd tscircuit && bash scripts/fab_pipeline.sh
	$(PYTHON) tscircuit/scripts/order_sheet.py

# The gate alone: prints the physical acceptance state and the hardware
# fingerprint. `--fingerprint` only prints the hash.
gate:
	cd tscircuit && $(BUN) run scripts/release_gate.ts

reports:
	cd tscircuit && $(BUN) run scripts/check_reports.ts

# Prototype verification, NOT a production approval: it proves that the
# sources, netlist, placement, geometry, host code and simplified SPICE models
# agree with each other. It cannot prove that a light pipe fits, that a cable
# bends, that a receiver stays inside its recommended input current or that the
# stack is 110 mm deep. `make release` additionally requires recorded physical
# evidence (hardware-acceptance.json + docs/evidence/).
test:
	@echo "== prototype verification only: see docs/acceptance.md =="
	cd tscircuit && $(BUN) run typecheck
	cd tscircuit && $(BUN) run test
	cd tscircuit && $(BUN) run scripts/gen-pins.ts --check
	cd tscircuit && $(BUN) run build
	cd tscircuit && $(BUN) run placement
	$(MAKE) reports
	$(PYTHON) tscircuit/scripts/fab_check.py
	$(PYTHON) tscircuit/scripts/test_sourcing_bom.py
	$(PYTHON) tscircuit/scripts/test_fab_check.py
	$(MAKE) electrical
	$(MAKE) mechanical
	$(MAKE) host
	$(MAKE) spice

# Electrical checker and its negative tests. Delivered by the electrical work
# item; this target fails with a clear message until that file is present, so a
# release cannot pass with the check silently missing.
electrical:
	@[ -f tscircuit/scripts/check_electrical.py ] || { \
	  echo "PENDING: tscircuit/scripts/check_electrical.py is not in this tree (electrical work item)"; exit 1; }
	@[ -f tscircuit/scripts/test_electrical.py ] || { \
	  echo "PENDING: tscircuit/scripts/test_electrical.py is not in this tree (electrical work item)"; exit 1; }
	cd tscircuit && $(PYTHON) scripts/check_electrical.py
	cd tscircuit && $(PYTHON) scripts/test_electrical.py

# The canonical TS-to-CAD bridge (mechanical work item). `--check` writes
# nothing and fails when mechanical/layout.json is stale against
# lib/layout.ts; the location/stack geometry tests then run on the committed
# export. Regenerate deliberately with `make mechanical-export` after an
# intentional layout change, and commit the result. Neither step fabricates a
# production output; full CAD remains optional and tool-dependent.
mechanical:
	@if [ -f tscircuit/scripts/export-mechanical.ts ] && [ -d mechanical ]; then \
	  cd tscircuit && $(BUN) run scripts/export-mechanical.ts --check && cd .. && \
	  $(PYTHON) -m unittest discover -s mechanical -p 'test_*.py'; \
	else \
	  echo "PENDING: the mechanical bridge (tscircuit/scripts/export-mechanical.ts,"; \
	  echo "         mechanical/layout.json) is not in this tree; freshness and geometry"; \
	  echo "         checks are reserved for the mechanical work item."; \
	  exit 1; \
	fi

mechanical-export:
	cd tscircuit && $(BUN) run scripts/export-mechanical.ts

host: midifx
	@set -eu; exe=$$(mktemp "$${TMPDIR:-/tmp}/midibrain-test.XXXXXX"); trap 'rm -f "$$exe"' EXIT; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic firmware/pico/protocol_test.cpp -o "$$exe"; "$$exe"; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic \
	    pi/emulator/clock_sim.cpp pi/emulator/clock_sim_test.cpp -o "$$exe"; "$$exe"; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic \
	    firmware/pico/clock_follow_test.cpp pi/emulator/clock_sim.cpp -o "$$exe"; "$$exe"; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic -I. \
	    firmware/pico/spi_slave_test.cpp pi/emulator/clock_sim.cpp -o "$$exe"; "$$exe"; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic -I. \
	    pi/link/link.cpp pi/link/link_reliability_test.cpp -o "$$exe"; "$$exe"

# Shared MIDI FX core + schema suite (midibrain/midifx): the firmware compiles
# this header today and the Pi adopts the same one, so the behavior expectations
# live here once. `gen_params.py --check` fails when pipes.json/wire-order.txt
# and the committed generated tables disagree; test_codegen.py checks that
# alarm, including a deliberate reorder that must be refused.
midifx:
	@set -eu; dir=$$(mktemp -d "$${TMPDIR:-/tmp}/midibrain-midifx.XXXXXX"); trap 'rm -rf "$$dir"' EXIT; \
	  $(PYTHON) midifx/gen/gen_params.py --check; \
	  PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest discover -s midifx/tests -p 'test_*.py'; \
	  /usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_core.cpp -o "$$dir/test_core"; \
	  /usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_schema.cpp -o "$$dir/test_schema"; \
	  "$$dir/test_core"; "$$dir/test_schema"

placement:
	cd tscircuit && $(BUN) run build && $(BUN) run placement

spice:
	cd spice && $(PYTHON) run_tests.py

panel-preview:
	cd tscircuit && $(BUN) run panel:preview
