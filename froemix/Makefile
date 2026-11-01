# Froemix base board — project tasks.
#
# The design is the hand-maintained tscircuit source in tscircuit/ (the KiCad
# project in tmp/ is a frozen port reference). Run `make` or `make help` for the
# target list; everything delegates to the scripts in tscircuit/ so there is one
# implementation per task.

SHELL      := /bin/bash
TSCIRCUIT  := tscircuit
FAB        := fab
BUN        := bun

# Kept in one place so every target reports the same board.
BOARD      := froemix base board (80.9 x 128.5 mm, 4 layers, 16HP x 3U)

.DEFAULT_GOAL := help
.PHONY: help install dev build gate release snapshots erc netlist footprints \
        parts decoupling sim sim-clean clean fab-clean distclean order assembly \
        check-all

help: ## show this help
	@printf '%s\n\n' "$(BOARD)"
	@printf 'usage: make <target>\n\n'
	@grep -hE '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
	  | sort \
	  | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[1m%-14s\033[0m %s\n", $$1, $$2}'
	@printf '\nartifacts land in %s/ ; the order settings live in jlc-order.json\n' "$(FAB)"

install: ## install node dependencies (once)
	cd $(TSCIRCUIT) && $(BUN) install

dev: ## local web app with live reload at http://localhost:3020
	cd $(TSCIRCUIT) && $(BUN) run dev

build: ## compile the design to dist/index/circuit.json
	cd $(TSCIRCUIT) && $(BUN) run build

# The gate is the regression contract: ERC -> build -> netlist -> footprints ->
# parts -> tsci source/pin_spec/netlist/placement/shorts/pcb-style -> snapshots.
gate: ## full 12-step regression gate (about 50s warm)
	cd $(TSCIRCUIT) && $(BUN) run gate

release: ## gate + export gerbers/BOM/CPL/SVG/STEP into fab/
	cd $(TSCIRCUIT) && $(BUN) run release
	@cp jlc-order.json $(FAB)/jlc-order.json

snapshots: ## refresh the schematic/PCB/3D snapshots
	cd $(TSCIRCUIT) && $(BUN) x tsci snapshot --update --3d

erc: ## KiCad ERC on the ported schematics (0 unexcluded errors expected)
	cd $(TSCIRCUIT) && $(BUN) run check:erc

netlist: ## KiCad netlist vs built board (must match, 105 nets)
	cd $(TSCIRCUIT) && $(BUN) run check:netlist

footprints: ## pads vs the KiCad board (must match pad for pad)
	cd $(TSCIRCUIT) && $(BUN) run check:footprints

parts: ## JLCPCB part numbers vs schematic (needs network)
	cd $(TSCIRCUIT) && $(BUN) run check:parts

decoupling: ## cap -> IC routed-copper distances (informational)
	python3 tools/report_decoupling.py

sim: ## run the four ngspice analog decks (needs ngspice)
	cd tools/sim && bash run.sh

sim-clean: ## delete simulated output
	rm -rf tools/sim/out

assembly: ## print the hand-assembly sheet (parts JLC does not mount)
	@printf '\n=== hand-assembly: parts JLC does not mount ======================\n'
	@printf 'sheet: doc/ASSEMBLY.md (copied to fab/ on release)\n\n'
	@grep -E '^\| `' doc/ASSEMBLY.md | sed 's/|/ /g' | cut -c1-150
	@printf '\norder of work: H1 -> J2 -> C24/C25 -> U16 -> CN1-4/J1 -> DaisySeed1 last\n'
	@printf '==================================================================\n'

order: release ## print the JLC order sheet and the files to upload
	@printf '\n=== JLCPCB order =================================================\n'
	@printf 'board   : %s\n' "$(BOARD)"
	@printf 'settings: jlc-order.json (ENIG, assembly on BOTH sides, THT hand-solder)\n'
	@printf 'guide   : doc/JLC-ASSEMBLY.md\n\n'
	@printf 'upload these:\n'
	@printf '  gerbers : %s/froemix-gerbers.zip\n' "$(FAB)"
	@printf '  BOM     : %s/froemix-bom-jlc.csv   (34 unique parts, 110 placements)\n' "$(FAB)"
	@printf '  CPL     : %s/froemix-cpl-jlc.csv   (100 SMT placements, THT excluded)\n\n' "$(FAB)"
	@printf 'tick on the order page:\n'
	@printf '  [ ] surface finish ENIG (flat pads for the 40 Daisy through-holes)\n'
	@printf '  [ ] assembly for BOTH sides (97 of 110 placements are on the bottom)\n'
	@printf '  [ ] 4 layers, 1.6 mm, 1 oz outer copper\n'
	@printf '  [ ] inspect: fiducials bright, bottom rotations sane, Daisy footprint empty\n'
	@printf '==================================================================\n'

check-all: gate ## run every check (alias of gate)
	@printf '\nindividual checks also available: make erc netlist footprints parts decoupling\n'

clean: ## remove build output, caches and check artifacts
	rm -rf $(TSCIRCUIT)/dist $(TSCIRCUIT)/.tscircuit $(TSCIRCUIT)/checks

fab-clean: ## remove generated fabrication artifacts
	rm -rf $(FAB)

distclean: clean fab-clean ## also drop node_modules and simulated output
	rm -rf $(TSCIRCUIT)/node_modules tools/sim/out
	@printf 'removed:$(TSCIRCUIT)/node_modules, tools/sim/out\n'
