NX2 Folder Execution Test 0.1

This is an executable loader-stage test, not a Yoshi launcher.
It is PC-built, checked and not yet console-tested. An execution/mapping failure
may close the Pokopia host process. No working HEN files need to be replaced.

Merge the ZIP's switch/ and games/ folders into the SD root:
  SD:/switch/nx2-folder-exec/nx2-folder-exec.nro
  SD:/games/nx2-exec-fixture/module.nxm

Leave Yoshi where it is. This test does not change or invoke Yoshi's files.
The fixture is our small compiled test module, not Nintendo game code.

Run:
1. Enter hbmenu through your usual Pokopia title override.
2. Open nx2-folder-exec.
3. Press A once. The app will create /nx2-folder-exec-report.txt.
4. Expected result: first=4702, second=4703, unload=00000000.
5. Exit with PLUS, then share the report.

What it does:
- Requires the Pokopia host program ID observed in your earlier report.
- Reads a bounded, SHA-256-whitelisted fixture from the games/ folder.
- Uses the existing borrowed ldr:ro session's LoadNro/UnloadNro operations.
- Maps executable, read-only and writable segments with the existing RO server.
- Resolves and applies the fixture's AArch64 GLOB_DAT relocation into writable data.
- Checks mapping permissions, calls nx2_module_entry twice, and verifies its state.
- Unmaps the module before freeing backing memory. If mapping/unmapping fails,
  it flushes the report and ends the host process rather than freeing mapped pages.

It does not change global filesystem roots, resolver paths, tickets, licenses,
saves, runtime files or NAND. Its only SD write is the report above.

Why a .nxm fixture?
The original module's virtual offsets are retained behind a 4 KiB NRO mapping
envelope, so the standard kaiHEN RO mapper supplies RX/R/RW memory permissions.
The envelope is a project-local test format, not a standard retail-game file.
The test calls an explicitly named fixture function; it does not invoke the NRO
envelope as a normal homebrew application or pretend it is a retail entrypoint.

Current loader limitations:
- One self-contained module, local/weak symbols, and RELA relocation types
  ABS64, GLOB_DAT, JUMP_SLOT and RELATIVE. Other formats fail before execution.
- No TLS descriptor support, multi-module retail initialization, game services,
  RomFS redirection or save isolation. Therefore this is NOT a retail launcher.

PC results for Yoshi:
All six modules decompressed and passed their requested segment-hash checks using
the upstream ns2jb-modloader decoder. No unmatched strong import names were found
across the supplied modules. Planned module address span is roughly 252 MiB.
One module has a TLS descriptor relocation (type 1031). That and the remaining
retail startup/filesystem environment must be implemented before entering Yoshi.

Build source:
Run make with ounce-devkit installed. The fixture uses the included custom linker
script and fixture/package.py; its compiled hash is embedded in fixture_hash.h.
The included core tests can run on a desktop C compiler without console access.
No upstream implementation code is included in this execution-test source.
