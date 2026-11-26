# Ordered NUP assets and profile-preserving save adapter

2026-11-26. Source revision `20261126_ordered_nup_save_3`. Source-only compatibility attempts. No SDK compiler, assembler, linker, build, packaging, native test executable, install or push was run.

## Profile behavior in the reviewed reference sources

At nx2-flow commit `9ad10e2bebb0cf02082c413c06b0dcae25926014`, loader/runtime/game.c uses `save` by default or `saves/<slot>` selected by game.cfg's `save` setting. loader/runtime/rom.c routes save mounts into that chosen directory without selecting it from the account UID. At nx2-relink commit `b3e0fc4540784d45cd40dd9a97b1f4a5d0bacaad`, runtime/rom.c uses the port's fixed `save` directory. Three Switch users therefore do not automatically get three filesystem namespaces in these reviewed implementations. Explicit slots can keep saves separate, and a game may independently implement in-game slots. Neither is automatic Switch-user isolation. This statement concerns these pinned sources, not every future upstream revision.

BackupLoaderMKW keeps its existing UID-based primary binding and secondary profiles/<uid> paths, using the actual HBL selected UID where available or the explicit SDK UID. Missing UID still uses the existing legacy fallback. No save directories, binding markers, cache metadata or game data were moved, renamed, merged or deleted.

## Changes implemented

### Theme controls

On Options > Theme, A does nothing and leaves the dialog open. Only left/right changes and persists the theme. Other Options actions retain A selection. No new UI resource was added.

### NUP program selection and asset view

The previous reader selected one complete package and exposed its RomFS. It did not implement the reference's base/update asset layering, and archive record order could select an older update.

The new implementation separates program selection from assets. It chooses the highest-version available executable package for the anchored base application. Related update IDs accept the standard 0x800 patch bit or the same base ID used by some producers. Unrelated application updates and DLC packages are excluded. It never combines executable modules from different versions. An executable update without its own RomFS can inherit earlier applicable assets. Assets newer than the chosen program are excluded.

All applicable base and update RomFS roots through that program version are ordered by version, with archive index as the deterministic tie breaker. Path reads and index enumeration share those priorities: newest file wins, matching directories merge, and an overriding file hides older descendants. A newer directory replacing an older file starts above that file barrier. Strictly sorted directories retain binary lookup; unsorted lists retain linear lookup. Duplicate names within one directory retain first-entry behavior.

The walker groups child references by name/priority and uses temporary per-directory metadata during index preparation, freeing it as recursion unwinds. It does not copy asset contents or build a second full path table. The mounted layer table is at most one small record per already-supported package; ordinary reads use bounded stack scratch and the existing path cache. Cycles, overlong paths, allocation failures and existing depth limits remain errors. Table/checksum/extent/codec checks remain. All layer allocations are freed on mount failure and close. Folder discovery, indexing and reads are unchanged by this implementation.

Conceptual reference: nx2-flow loader/runtime/game.c ordered package preparation and nx2-relink tools/game2.py's newest program plus ordered file overlays. This is an original adaptation to the current reader rather than replacing all reference runtime ownership. Existing licensing/attribution remains.

### Host-current-application save identity

The native SDK can request the real running host's application ID, as the reference rom.c save adapter explicitly describes. Our account/device bridge accepted only zero/current or the selected guest ID and could reject such a current-process request as NX2_INVALID. This was a confirmed behavioral mismatch; the exact rejected attribute from Yoshi's run was not captured.

Preparation now retains the real kernel host ID. For native user-space account/device save requests, zero, the guest ID and that actual host ID are accepted as identities for routing into the selected guest's private SD tree. Other application IDs remain rejected. User UID handling remains intact; no host save service is opened and no success is fabricated. The authorization predicate is shared in profile_save.h. This is a universal correction attempt for a possible source of Yoshi's command-51 rejection, not a confirmed Yoshi fix.

## Validation performed

- Executed the actual production C ASTs for package selection and save-application identity in a narrow Python interpreter: 12 package-selection and 5 save-identity assertions passed. Cases include unordered versions, unrelated apps, patch IDs, DLC exclusion, inherited RomFS, incomplete packages, standalone update flags, current/guest/host identity and foreign rejection. This is not native C execution or kernel validation.
- Read the user's existing PC NUP package tables with the independent reference parser. PLZA: program package 1, two layers, 23 winning files; one inherited file absent from the former update-only view. Mario Tennis: program 1, two layers, two winning files; no newly inherited path. Yoshi: program 0, one layer, 147 files; no newly inherited path. These counts validate archive content/model only, not execution of our new C walker.
- Added test_nup_layers.c source fixture for inherited/overwritten files, unsorted children, lookup/walk agreement, file/directory barriers and cycles. Updated the native-read fixture to match the prior 1 MiB chunk ceiling and layer-backed mount. Expanded profile predicate fixture. Fixture grammar checks passed; fixtures were not compiled or executed.
- Existing Python grammar/source-wiring checks passed for GUI, runtime loop, complete host, NUP reader and launch declarations. These checks strip preprocessing/GNU extensions and stub platform declarations; they are not SDK type/link checks. Whitespace checks passed.

## What this does not resolve

This is not a complete replacement with nx2-flow's execution runtime. Its custom linking/module objects, measured arena ownership, applet/service adaptation, dynamic RO handling and older-NVN renderer have not been integrated here. The confirmed PLZA nvdrv 0x1015 failure requires a real compatible graphics backend/native program; an extra inherited file does not resolve it. Mario Tennis and Yoshi do not gain paths from layering, so their crashes cannot be claimed fixed by it. OOT/Bananza freezes and Xenoblade's mapping failure remain unverified. No new diagnostic layer or per-title runtime ID/offset/hash workaround was added.

## Build/test and rollback

User runs build.bat and copies the complete packaged switch directory as before. Both folder and direct NUP entry remain. Preserve the existing saves, profile binding markers and generated caches. Check working titles first; test Yoshi's existing save-slot action, then folder/NUP variants independently. Confirm source revision ...save_3 in the new report.

The pre-edit source snapshot and before/after hashes are under `.work/compat-integration-20261126/`. Restore only the listed changed files and remove the new test_nup_layers.c to undo this pass; do not replace unrelated earlier GUI/runtime changes. No successful console outcome is recorded for these edits.
