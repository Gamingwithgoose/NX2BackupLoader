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
