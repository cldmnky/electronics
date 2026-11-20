#!/usr/bin/env python3
"""Freeze explicit fabrication uploads, without publishing or placing an order."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

HERE = Path(__file__).resolve().parents[1]


def git(root, *args):
    return subprocess.check_output(['git', '-C', root, *args], text=True).strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('project', choices=['froemix', 'midibrain'])
    p.add_argument('packet', help='packet identifier, e.g. proto-001')
    p.add_argument('--root', type=Path, default=HERE, help='checkout that produced the files')
    p.add_argument('--source', required=True, help='known source commit; never guess from the current HEAD')
    p.add_argument('--out', type=Path, required=True, help='new ZIP path outside the generated fab directory')
    args = p.parse_args()
    if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', args.packet):
        p.error('invalid packet identifier')
    root = args.root.resolve()
    source = git(root, 'rev-parse', '--verify', args.source + '^{commit}')
    if git(root, 'status', '--porcelain'):
        p.error('source checkout must be clean')
    fab = root / args.project / 'fab'
    if args.out.resolve().is_relative_to(fab):
        p.error('output must survive the next build: keep it outside fab/')
    settings = None
    if args.project == 'froemix':
        uploads = {'front': ['euro/euro-gerbers.zip', 'euro/euro-bom-smt.csv', 'euro/euro-cpl-jlc.csv'],
                   'panel': ['euro/panel/panel-gerbers.zip']}
        extras = ['euro/EURO-BOARD.md', 'euro/panel/FRONT-PANEL.md', 'euro/euro-bom-hand.csv',
                  'euro/mechanical/MECHANICAL-STACK.md', 'euro/mechanical/hardware.csv']
        panel_data, = (fab / 'euro/panel').glob('panel-release-*.json')
        extras.append(str(panel_data.relative_to(fab)))
        panel = json.loads(panel_data.read_bytes())
        settings = {'front': {'layers': 2, 'thickness_mm': 1.6, 'assembly': 'bottom only'},
                    'panel': {'variant': panel['design'], 'material': panel['material']}}
        reused = {'rear': 'Previous order; exact revision and vendor reference unknown'}
        revisions = {'front_and_panel': re.search(r'BOARD_REVISION = "([^"]+)"',
                         git(root, 'show', source + ':froemix/euro/lib/build-info.ts'))[1]}
    else:
        uploads = {board: [f'{board}/{board}-gerbers.zip'] for board in ('front', 'rear', 'panel', 'pi-panel')}
        for board in ('front', 'rear'):
            uploads[board] += [f'{board}/{board}-bom-smt.csv', f'{board}/{board}-cpl-jlc.csv']
        extras = ['jlc-order.json', 'JLC-ASSEMBLY.md', 'assembly.md', 'sourcing-bom.csv',
                  'front/front-bom-hand.csv', 'rear/rear-bom-hand.csv']
        revisions = {'note': 'Use packet tag and source commit; no separate board revision asserted.'}
        reused = {}
    files = {name: (fab / name).read_bytes() for name in
             [*extras, *(name for group in uploads.values() for name in group)]}
    if any(not data for data in files.values()):
        p.error('empty input file')
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in sorted(files.items())}
    if args.project == 'froemix' and hashes[uploads['panel'][0]] != panel['zip']['sha256']:
        p.error('panel ZIP does not match its release manifest')
    manifest = dict(schema=1, project=args.project, tag=f'fab/{args.project}/{args.packet}',
                    source_commit=source, archived_at=datetime.now(timezone.utc).isoformat(),
                    status='Engineering prototype packet; not production approval or purchase record',
                    revisions=revisions, uploads=uploads, reused=reused, settings=settings, sha256=hashes)
    with ZipFile(args.out, 'x', ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
        archive.writestr('manifest.json', json.dumps(manifest, indent=2) + '\n')
    print(f"{manifest['tag']} → {source}\n{args.out}")


if __name__ == '__main__':
    main()
