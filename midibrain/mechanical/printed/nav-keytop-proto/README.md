# NAV prototype sockets — generated, NOT committed

Four printable prototypes that replace the installed part's D3.2 **circular**
socket with a **square** bore matched to the rounded-square stem tip. They are
prototypes: they are not installed hardware, not a release, and not a proven fit.

```bash
/tmp/cq-venv/bin/python mechanical/printed/nav_proto.py
```

Reproducibility and dependencies:

* source: `mechanical/printed/nav_proto.py` + `printed_geometry.py`
  (`nav_proto_specimens`, `nav_proto_numbers`, `nav_proto_envelope`), which read
  `tscircuit/lib/layout.ts` and `mechanical/layout.json`;
* tool: CadQuery in a private venv
  (`uv venv /tmp/cq-venv && uv pip install --python /tmp/cq-venv/bin/python cadquery`);
* the four specimens are labelled with their square size across flats:
  `square-2.00`, `square-2.10`, `square-2.20` and `rock-square-2.10`;
* `printed_geometry.py` and `nav_keytop.py` ARE in the release fingerprint; these
  prototype files are not, because they are not installed hardware.

Nothing in this directory closes `control_actuator` or proves grip, retention,
travel or that the cap cannot rock. The tip shape is vendor MODEL geometry, the
`0.49` stem-rotation label's endpoints are unresolved, and every specimen must be
measured on the real switch before a size is chosen.
