# Separate-process experiment 1

This is the first capability test for a different launch architecture. It
**does not launch a game yet**. It never silently uses the normal launcher.
The existing BackupLoaderMKW executable and logs stay separate.

Copy `release/switch/BackupLoaderProcessExperiment` to `SD:/switch/`.
Open **BackupLoader Process Experiment** from your usual homebrew menu,
select OOT, and press A. The selected title is recorded as context; the
child payload is a tiny independent kernel process that calls ExitProcess.
Its permissions are deliberately limited to that operation. The test does
not give a child the game's NPDM, filesystem, save files, or services.

Send both files from the SD root:

- `BackupLoaderMKW-process.log`
- `BackupLoaderMKW-process-crash.log`

The report distinguishes unavailable permission hints from actual kernel
call results, records a distinct child PID, maps and protects its code,
starts it, observes state changes, and attempts cleanup. Required privileged
calls are not executed unless advertised by hbloader. Missing hints mean
this route is not advertised; they do not prove a direct kernel rejection
or rule out a privileged broker. A child test pass does not prove retail
game launching, normal application registration, or filesystem routing.

A failed cleanup disables further trials until the host title is closed
and restarted. Successful trials need no reboot. To roll back, use the
existing BackupLoaderMKW entry; it is not replaced. This build writes no
save data, title registration, system settings, or kaiHEN files.

Current boundary: installed kaiHEN protocol 7 exposes FS/loader roots,
the caller's process handle, an SM proxy, and plugin access, but no public
arbitrary-folder process-launch or application-registration command. Its
bootstrap's native PM launch code uses a separately supplied PM session.
A full backend needs a permitted process creation/broker route, native
module loading, a child filesystem backend for the selected folder/NUP and
private saves, child service access, and application lifecycle/graphics
registration. Those pieces are not implemented by the self-test.

Build on Windows with `./build-process-experiment.ps1`. It compiles against
the installed Switch 2 libounce, produces a separately named NRO, validates
its startup/segments and embeds distinct menu metadata. Normal build and
normal release outputs are not overwritten. The only change to the shared
packaging helper adds an optional menu title with its existing default.

Host tests: compile `source/process_trial.c` and `tests/test_process_trial.c`
with `tests/stubs` as an include directory, then pass a scratch output log
path to the resulting executable. Tests cover permission gating, payload,
distinct PIDs, syscall failures, timeout, cleanup, and failed-cleanup status.
Hardware behavior remains unverified until a console log is captured.

ABI references inspected for this experiment:

- [kaiHEN homebrew interface](https://github.com/ouncebrew/kaihen/blob/main/hen/README.md)
- [kaiHEN native PM launch](https://github.com/ouncebrew/kaihen/blob/main/bootstrap/source/pm_launch.hpp)
- [CreateProcess parameter](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libvapours/include/vapours/svc/svc_types_priv.hpp)
- [Capability descriptor layout](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/include/mesosphere/kern_k_capabilities.hpp)

The ABI references help construct a bounded experiment; they do not establish
that the Switch 2 environment grants those operations or accepts the ABI.
