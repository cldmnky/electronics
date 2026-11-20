# Fabrication packets

PCB revision identifies the design marked on a board. A packet tag identifies a specific set of upload files. A vendor order ID identifies a purchase. Keep those separate; a repeat purchase can use the same packet.

1. Build from a clean committed checkout. For Froemix front + panel, build the base circuit JSON first, then run `bash scripts/release.sh` from `froemix/euro/`. For Midibrain, run `make -C midibrain order`. Do not change or bypass the production acceptance gates.
2. Before another build overwrites `fab/`, freeze the files with `python3 tools/fab-packet.py froemix proto-002 --source HEAD --out /tmp/froemix-proto-002.zip` (substitute `midibrain` and the corresponding packet name). Use `HEAD` only when it is the commit that produced the files. The tool checks file presence and writes checksums; it cannot infer the provenance of ignored build outputs.
3. Review the manifest and files. Create an annotated tag such as `fab/froemix/proto-002` at the manifest's source commit, then push that tag. Attach the ZIP to a GitHub prerelease with `gh release create "$tag" "$zip" --verify-tag --prerelease --latest=false`. The repository is public: do not include invoices or private vendor references.
4. Upload only the files listed under `uploads` in `manifest.json`. Keep your vendor order reference linked to the packet tag privately. Never replace the assets or move a published packet tag; issue the next packet instead.

Froemix packets currently include the front board and selected panel only. The rear board is reused, and its previous order provenance is unknown. Midibrain packets include front, rear, main panel and Pi panel. A packet is not a purchase, CAM acceptance, or production approval.
