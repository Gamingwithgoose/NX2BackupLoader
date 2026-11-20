NX2 Folder Execution Suite 0.4 -- Native Yoshi SDK OS test

This is a new native-code test. It calls Yoshi's actual SDK OS initializer and
runtime functions from the verified SD backup, on a dedicated worker thread.
It does not call the game entrypoint or full SDK initializer.

How to run
1. Merge release/switch and release/games into your SD root, replacing the NRO.
2. Keep your existing Yoshi folder unchanged.
3. Enter hbmenu through Pokopia override and open nx2-folder-exec.
4. Confirm Suite 0.4. Press A for the full native SDK test.
5. Share SD:/nx2-folder-exec-report.txt.
Y runs only the previously proven owned fixtures. PLUS exits when idle.
A normal full run now performs 152 checks; fixture-only mode performs 102.

What is new
After the existing hashes, mappings and relocations pass, this build:
- Resolves 11 actual SDK functions from the verified Yoshi SDK image.
- Creates a 1 MiB test worker on a different allowed CPU from the current host.
- Calls nnosInitialize using that worker's real kernel thread handle.
- Verifies the SDK's current-thread object and thread ID against the kernel.
- Allocates an SDK TLS slot, checks its initial value, writes/reads a cookie,
  clears it and frees it through the SDK.
- Initializes a nonrecursive SDK mutex, checks first lock, rejected recursive
  try-lock, unlock/relock and finalization. The lock checks are nonblocking.
- Restores the host thread-pointer register and SDK-owned TLS region before
  returning to libounce, then joins/closes the worker before unloading code.

The worker makes no host libc/libounce calls while using SDK thread-local state.
The main host thread logs progress and waits up to 15 seconds. On a timeout or
reported context fault, it ends the Pokopia process before unmapping active code.
This is not memory/process sandboxing. A native CPU fault can still terminate the
application; the watchdog does not catch every crash or guarantee console safety.
Hardware verification received 2026-11-20: 152/152 checks passed on the user's Pokopia host, including SDK OS initialization, TLS/mutex checks and context restoration. This establishes the selected path, not complete game startup. The report is flushed before execution.

Scope
This executes real game-supplied SDK code; it is no longer only a map/link test.
Only the specified OS/TLS/mutex functions are called. It does not initialize
services, graphics or the complete SDK, call game constructors, route RomFS, mount
saves, alter tickets or replace HEN. Reaching gameplay still requires that work.
The one anonymous TLS descriptor is preserved as established in 0.3 and is not
invoked by this test.

PC validation
- The actual compiled sdk_os.o worker was linked into an ARM64 test image and
  exercised against the C-linked Yoshi SDK under Unicorn.
- Normal execution passed OS init, thread identity, TLS and mutex checks, and
  preserved a nonzero host TLS sentinel and thread-pointer register.
- Three negative paths passed: busy preemption state refused SDK entry, invalid
  SDK thread object restored context, and failed TLS allocation skipped slot use.
- Only GetInfo, QueryMemory, GetThreadPriority and GetThreadId were requested by
  the successful emulated path. Calls were modeled, never forwarded to a console.
- Kernel replies, stack/TLS, atomic ordering and PAC returns have model limits;
  this is not hardware proof. See tests/SDK-OS-TEST.txt and captured JSON results.
- All 55 native host regression checks passed. All 2,691,120 relocation targets
  still matched the original loader's resolved results.

Source is in probes/folder-suite/. Run build.bat to rebuild this release layout.
The combined probe is GPL-2.0-only. Decoder provenance and source are retained in
vendor/NOTICE and vendor/COPYING. No decrypted game bytes are distributed.
