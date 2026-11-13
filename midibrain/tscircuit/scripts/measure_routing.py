#!/usr/bin/env python3
"""Report routed-copper totals and the closest copper-to-cutout approach.

Placement decisions on this board were being argued against a *build succeeded*
signal, which is too coarse: the autorouter can find a legal path that is 40 mm
longer and turns layers seven times, and nothing complains. This prints the
numbers those decisions should be made on, from the circuit JSON that `tsci
build` already wrote:

  * total routed copper and the MIDI subset,
  * per-net length for every MIDI_* net,
  * vertices and layer changes per net (a detour signature: a net that bounces
    between layers is squeezing through a corridor something else owns),
  * the minimum copper-to-cutout clearance and which net owns it.

Usage:  python3 scripts/measure_routing.py [front|index]

Not a test and not a gate: it measures, it does not judge. Values move with
every placement change, so compare two builds rather than reading one alone.
"""
import json, math, os, sys
BOARD = sys.argv[1] if len(sys.argv) > 1 else "front"

def load():
    d = json.load(open('dist/%s/circuit.json' % BOARD))
    names = {t['source_trace_id']: t.get('display_name','') for t in d if t.get('type')=='source_trace'}
    traces = []
    for e in d:
        if e.get('type') != 'pcb_trace': continue
        pts = [(p['x'], p['y']) for p in e.get('route', [])]
        if len(pts) < 2: continue
        L = sum(math.dist(pts[i], pts[i+1]) for i in range(len(pts)-1))
        w = e.get('width') or 0.2
        flips = sum(1 for p in e.get('route', [])[:-1] if True) # placeholder
        layers = [p.get('layer') for p in e.get('route', [])]
        flips = sum(1 for i in range(len(layers)-1) if layers[i] != layers[i+1])
        traces.append({'name': names.get(e.get('source_trace_id'), e.get('connection_name','')), 'pts': pts, 'len': L, 'w': w,
                       'pts_count': len(pts), 'flips': flips})
    cut = []
    for c in d:
        if c.get('type') == 'pcb_cutout' and c.get('points'):
            cut.append([(p['x'], p['y']) for p in c['points']])
    return traces, cut

def seg_point_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx-ax, by-ay
    L2 = dx*dx + dy*dy
    t = 0 if L2 == 0 else max(0, min(1, ((px-ax)*dx + (py-ay)*dy)/L2))
    return math.dist(p, (ax+t*dx, ay+t*dy))

def inside(p, poly):
    x, y = p; ins = False; n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i+1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y-y1)*(x2-x1)/(y2-y1)
            if x < xi: ins = not ins
    return ins

def min_clearance(traces, cut):
    worst = (1e9, None)
    for t in traces:
        for i in range(len(t['pts'])-1):
            a, b = t['pts'][i], t['pts'][i+1]
            steps = max(2, int(math.dist(a, b)/0.1))
            for k in range(steps+1):
                f = k/steps
                p = (a[0]+(b[0]-a[0])*f, a[1]+(b[1]-a[1])*f)
                for poly in cut:
                    if inside(p, poly):
                        return (-t['w']/2, t['name'])
                    d = min(seg_point_dist(p, poly[j], poly[(j+1) % len(poly)]) for j in range(len(poly)))
                    if d - t['w']/2 < worst[0]:
                        worst = (d - t['w']/2, t['name'])
    return worst

traces, cut = load()
total = sum(t['len'] for t in traces)
midi = [t for t in traces if 'MIDI_' in t['name']]
bynet = {}
for t in midi:
    key = t['name'].split(' to ')[-1] if ' to ' in t['name'] else t['name']
    bynet[key] = bynet.get(key, 0) + t['len']
worst, who = min_clearance(traces, cut)
errs = (json.load(open('dist/%s/errors.json' % BOARD)) if os.path.exists('dist/%s/errors.json' % BOARD) else [])
cut_errs = [e for e in errs if 'cutout' in e.get('message','')]
print('traces: %d   total length %7.1f mm' % (len(traces), total))
print('MIDI nets: %d traces, %6.1f mm' % (len(midi), sum(t['len'] for t in midi)))
rows = {}
for t in midi:
    key = t['name'].split(' to ')[-1] if ' to ' in t['name'] else t['name']
    agg = rows.setdefault(key, {'len': 0.0, 'pts': 0, 'flips': 0})
    agg['len'] += t['len']; agg['pts'] += t['pts_count']; agg['flips'] += t['flips']
for k in sorted(rows):
    a = rows[k]
    print('   %-24s %6.1f mm  %3d vertices  %2d layer changes' % (k, a['len'], a['pts'], a['flips']))
print('min copper->cutout clearance: %.3f mm  (%s)' % (worst, who))
print('DRC cutout violations: %d' % len(cut_errs))
