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

python3 - "$ROOT" "$VIEWER/models.json" <<'PY'
import json, sys
from pathlib import Path

root = Path(sys.argv[1])
out = Path(sys.argv[2])
models = []
for pattern, label in (
    ("mechanical/reference/*.step", "reference"),
    ("mechanical/cad-out/*.step", "assembly"),
    ("mechanical/viewer/models/*.step", "viewer"),
):
    for path in sorted(root.glob(pattern)):
        models.append({
            "label": f"{label}: {path.name}",
            "url": str(path.relative_to(root)),
            "bytes": path.stat().st_size,
        })
out.write_text(json.dumps({"base": "/", "models": models}, indent=2) + "\n", encoding="utf-8")
print(f"mechanical-view: {len(models)} STEP file(s) listed in {out}")
for m in models:
    print(f"  {m['bytes']/1e6:6.2f} MB  {m['url']}")
PY

URL="http://localhost:${PORT}/mechanical/viewer/index.html"
echo "mechanical-view: serving $ROOT on http://localhost:${PORT}"
echo "mechanical-view: open $URL"
if [ "${NOOPEN:-0}" != "1" ]; then
  ( sleep 1; command -v open >/dev/null && open "$URL" || true ) &
fi
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT"
