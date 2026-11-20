## Current test: Folder Execution Suite 0.3

Run build.bat to build the current test into release/switch/ with its matching owned executable fixtures in release/games/. See probes/folder-suite/README.txt for setup, test coverage and remaining blockers. This test fully relocates Yoshi's verified modules and collects read-only startup capabilities; it does not start the game. Offline comparison with the original rtld matched all 2,691,120 relocation targets after lazy imports were resolved. Earlier diagnostics and the passed 0.1 test remain as source.

# NX2BackupLoader 0.3.0

Switch 2 backup research and diagnostics. **Game execution is not implemented.**
The project name describes the goal, not an existing launch capability.

## Start on the PC

Inspect a decrypted backup without contacting a console or changing the backup:

```powershell
python scripts/inspect-backup.py "D:\Backups\MyGame" --inventory
```

Supports `exefs/` + `romfs/` directly, or `base/exefs/` + `base/romfs/`.
Checks NSO header/range consistency, bounded NPDM declarations, and optional FTP
`.listing` file sizes. Outputs JSON to the terminal; exit code 1 means issues.
This does not verify NSO compression/hashes, NPDM signatures, complete game
content, update pairing, or launchability. Updates are not automatically overlaid.

Compare host-title and backup policies using explicit NPDM files:

```powershell
python scripts/compare-policies.py "D:\Backups\Host\base\exefs\main.npdm" "D:\Backups\Target\base\exefs\main.npdm"
```

Use the host NPDM matching the version installed on the console. This compares
declarations, not a running process's effective permissions. A match is not
evidence of compatibility.

With Python 3.14+, measure NUP-style compression/dedup savings without creating
an archive or modifying the source backup:

```powershell
python scripts/estimate-nup-size.py "D:\Backups\MyGame" --level 3
```

This reads all files, excludes `.listing`, and estimates payload only. It is not
a NUP packer. The [NUP reference tools](https://github.com/a55uka/nup) provide
actual packing and verification; see [feasibility notes](docs/FEASIBILITY.md).

## Optional Switch 2 diagnostic build

The console app scans `sdmc:/games/<game>/`, supports the same direct/base layouts,
and checks folder presence and four-byte signatures only. Game files are read
only. **A appends an SD report**, so the application does perform that SD write.

- Up/Down: select; A: write report; X: rescan; +: exit.
- Report: `sdmc:/nx2backuploader-report.txt`.
- No native game launch, installation, NAND writes, ticket changes, or license work.

Install the [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) PC toolchain,
then run `build.bat` from this repository. It logs to `build.log`, verifies the
candidate, and writes `release/nx2backuploader.nro` only after format checks pass.
The shell builder also verifies before publishing and clears stale candidates on
failure. The Makefile supports incremental rebuilds after source changes.

The verifier bounds NRO segments, follows the declared MOD0/startup extensions,
and requires a Switch 2 OUN0 platform record with a supported version. It does
not authenticate the code or establish compatibility with your hardware/runtime.
This revision was PC-built and tested; it has **not been console-tested**.
Building this project does not require replacing any working console HEN files.

## Regression checks

PC tests (Python 3.10+ and Windows PowerShell or PowerShell 7; compression tests
require Python 3.14 and otherwise skip):

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

Scanner fault tests on a desktop compiler:

```sh
cc -Itests/host tests/scanner_tests.c -o scanner-tests
./scanner-tests
```

Or in a Visual Studio developer command prompt:

```bat
cl /nologo /W3 /D_CRT_SECURE_NO_WARNINGS /Itests/host tests/scanner_tests.c /Fescanner-tests.exe
scanner-tests.exe
```

These substitute filesystem/controller/console operations. They test application
logic and failures, not actual console I/O.

## History

- **0.3.0:** bounded platform-aware NRO verification; reliable write-error
  reporting; complete incremental rebuilds; checked paths; sorted scans with
  omitted/incomplete counts; `base/` layout support; PC backup, policy and
  compression diagnostics; regression tests and explicit scope.
- **0.2:** Switch 2 libounce toolchain correction.
- **0.1 (retired):** earlier build used Switch 1 libnx rules. A console crash was
  reported after running that build. The exact failure cause was not established
  by this project. Older outputs should not be treated as tested Switch 2 builds.
