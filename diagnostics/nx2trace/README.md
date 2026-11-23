# NX2Trace (developer-only)
This optional, separately-running kaiHEN plugin records **events sent by NX2BackupLoader**, not every kernel/system process or exception. It does not install hooks, inspect other processes, request service privileges, or modify HEN itself.

- Private build: run `build-dev.bat`, which builds the normal NRO and then `nx2trace.nso`.
- Developer-only output: `release/developer/kaihen/plugins/nx2trace.nso` (NOT automatically installed).
- To use: copy that NSO yourself to `/kaihen/plugins/nx2trace.nso` **before starting kaiHEN**. Plugins start at HEN bootstrap, not when copied to SD during an active session.
- Event output: SD root `BackupLoaderMKW-trace.log`. Log capped at 1 MiB to avoid filling SD.
- If HEN fails to start after installation, remove the NSO from the SD card and boot normally. Do not repeatedly attempt an unstable HEN boot with the plugin installed.
- No start-time system probes: registration only. The plugin does not access FS until the NRO sends an event.
- The first version observes only milestones sent by the developer loader. A missing heartbeat does not prove the kernel killed a process.
- **Do not include `release/developer` in public packages.** `build.bat` is untouched.
- Unverified until compiled with ounce-devkit and exercised on hardware. Keep your prior known-good NRO and backup the SD files before installing.


## Safety and crash-evidence scope (2026-11-23)
This build is **PASSIVE**. The plugin does not query pm:dmnt, pm:shell, erpt:r, SM services, kernel process lists, or debugging SVCs. It does not consume system process events or modify any process. It merely registers its own official kaiHEN IPC listener and awaits developer-NRO events. It cannot independently determine the Horizon termination reason for a game.

Developer NRO emits these additional **real CPU fault** events only if libounce's exception handler captures them:
- event 8: actual `ThreadExceptionDump.error_desc`
- event 9: actual fault PC
- event 10: actual LR
- event 11: actual FAR (fault address)
- event 12: actual ESR (exception syndrome)

The values are hexadecimal in `BackupLoaderMKW-trace.log`. Events 1–7 remain launch milestones. These events are sent asynchronously and may be lost if the NRO is terminated immediately. The existing flushed `BackupLoaderMKW-crash.log` remains the better source for exceptions.

**Not supported:** External Horizon crash reason, software-closed popup result, GPU/kernel exception reporting, monitoring arbitrary apps. Getting those would require a separately verified privileged interface; do not mistake the current plugin for such a reporter. Never claim guaranteed safety or hardware validation. Do not reinstall kaiHEN simply to test this limited logger.
