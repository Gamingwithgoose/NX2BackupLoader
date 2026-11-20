# Current evidence: general folder launcher 0.13

On November 20, 2026, the owner confirmed that 0.12 reached Yoshi's menus and
actual gameplay from a decrypted SD folder through the MKW override. Its report
shows continued presents/asset reads until the deliberately armed 180-second
cutoff. This establishes that this owned backup can execute in that environment;
it does not establish universal title compatibility or stock-firmware support.

0.13 removes the gameplay cutoff and replaces the title-specific entry path with
a /games browser, dynamic module/hook resolution, automatic directory indexing,
a bounded file cache and separate private save directories. Preparation passes
for Yoshi, Zelda, MKW and Kirby fixtures; only the previous Yoshi build has the
reported hardware gameplay proof. No ticket/ownership database changes occur.
See the current root README and probes/game-launch/tests/pc-validation.txt.

The remaining text is historical research and earlier experiment notes. Statements
below that no native boot had been shown refer to their earlier date/build and
are superseded by the evidence above. NUP work remains outside the current scope.

---
# Native SD backup loading and NUP

Research snapshot: November 20, 2026. No successful native game boot is claimed.

## Current experiment: Launch Attempt 0.5

The root build now targets an actual Yoshi demo launch attempt under the verified
Pokopia override. It uses a metadata-only RomFS image backed by loose SD files,
a private SD save filesystem, a private heap, and the original rtld/CRT/SDK
startup. It releases the homebrew display before native graphics initialization.
There is no ticket, signature or ownership database modification.

The actual linked ELF passes offline native-SDK file reads and private-save I/O.
Its original loader completes relocation and OS initialization and reaches SDK
module initialization before the first unmodeled console service connection.
A host-loop FSP policy stops requests outside initialization and a read-only log
query; it is not a general sandbox for all OS services. Startup/GPU compatibility
and a rendered title screen remain unverified on hardware. See
`probes/game-launch/README.txt` and its retained PC evidence for scope and limits.
Earlier Suite 0.4 passed 152/152 checks on the user's console. That validates its
SDK OS subset, not the new complete SDK/game path. The sections below preserve
the wider feasibility analysis; statements about an existing launcher refer to a
validated capability, which this experimental entry attempt does not establish.

## Intended behavior

Use a legitimate host title's override process (for example Pokopia), retain that
host identity, and run a different owned backup from SD. This is a research goal;
it is not an existing feature of NX2BackupLoader.

## Established upstream behavior

[kaiHEN 20261117.1's HEN documentation](https://raw.githubusercontent.com/ouncebrew/kaihen/20261117.1/hen/README.md)
describes starting hbloader in place of a native game, after caching that game's
signed policy. Its documented homebrew API exposes filesystem/service sessions,
the caller's process handle, plugins, and the loader filesystem root. It does not
document a call to launch an arbitrary extracted retail game folder.

The [ns2jb mod loader](https://github.com/SebaaMG/ns2jb-modloader) documents SD-backed
RomFS and ExeFS replacements, including complete executable files. Its documented
flow starts a corresponding installed or inserted title. This is evidence that
SD redirection is possible, not evidence that an arbitrary backup can run without
its installed title or under a different host identity.

## Engineering implications

Decrypted files avoid content-decryption work, but do not supply a native process
environment. A host-process design must still resolve executable loading and
relocation, memory/TLS/stack expectations, permissions/services, RomFS reads,
title-dependent APIs, and save-data identity. A homebrew NRO and a retail ExeFS
module set have different loading requirements. JIT support does not solve all
those requirements by itself. These are engineering inferences from the documented
interfaces, not a claim that such a loader is impossible.

Host/target NPDM comparison can identify declared differences. It cannot establish
the effective policy of an already running HEN process, full ACID restrictions,
or runtime compatibility. Saves must be isolated before any title-substitution
experiment; simply inheriting the host's identity could access its save data.
No save or runtime experiment is included here.

## NUP storage

The [NUP v1 draft specification](https://raw.githubusercontent.com/a55uka/nup/meow/SPEC.md)
defines a plaintext container for base/update/DLC trees, per-file hashes and shared
extents for identical files. Codec 1 is a whole-file Zstandard frame; seekable
chunked Zstandard is reserved for a future revision. NUP therefore addresses
packaging/compression, not native game authorization or execution.

A console reader would need bounded metadata parsing, decompression, verified
data, caching and virtual filesystem integration. Whole-file compression complicates
random reads of large game archives; a raw extent or bounded disk cache could be
useful tradeoffs. Do not invent an incompatible codec under the NUP v1 name.
The PC estimator in this repo measures zstd/dedup payload savings and never writes
a container. Exact container size and interoperability require packing with and
verifying against the reference implementation.

## Game Vault reference

The supplied Game Vault 0.4.5 source was reviewed as a reference. Useful design
patterns include separate content inventory, explicit metadata, bounded access,
checksums and incomplete/completed states. The PC diagnostics here implement their
own checks; no Game Vault code was transplanted.

Its `console/source/game_licenses.c` prepares existing installed rights when an
encrypted content mount requires them. Removing that call does not supply a
decrypted-folder loader. Its supplied README also says restoring content does
not restore licenses or guarantee playability.

The supplied experimental `support-source/nup-launcher/paired-provider` is not
evidence of a production loader: it explicitly limits fixture payloads to 8 MiB,
and references `sd_storage.hpp`, absent from the supplied source tree. Treat its
provider/transport experiments separately from demonstrated retail game launch.

## Current conclusion

Compressed offline storage is feasible today. SD redirection is demonstrated by
upstream work. Generic retail-game execution inside another title's override
process remains unproven for this project and the supplied runtime. There is no
console-tested launch candidate here. Confirming that goal requires additional
loader/filesystem work and controlled runtime evidence, not just ticket-check
removal or a successful build.

## Suite 0.3 startup work

The supplied Suite 0.2 console report passed 129/129 checks under Pokopia. All six
Yoshi modules decoded, matched hashes, mapped together and unloaded; owned test
code executed across modules and threads. Yoshi was not executed.

Offline execution of this Yoshi build's original rtld reached SDK startup in
Unicorn. The original relocation pass preserves the zero anonymous TLSDESC pair;
our earlier missing-template result was a strict policy restriction, not proof
that the native runtime needed a template for that descriptor. A narrowly scoped
preservation policy now supports applying the relocations for these exact images.
After resolving 641 lazy imports using the original rtld, all 2,691,120 relocation
destinations matched our eager C linker. See probes/folder-suite/tests/STARTUP.txt
for reproduction, limitations and the retained trace.

The emulator uses synthetic kernel memory/process information and TLS, models
PAC returns without validating signatures, and forwards no service calls. It
stops at the SDK's GetInfo type 29 request, which is unimplemented in the model;
this does not establish a console failure. Suite 0.3 collects actual read-only
process information and applies the verified modules' relocations on hardware.
It does not call native SDK initialization, constructors or the game entrypoint.
RomFS routing, save isolation and compatibility with the existing host's process
state remain unresolved. There is still no demonstrated Yoshi gameplay.

## Suite 0.4 native SDK OS test

The next console test calls the exact supplied Yoshi SDK's nnosInitialize on a
dedicated worker, then checks its current-thread identity, TLS allocation and
values, and nonrecursive mutex semantics. It saves/clears/restores the SDK-owned
TLR tail and restores TPIDR_EL0 before returning to the host runtime. The worker
is joined and closed before code mappings are released; the normal host thread
has a 15-second wait deadline. A timeout/context fault ends the process instead
of unmapping live code. This does not guarantee recovery from native CPU faults.

The final release worker object passed four ARM64 integration cases against the
C-linked SDK in Unicorn: normal, preemption-busy refusal, invalid thread object,
and failed TLS allocation. Host TLS sentinels and TPIDR_EL0 were preserved. The
successful modeled SDK path made only GetInfo, QueryMemory, GetThreadPriority
and GetThreadId calls. Kernel replies, PAC returns and atomic ordering retain
emulator limitations. No host service calls were forwarded. All 55 host checks
and all 2,691,120 relocation comparisons also passed.

The Suite 0.4 hardware report received 2026-11-20 passed all 152 checks. The actual supplied SDK OS initializer returned; its thread ID matched the kernel, TLS allocation/read/write/release and mutex tests passed, host TLS/TP state was restored, and the worker closed before all six modules were unmapped. The tested SDK OS path is now demonstrated on the console. The game entrypoint, complete SDK/service
initialization, RomFS routing, graphics and save isolation are not implemented
by this test. See probes/folder-suite/tests/SDK-OS-TEST.txt for reproduction.
