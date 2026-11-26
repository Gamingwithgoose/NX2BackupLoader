# Compatibility batch: integration decision

2026-11-26. Reviewed all 24 files in `release/switch/Logs` across six test directories. All six gameplay reports identify revision `20261126_storage_profile_pool_2`, compiled Nov 26 2026 11:44:25. Input hashes are in compatibility-batch-20261126-manifest.json. No runtime source was changed, no extra diagnostic layer was introduced and no build was performed in this review.

## What these reports establish

| Title | Captured behavior | Limits of interpretation |
| --- | --- | --- |
| Yoshi | First present at 1464 ms. Virtual FSP command 51 (OpenSaveDataFileSystem) returns our NX2_INVALID (0x2f5e02), immediately followed by an SDK break carrying exactly that Result. | This is a loader/private-save bridge rejection. The exact failed attribute/layout/identity/profile validation is not identified by these records; do not guess which check to disable. It is not evidence of a corrupt save. |
| PLZA | Real SDK ConnectToHipcService for nvdrv returns 0x1015, then the same CreateNvDrvByHipc assertion identified from the prior executable analysis. | Confirms the previously missing numeric failure. Does not establish that a driver alias or GSV handle can be substituted compatibly. |
| OOT | First present at 3387 ms, 650 presents, then 8-second pause. storage_slot=0, storage_age_ms=0. Successful appletOE completions remain in the tail. | This run does not support an active blocked storage request. Earlier OOT storage-stall evidence is from a different run and must not be generalized. Missing caption/option files return real not-found and are not by themselves the freeze cause. |
| Bananza folder | First present at 63549 ms, 5100 presents, then 8-second pause with storage_slot=0. | Long first-frame time and later freeze are separate observations. No active storage request at this snapshot. Does not prove GPU deadlock or a specific kernel kill reason. |
| Xenoblade | MapPhysicalMemory of 1 MiB returns real kernel Result 0x10801; first present nevertheless occurs at 12499 ms. Guest declares 96 MiB bookkeeping pool against host 64 MiB. | A real mapping failure exists, but the subsequent present means that failure alone is not proof of the terminal cause. Do not raise/spoof reported host quotas. |
| Mario Tennis | Successful preparation and main-thread handoff; last service failures are optional-looking pctl:s/a requests with 0x1015. No first-present/break/terminal reason recorded. | Those pctl failures also occur in titles that present successfully. They do not establish the startup crash's cause. |

All six raw sidecars are captured=false. Mario Tennis, Bananza and Xenoblade host-end files contain preparation headers only. Do not invent a CPU exception or termination event from those files.

## Why selective copying has not reached parity

The prior architecture comparison already identified functionality that remained unimplemented: an ordered asset union across base/intermediate/latest updates, measured arena ownership/trim, and the module merge/RO ownership strategy. Native startup has also diverged: ours hands the original rtld its mapped NSOs, whereas the reference custom linker constructs module objects and owns relocations, initialization and some thread-local state. Mixing those owners can double-initialize or mis-own state. Reference save IPC handling and our profile-preserving private VFS have different object/namespace models. The legacy NVN renderer is a separate backend with documented API/shader limits, not a service-name replacement.

These are integration constraints, not a demonstrated fundamental prohibition on reuse. The working behavior must be integrated as coherent components with explicit adapters for our folder/NUP entry, existing saves/profile metadata and cache format. Earlier small hook/resource/diagnostic changes did not implement those components; they must not be described as equivalent to merging the working runtime.

## Practical course

For immediate gameplay, using the reference tool that actually runs the user's titles is reasonable. Keeping BackupLoaderMKW's source does not require continuing to spend tests or usage on it now. Preserve its existing saves/metadata; reference migration/overwrite of those directories is not authorized by this review.

If continuing development, prioritize a coherent reference-runtime integration over further diagnostic expansion: retain the existing frontend/direct launch request, adapt runtime ownership and the save backend, port ordered file layers, then validate one fixed build against working Star Fox/Fast Fusion plus the failed save/transition workloads. An isolated integration backend would make comparison and rollback possible without deleting the current runtime. This is a proposed engineering direction, not implemented code or a promise of playability. It still needs folder support and save/profile adapters; simply replacing files from the reference would not preserve those mandatory requirements.

The evidence does not support deciding which model is generally better from which model an upstream author reportedly used. The engineering outcome here has not met the user's needs. No guarantee of compatibility or reason to spend more on that guarantee is offered.

References: runtime-architecture-review-2026-11-26.md (pinned nx2-flow/nx2-relink sources), plza-driver-startup-1795713343.md, runtime-followup-1795712522.md, and the six original log directories above. The supplied logs were only read and hashed.
