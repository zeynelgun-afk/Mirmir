No imported Unreal `.uasset` files are bundled here yet. The preferred local
source is `../../../assets/personal-use/cat-walk.glb`: Cat Walk by LostBoyz2078,
CC BY-NC 4.0, personal noncommercial prototype only. Its `sources.json` records
the license, credit, and authored in-place walk. No idle/run/sit/Groom is supplied.
The fallback Fripouille GLB (guillaume bolis, CC BY 4.0, no authored walk) and
BigSoundBank recordings (CC0) remain source files in the frozen desktop lab.

Read `../../../ASSET-LICENSES.md` and the Unreal project README before import.
`../scripts/import-assets.py` is an editor-only import helper for these local
files; it has not run because Unreal is not installed. It audits the resulting
SkeletalMesh/Skeleton and imported AnimSequence paths and does not author
animation clips or Groom. Actual UE 5.8 import and gait remain unverified.
