NX2 Folder Execution Suite 0.3

New test: applies Yoshi relocations and surveys native startup capabilities.
It does not call Yoshi constructors, SDK startup or its game entrypoint on console.

Merge release/switch and release/games into the SD root. Leave Yoshi unchanged.
Open nx2-folder-exec through Pokopia override. Confirm Suite 0.3, then press A.
Share SD:/nx2-folder-exec-report.txt. Full mode normally performs 131 checks;
Y runs the same 102-check owned fixture suite. Kernel info return codes are
observations; unsupported queries do not count as failures.

What changed since the console-tested 0.2
- All six Yoshi modules are fully relocated in memory, then unloaded.
- A narrowly specified policy preserves the one zero anonymous TLS pair at
  subsdk0+0x1197b0, matching the observed original rtld. Exact hashes of all six
  source NSOs are required. Named, nonzero, template-backed or other unknown TLS
  descriptors still fail validation. This does not make that callback callable.
- Read-only GetInfo queries collect the process characteristics needed by the
  SDK, including types 29-38 missing from older headers. No target startup runs.

New evidence
- Suite 0.2 passed 129/129 checks on the user's console.
- PC execution of the original Yoshi rtld reached SDK startup in Unicorn.
- Resolving the original main module's 641 lazy imports allowed comparison with
  our eager linker: 2,691,120 relocation targets matched, with zero differences.
- 55 native PC regression checks passed, including 10 new preservation checks.
- The earlier 14 compiled ARM64 fixture checks passed. No fixture code changed.

Limits
The PC startup trace uses synthetic memory/process information, deterministic
entropy and TLS, accelerated memset, and modeled PAC returns without signature
validation. It stops at an unimplemented kernel request; it is not gameplay or
proof of a valid console runtime. The current console test only reads GetInfo,
maps/links our verified modules and writes its report. It does not replace HEN,
modify tickets, mount saves or route Yoshi's RomFS. It cannot launch Yoshi yet.

The old "missing TLS template" message described our strict loader policy.
It did not establish that Yoshi's native startup required this template. The
original relocation pass leaves that pair untouched. We preserve exactly that
observed behavior here and keep actual game startup disabled.

Source/build
Run build.bat to rebuild release/switch and release/games. Source is under
probes/folder-suite. Offline startup experiments and reproduction steps are in
probes/folder-suite/tests/STARTUP.txt. The combined probe is GPL-2.0-only, including
Atmosphere's ZBIC decoder; vendor/NOTICE, vendor/COPYING and source are supplied.
