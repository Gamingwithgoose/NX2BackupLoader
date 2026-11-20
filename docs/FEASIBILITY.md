# Native SD backup loading and NUP

Research snapshot: November 19, 2026. No native game-launch claim is made.

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
