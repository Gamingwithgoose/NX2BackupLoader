NX2 SD Read Probe 0.1 — Yoshi demo reference build

This diagnostic was compiled for Switch 2/libounce and passed NRO format checks.
It has not been tested on a console. It DOES NOT launch Yoshi or any backup.

Install only this app:
  SD:/switch/nx2-sdprobe/nx2-sdprobe.nro

The ready-to-copy ZIP contains the switch/ folder above. Merge that folder into
your SD root. It contains no kaihen/ files or replacements for your working HEN.

Keep your existing game files in either layout:
  SD:/games/<any-folder>/base/exefs/main
  SD:/games/<any-folder>/base/romfs/...
or
  SD:/games/<any-folder>/exefs/main
  SD:/games/<any-folder>/romfs/...

The probe identifies the supplied Yoshi demo by its main executable build ID;
renaming the game's outer folder is fine. Other builds will not match this test.

1. Enter hbmenu using your usual Pokopia title override.
2. Open nx2-sdprobe.
3. Press A once to run the test. PLUS cancels or exits.
4. The screen reports how many of 13 checks passed.
5. Retrieve SD:/nx2-sdprobe-report.txt and share it in the chat.

Reads:
- Full SHA-256 checks of main, main.npdm, rtld, sdk, subsdk0, subsdk1 and subsdk2.
- 64 KiB at the beginning, middle and end of each of the two largest RomFS files.
- Current process title ID and memory information through read-only queries.
- HEN status/protocol through a newly opened, private session. The borrowed RO
  session is never closed or changed; shared filesystem roots are not requested.

Writes:
- Overwrites only SD:/nx2-sdprobe-report.txt on each A-initiated run.
- Does not write game files, saves, tickets, NAND, HEN files or resolver settings.
- Does not map executable game code or attempt a process/title substitution.

13/13 means the checked SD bytes match the supplied PC reference. Most RomFS
bytes are not hashed by this test. A pass does not establish native game execution,
license independence, complete RomFS integrity or NUP compatibility.

The same host title ID shown in the report would verify that this homebrew
process is running under that host's identity. It would not prove that retail
game code can use that identity successfully.
