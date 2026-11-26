# BackupLoaderMKW library interface

Native libounce framebuffer GUI, 1280 x 720, five columns and three visible rows.
Build through the repository's build.bat; this directory is the frontend, not
the guest runtime. Both NROs must be copied together from release/switch.

Controls: D-pad / left stick browse; A launches; X filters Folder / NUP;
Y sorts A-Z / Z-A / Folder First / NUP First; Minus opens Options; B returns.
Options include nine themes, Minimal / Verbose diagnostics, refreshing the
library, and manually exporting the last system crash report.

Names are separate from Nx2Game.name, so display metadata never renames a save.
Folder artwork is read from control/icon files. NUP artwork is read from
metadata or bounded raw/Zstandard control-file records. Missing artwork gets
a placeholder. The console's existing application-control cache is queried read-only when backup artwork is missing; no downloads are requested. No game artwork or proprietary fonts are distributed.
The standard and Nintendo Extended system shared fonts are mapped using pl:u; native button glyphs are used where available, with libounce's built-in console
font as fallback if unavailable. Unavailable status services show unknown data.
Only existing connection status is read; no network connection is requested.

Decoded artwork is bounded to twenty 224 x 132 RGBA slots. Glyphs are bounded
to 512 cached bitmaps. Input image files are capped at 4 MiB / 2048 pixels.
The NUP fallback scans file records in batches with a 128 MiB TOC budget; name-pool spans must fit the validated table. Missing/unreadable assets use the placeholder.

Before launching, a checked, one-shot launch-request.bin is written and
envSetNextLoad selects runtime/BackupLoaderMKW-core.nro. The UI closes its
framebuffer/VI2 layer, font mapping and status sessions, frees caches and
catalog storage, and returns to hbloader. hbloader performs the NRO replacement;
the frontend program and heap do not intentionally remain resident with the game.
This does not reduce the game runtime's required host reservation. Runtime
preparation also closes its progress console before entering the guest.

The handoff and GUI are source-only changes until built and tested on hardware.
Grammar checks do not establish SDK type correctness, linking or compatibility.

stb_image.h and stb_truetype.h are unmodified upstream headers from
https://github.com/nothings/stb . Each retains its embedded MIT/public-domain
license. The shared Zstandard decoder remains under its existing vendor license.

The revised name reader rejects NPDM/META headers and generic placeholder
labels rather than using them as titles. The filename stays available as
fallback. Rounded corners and header symbols now use analytic edge coverage.
Launch request version 2 carries the frontend host/pool identity for comparing
the runtime handoff, without changing allocations or save identities.

Options > Rebuild selected folder index clears only that entry's generated
index and partial checkpoints. Press A on the tile afterwards to rebuild
and launch. Archive tables and game/save files are not removed.
Legacy one-based NUP fact tags are anchored to a valid NPDM/ACI0 payload.
