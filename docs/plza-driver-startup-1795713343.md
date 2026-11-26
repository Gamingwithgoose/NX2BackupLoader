# PLZA same-product host test: driver service acquisition abort

2026-11-26. Diagnosis from supplied logs and read-only executable analysis. No title-specific code change, compilation, linking, packaging or native test execution.

## Confirmed evidence

User supplied `release/switch/PLZA host and PLZA backup/`, then identified the tested local archive as `D:/Roms/Switch 2/Pokemon Legends Z-A.nup`. Logs and source-before changes are preserved under `.work/plza-1795713343/`. Main/SDK entries alone were read into the analysis directory, their archive SHA-256 checksums verified, and their existing NSO segments decoded for read-only symbol/disassembly analysis. No complete game extraction, backup modification or game NRO was produced.

Host is `0400f43008c44001`; the selected backup is `0100f43008c44000`. Configured and requested system-resource pools are both 16777216 bytes. Preparation maps rtld/main/sdk successfully, releases the unused heap tail and reports 8434581504 available bytes. Original-main SDK handoff occurs. No FIRST_PRESENT appears. The SDK requests a reason-0 break; the loader then stops, and host-end records its svcExitProcess. Raw exception sidecar is complete with captured=false. This is an intentional SDK abort followed by launcher termination in the captured evidence, not a captured CPU exception or proof of a GPU hang.

The NUP contains a base program, version-393216 update and a content-only package. Base and update NPDMs both identify `0100f43008c44000`; both use the same SDK. The selected update's main is 33970422 bytes and SDK 6247676 bytes, matching the report exactly. SDK build ID is `df7a3960c74c3a4803f826c5871532523aa9cf2a0`; main build ID is `b1f12fd919eae86ab8a978317677e64bce443d1f0`. The content-only package has no ExeFS and contains one zero-byte dummy file. There is no alternate native Switch 2 ExeFS package in this container. A directory name containing “Switch 2” does not select a platform backend.

## Abort traced to its assertion

The eight instruction words at SDK offset 0x9c568 match SDK_BREAK_CODE in the supplied log exactly, confirming symbol/disassembly alignment. SDK offset 0x9c578 is nn::diag::detail::Abort(Result const*); the following generic frames are VAbortImpl/AbortImpl, so nearest exported-name guesses for much more distant frames must not be treated as function identification.

At the real caller, SDK offset 0x4532d8 branches to the SDK assertion thunk; its return address is the logged frame 0x4532dc. The argument-building instructions reference these embedded strings:

- expression: `result.IsSuccess()` (0x91ed3f)
- function: `CreateNvDrvByHipc` (0x90e474)
- source: `nvos_drv_DriverImplByHipc.cpp`, line 48

The immediately preceding operation calls `_ZN2nn2sf4hipc20ConnectToHipcServiceEPNS_3svc6HandleEPKc` through PLT 0x632b70 / GOT 0xb78d68, then branches to that assertion if the returned Result is nonzero. InitializeGraphics passes the service-name pointer stored at 0xb89178, resolving to the string `nvdrv` at 0x91589e. Thus **service connection failure is confirmed**; the numeric service/IPC failure Result is absent from these logs. SDK_BREAK_WORD=0 is the generic abort payload and must not be interpreted as that Result.

## Underlying compatibility gap and limits of the conclusion

This program takes the older Switch NVDRV/HIPC graphics initialization path. PLZA's SDK contains no GSV-named function exports. The working local Star Fox SDK contains 61 GSV-related function exports. Host and backup share a product name but do not execute the same platform program/SDK.

The Switch 2 path in ouncebrew/libounce `nx/source/services/nv.c` obtains an attached driver session with gsvInitialize/gsvOpenNvServices, clones it and sends feature command 13. It explicitly avoids the old SM lookup, transfer-memory Initialize command 3 and SetClientPID command 8. The older path uses SM nvdrv* and those older initialization operations. Fetched source blob SHA: `b26bce47a437a2c7351eea6f5696c11ec754aadf`; exact read-only reference is preserved in the analysis directory.

BackupLoaderMKW currently forwards that older service request without a legacy graphics compatibility backend. nx2-flow's runtime/nvn2.c introductory architecture comments explicitly describe replacing older NVN calls with an OpenGL ES backend because that older driver path is refused for homebrew on Switch 2. They also state limited API and shader support; this is not a universal ready-made adapter for arbitrary precompiled shaders.

The legacy/native backend mismatch is a source-supported explanation and a concrete architectural gap, beyond the confirmed failing connection. **The existing report does not identify the exact numeric SM refusal, prove that every legacy NVDRV connection fails on all hosts, or establish that PLZA becomes playable after any proposed graphics adaptation.** It does not establish a common cause for Yoshi, Bananza or OOT; their binaries/failing paths must be examined independently. Changing title IDs, blindly replacing nvdrv with a GSV handle, swapping driver service aliases or fabricating a successful connection would not prove protocol compatibility and was not implemented.

References:

- https://github.com/ouncebrew/libounce/blob/main/nx/source/services/nv.c (blob SHA above)
- https://github.com/Olmectron/nx2-flow/blob/9ad10e2bebb0cf02082c413c06b0dcae25926014/loader/runtime/nvn2.c
- Earlier three-repository comparison: runtime-architecture-review-2026-11-26.md.

## Universal source changes in this follow-up

1. Optional export-based ConnectToHipcService observer, using the exact SDK symbol and ABI, forwards the original output handle and Result without alias/retry/stub/permission edits. Only failures enqueue service name, real Result and caller into the existing bounded nonblocking diagnostic queue. The observer applies to any SDK exposing this symbol; no PLZA ID, code offset, file hash or function address appears in runtime logic.
2. SDK-break handling drains at most the bounded queued diagnostic capacity before termination. Previously the host only drained that queue on idle polls, so an immediate fatal request could overtake the preceding failure record. Existing queued save-mount notices are processed with their served counter preserved.
3. Fatal SDK-break reports include the last 32 existing in-RAM IPC breadcrumbs, including kernel and CMIF results and service name. No successful IPC disk logging or routine per-read writes were enabled. These are best-effort cross-thread observations. The failure queue remains sampled/drop-on-full; absence of a record is not success.
4. Source revision becomes `20261126_storage_profile_pool_2`. The supplied report predates these changes and the preceding follow-up: it contains neither the source-revision label nor REPORT_ARCHIVE/RESOURCE_CHECK_USED, so it does not test those new behaviors.

These changes improve diagnosis; they are **not a graphics compatibility fix**. Older NVDRV/NVN support remains work requiring real protocol/rendering compatibility, or selection of an available matching native program. No matching native ExeFS was found in this particular archive. Existing folder/NUP direct launching, saves, profile routing and earlier performance attempts are retained.

## Validation / rollback

Python grammar/source-wiring checks passed for runtime loop, entire host source, launch declarations, the SDK observer and existing GUI/NUP readers, with preprocessing/GNU syntax stripped and platform declarations stubbed. They are not type checking, compilation, linking or console tests. Read-only objdump was used on already-existing decoded executable bytes. Git whitespace checks passed. Six source files differ from the exact source-before snapshot in this analysis directory; change-manifest.json records before/after hashes. Restore those six files from source-before to undo only this diagnostic follow-up, preserving prior compatibility/UI work.

Next console evidence should preserve SDK_SERVICE_CONNECT_FAILED (or SDK_BREAK_PENDING) and SDK_BREAK_IPC for the same failing service. A build using these sources can confirm the numeric failure without enabling continuous verbose logging. No hardware improvement has yet been verified.
