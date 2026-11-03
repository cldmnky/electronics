.PHONY: test spice panel-preview placement release

release:
	cd tscircuit && bash scripts/release.sh

test:
	cd tscircuit && bun run typecheck && bun run test && bun run build && bun run placement
	@set -eu; exe=$$(mktemp "$${TMPDIR:-/tmp}/midibrain-test.XXXXXX"); trap 'rm -f "$$exe"' EXIT; \
	  $${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -pedantic firmware/pico/protocol_test.cpp -o "$$exe"; "$$exe"
	$(MAKE) spice

placement:
	cd tscircuit && bun run build && bun run placement

spice:
	cd spice && python3 run_tests.py

panel-preview:
	cd tscircuit && bun run panel:preview
