# Froemix — project tasks.
#
# Two tscircuit designs: the base board in tscircuit/ and the 20HP euro board in
# euro/ (front board + panel). Every target below covers BOTH unless it is
# base-board specific (ERC/netlist/parts/sim/assembly are KiCad or analog checks
# that only exist for the base board). `make release` produces every fab set:
# fab/ for the base board, fab/euro/ for the front board and its panel.

SHELL      := /bin/bash
TSCIRCUIT  := tscircuit
EURO       := euro
FAB        := fab
EURO_FAB   := $(FAB)/euro
BUN        := bun
# Capture one UTC build date for every subprocess in a two-board release.
# Override for an intentional reproducible build (YYYY-MM-DD).
export FROEMIX_BUILD_DATE ?= $(shell date -u +%F)

# Kept in one place so every target reports the same boards.
BOARD      := froemix base board (80.9 x 128.5 mm, 4 layers, 16HP x 3U) + euro board (20HP front board + panel)

.DEFAULT_GOAL := help
.PHONY: help install dev build gate test release snapshots erc netlist footprints \
        parts decoupling sim sim-clean clean fab-clean distclean order assembly \
        check-all acceptance acceptance-check unit-tests mechanical

help: ## show this help
	@printf '%s\n\n' "$(BOARD)"
	@printf 'usage: make <target>\n\n'
	@grep -hE '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
	  | sort \
	  | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[1m%-14s\033[0m %s\n", $$1, $$2}'
	@printf '\nartifacts: %s/ (base board) and %s/ (euro board + panel)\n' "$(FAB)" "$(EURO_FAB)"
	@printf 'order settings: jlc-order.json ; base-board hand assembly: doc/ASSEMBLY.md\n'

install: ## install node dependencies for both boards (once)
	cd $(TSCIRCUIT) && $(BUN) install
	cd $(EURO) && $(BUN) install

dev: ## base-board dev server (euro: `cd euro && bun run dev`)
	cd $(TSCIRCUIT) && $(BUN) run dev

build: ## compile both designs (base + euro board and panel)
	cd $(TSCIRCUIT) && $(BUN) run build
	cd $(EURO) && $(BUN) run build

# Prototype verification. `make test` runs everything that can be checked without
# a bench, and `make gate` is its shorter form. Neither is a physical approval:
# the production release additionally requires every item in
# hardware-acceptance.json to be accepted with recorded evidence (doc/acceptance.md).
#
# The gates are the regression contracts: base board ERC -> build -> netlist ->
# footprints -> parts -> value literals -> tsci source/pin_spec/netlist/
# placement/shorts/pcb-style -> silkscreen bounds -> snapshots (15 steps), then
# the euro board's 11 steps (build, netlist, placement, shorts, style, mating,
# mux-input contract, mechanical stack, panel artwork, regression tests, snapshots).
# Euro's gate reads BOTH built boards: this target builds the base first; for a
# standalone `cd euro && bun run gate`, run `make build` first.
gate: ## prototype regression gates + physical acceptance state (NOT an approval)
	cd $(TSCIRCUIT) && $(BUN) run gate
	cd $(EURO) && $(BUN) run gate
	$(BUN) tools/release/gate.ts --report

test: ## prototype verification: gates, SPICE, mechanical, unit tests (NOT an approval)
	@printf '\n== prototype verification only: no physical item is accepted ==\n'
	@printf '== a release needs doc/acceptance.md evidence; see `make acceptance` ==\n\n'
	$(MAKE) gate
	$(MAKE) mechanical
	$(MAKE) unit-tests
	$(MAKE) sim

# Offline tests only: no board build, no network, no KiCad. The silk unit test
# and the euro panel/mechanical unit tests read the built circuits, so the gates
# run them after `tsci build` instead. The JLC order tests (tools/release/
# jlc-order.test.ts) count the BUILT designators, so run `make build` once if
# `dist/index/circuit.json` is absent; `make test` has already done that.
unit-tests: ## offline unit tests: tool CLIs, ERC checker, acceptance machinery
	python3 -m unittest discover -s tools -p 'test_*.py'
	$(BUN) test tools/release

mechanical: ## mechanical-stack and panel checks that write nothing (needs `make build`)
	cd $(EURO) && $(BUN) scripts/mechanical_check.ts
	cd $(EURO) && $(BUN) scripts/panel_check.ts

acceptance: ## print the physical acceptance state (NOT an approval; writes nothing)
	$(BUN) tools/release/gate.ts --report

acceptance-check: ## production gate: every physical item accepted (fail closed, writes nothing)
	$(BUN) tools/release/gate.ts --check

# PRODUCTION release: the physical acceptance gate runs FIRST, as a prerequisite,
# so a pending or invalid item stops the release before any build, export or
# deletion (scripts/release.sh and scripts/panel_release.ts repeat the guard for
# direct use). No flag skips it.
release: acceptance-check ## physical acceptance + gates + ALL fab sets into fab/
	cd $(TSCIRCUIT) && $(BUN) run release
	@cp jlc-order.json $(FAB)/jlc-order.json
	cd $(EURO) && bash scripts/release.sh

snapshots: ## refresh both boards' schematic/PCB/3D snapshots
	cd $(TSCIRCUIT) && FROEMIX_BUILD_DATE=2024-08-10 $(BUN) x tsci build && FROEMIX_BUILD_DATE=2024-08-10 $(BUN) x tsci snapshot --update --3d
	cd $(EURO) && FROEMIX_BUILD_DATE=2024-08-10 $(BUN) x tsci build && FROEMIX_BUILD_DATE=2024-08-10 $(BUN) x tsci snapshot --update --3d

erc: ## KiCad ERC on the ported schematics (0 unexcluded errors expected)
	cd $(TSCIRCUIT) && $(BUN) run check:erc

netlist: ## KiCad netlist vs built board (must match, 102 nets)
	cd $(TSCIRCUIT) && $(BUN) run check:netlist

footprints: ## pads vs the KiCad board (must match pad for pad)
	cd $(TSCIRCUIT) && $(BUN) run check:footprints

parts: ## JLCPCB part numbers vs schematic (needs network)
	cd $(TSCIRCUIT) && $(BUN) run check:parts

decoupling: ## cap -> IC routed-copper distances (informational)
	python3 tools/report_decoupling.py --circuit $(TSCIRCUIT)/dist/index/circuit.json

sim: ## run the four ngspice analog decks (needs ngspice)
	cd tools/sim && bash run.sh

sim-clean: ## delete simulated output
	rm -rf tools/sim/out

assembly: ## print the hand-assembly sheet (parts JLC does not mount)
	@printf '\n=== hand-assembly: parts JLC does not mount ======================\n'
	@printf 'sheet: doc/ASSEMBLY.md (copied to fab/ on release)\n\n'
	@grep -E '^\| `' doc/ASSEMBLY.md | sed 's/|/ /g' | cut -c1-150
	@printf '\norder of work: H1 -> J2 -> U16/C24/C25 -> CN1-4/J1 -> Daisy sockets -> rails + J1 shunts -> plug in Daisy last\n'
	@printf '==================================================================\n'

order: release ## print the order sheet and the files to upload for both boards
	@printf '\n=== JLCPCB order =================================================\n'
	@printf 'board   : %s\n' "$(BOARD)"
	@printf 'settings: jlc-order.json (lead-free HASL, SMT on BOTTOM only, THT hand-solder)\n'
	@printf 'guide   : doc/JLC-ASSEMBLY.md (base) / doc/EURO-BOARD.md (euro)\n\n'
	@printf 'base board - upload these:\n'
	@printf '  gerbers : %s/froemix-gerbers.zip\n' "$(FAB)"
	@printf '  BOM     : %s/froemix-bom-smt.csv (upload; hand list: froemix-bom-hand.csv)\n' "$(FAB)"
	@printf '  CPL     : %s/froemix-cpl-jlc.csv\n\n' "$(FAB)"
	@printf 'euro board + panel - upload these:\n'
	@printf '  front   : %s/euro-gerbers.zip + %s/euro-bom-smt.csv\n' "$(EURO_FAB)" "$(EURO_FAB)"
	@printf '            hand list: %s/euro-bom-hand.csv\n' "$(EURO_FAB)"
	@printf '            %s/euro-cpl-jlc.csv (19 SMT; THT hand-solder)\n' "$(EURO_FAB)"
	@printf '  panel   : %s/panel/panel-gerbers.zip (2mm FR4, holes + silk only; verified by panel:release)\n' "$(EURO_FAB)"
	@printf '  mech    : %s/mechanical/ (MODE/NAV STLs, hardware list, stack + acceptance guide)\n\n' "$(EURO_FAB)"
	@printf 'tick on the order page:\n'
	@printf '  [ ] surface finish lead-free HASL (confirm quote and assembly preview)\n'
	@printf '  [ ] base SMT assembly on BOTTOM only (all 107 placements); THT fitted by hand\n'
	@printf '  [ ] 4 layers, 1.6 mm, 1 oz outer copper (base) / 2 layers, 2 mm, empty copper (panel)\n'
	@printf '  [ ] headers are 6mm-shoulder X4612 (not old short A2005WV); CN4 cut to 7 ways\n'
	@printf '  [ ] stack: 11mm clear spacers; J1+shunts <=9.5mm; front tails <=1mm\n'
	@printf '  [ ] inspect: fiducials bright, bottom rotations sane, Daisy footprint empty\n'
	@printf '  [ ] separately source two 1x20 female Daisy sockets; verify fit and rack clearance\n'
	@printf '==================================================================\n'

check-all: gate ## run every check (alias of gate)
	@printf '\nindividual checks also available: make erc netlist footprints parts decoupling\n'

clean: ## remove both boards' build output, caches and check artifacts
	rm -rf $(TSCIRCUIT)/dist $(TSCIRCUIT)/.tscircuit $(TSCIRCUIT)/checks
	rm -rf $(EURO)/dist $(EURO)/.tscircuit $(EURO)/checks

fab-clean: ## remove generated fabrication artifacts (both boards)
	rm -rf $(FAB)

distclean: clean fab-clean ## also drop node_modules (both) and simulated output
	rm -rf $(TSCIRCUIT)/node_modules $(EURO)/node_modules tools/sim/out
	@printf 'removed: $(TSCIRCUIT)/node_modules, $(EURO)/node_modules, tools/sim/out\n'
