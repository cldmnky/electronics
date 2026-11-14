#!/usr/bin/env bash
#
# Serve the midibrain mechanical models to the local STEP viewer.
#
#   make mechanical-view            # serve on :8161 and open the browser
#   make mechanical-view PORT=9000  # another port
#   make mechanical-view NOOPEN=1   # just serve (CI / remote shell)
#
# The server root is the midibrain directory, so the viewer can read the
# reference STEP files under mechanical/reference/ and the generated assembly
# under mechanical/cad-out/ without copying anything. models.json is generated
# here (it is not tracked) and lists every STEP file it can find, plus their
# sizes, so the page shows what it is about to load.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PORT="${PORT:-8161}"
VIEWER="$ROOT/mechanical/viewer"

if [ ! -d "$VIEWER/node_modules/three" ] || [ ! -d "$VIEWER/node_modules/occt-import-js" ]; then
  echo "mechanical-view: installing viewer dependencies (one-off)…"
  ( cd "$VIEWER" && npm install --silent )
fi

python3 "$VIEWER/serve_manifest.py" "$ROOT" "$VIEWER/models.json"

exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT"
