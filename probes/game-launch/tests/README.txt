Current release: Folder Launcher 0.13

See pc-validation.txt and the twelve *013.json linked-ELF result files, plus
folder-index-013.json. The generic resolver is exercised directly on Yoshi,
Zelda, MKW and Kirby decoded modules with emulate_general_prepare.py --images
<decoded-fixture> --worker <current-ELF> --out <result.json>. Original Nintendo
modules/snapshots are intentionally not distributed. Error formatting is modeled
in damaged-wrapper cases; the real parser/resolver/writer/refusal code executes.
The legacy working Yoshi sites are a test fixture in legacy-yoshi-hooks.json;
production hook locations are resolved dynamically and have no fixed offsets.
Native boot/guard emulator fixtures now also invoke the actual generic resolver.
Their old fixed PC milestones apply only to their original Yoshi fixture.

folder_check.c compiles with source/folder.c and source/rom_view.c on Windows;
folder_check --catalog <games-root> checks discovery. With <romfs-root> <map-out>
it runs the same index builder and validates the resulting view. It does not
copy assets. Windows directory enumeration does not prove console enumeration.
The non-menu SDK preflight worker is retained in the ELF solely for the existing
SDK filesystem ABI regression tests. Runtime launch no longer needs the old
Yoshi sample paths or generated romfs.map. The unconditional watchdog was removed.

Historical instructions and result files below describe earlier builds:
Current release 0.12: eight *012.json files match the current ELF. See pc-validation.txt.
emulate_rpc_wait.py exercises actual linked RPC state/sleep handling and the independent
watchdog with modeled kernel calls. Other emulator schedulers service bridge replies
at bounded-sleep calls; no true scheduling or wake-event state is emulated.
Historical instructions/results below are retained as development evidence.
0.11 regression runs the linked allocator initializer through its normal heap-marker mutation; only mimalloc options/arena registration are modeled. Old 0.10 rejects this valid state. See pc-validation.txt.

Current release: Native VM Launch Test 0.11

emulate_vm_release.py checks the actual linked heap-transition helper and host
allocator against captured MKW geometry, plus ten modeled failure scenarios.
emulate_vm_syscalls.py checks native GetInfo and Map/UnmapPhysicalMemory adapters,
kernel error propagation, alias bounds and blocked legacy heap resizing.
emulate_native_vm_guard.py executes original Yoshi startup past its prior exit
check to SDK AllocateAddressRegion entry. It stops before the allocation body.
Use --help for paths; provide --worker with the current linked ELF. The syscall
and guard tests also require the original decoded images and SDK snapshot used
by the bridge tests below. mkw-kernel-model.json combines captured MKW queries
with explicitly modeled uncaptured replies; post-release accounting is modeled.

The seven *011.json files and pc-validation.txt describe the current binary.
Actual console allocation and title-screen boot remain unverified.
Historical descriptions follow; their result files are not current hardware proof.
NX2BackupLoader Launch Test 0.8 PC validation

host_checks.c compiles with a standard C compiler alongside source/{path_guard,
ipc_policy,rom_view,patch,heap_plan}.c. Run the executable with generated/romfs.map.
It checks path traversal/length rejection, bounded HIPC/CMIF/TIPC decoding,
FSP command policy, malformed indices, >2GiB offsets and gate/trampoline bounds.

The emulator tests require Python + Unicorn 2.1.4, a decoded images directory
from ../../folder-suite/tests/export-startup.cjs, the C-linker SDK snapshot generated
by ../../folder-suite/tests/native_parity.c, and process-info-03.json from that suite.
No Nintendo executables or decrypted snapshots are supplied with this repository.

emulate_bridge.py executes the actual linked release ELF's bridge/worker plus
real game SDK filesystem code; it reads the user's own backup and writes only
the specified local scratch directory. emulate_native_launch.py uses original
unrelocated images and the actual shared C patch manifest, gates and worker,
then executes native rtld/CRT to the first unmodeled console service.
emulate_guards.py supplies synthetic offline SM replies to exercise actual
service adapters and handle tracking. It forwards no real service requests.

Use --help for required paths. Pass the freshly built nx2-folder-exec.elf as
--worker; these tests include its RELR relocation and ABI layout verification.
The .json results are retained PC evidence. They are not hardware reports.
PAC return targets, single-thread atomics and selected kernel replies are models.
No GPU, cache coherence or real multithread scheduling is emulated.

Heap fix validation: emulate_heap_startup.py needs only Python/Unicorn and the
linked ELF (--worker) plus --out. It executes the strong startup override and
libounce page allocator with the exact 0.5 hardware heap bounds. The reserved
game range stays unmapped in this test, so accidental touches fail. It covers
allocator boundaries, 64 host allocations/frees, oversized requests and small/
missing override cases. Pure heap planning also runs in host_checks.c.

Hardware 0.5 evidence is in hardware-launch-05.txt; its file/save checks passed,
but game entry was not attempted because allocation failed. Historical PC 0.5
results are under history-05/. Version 0.6 is not yet hardware-tested.

Fault capture: emulate_fault_capture.py takes --worker (linked ELF) and --out.
It needs Python/Unicorn and no game assets. A synthetic kernel exception packet
passes through the actual linked libounce decoder/ReturnFromException path into
the handler. It checks registers/status, guest TLS and first-fault preservation.
Actual kernel delivery is modeled and cannot establish hardware catchability.

Hardware 0.6 evidence is hardware-launch-06.txt. The heap fix worked; startup
reached SDK module initialization, then the process closed. Version 0.7 adds
diagnostics for this unexplained closure. The emulator startup test now enforces
game segment memory permissions. Historical PC 0.6 evidence is in history-06/.

Wrapper regression: emulate_guards_scrubbed.py uses the same arguments as the
guard test and deliberately clears volatile registers after modeled kernel
calls. It fails against the archived 0.7 ELF at FSP handle tagging and passes
against 0.8. emulate_syscall_registers.py additionally checks GetInfo output,
user-buffer transport, async event output, and the Break diagnostic pointer.
The kernel replies/scrubbing are fixtures; they are not hardware state dumps.
Service-name and CMIF/TIPC reply-result decoding are checked in host_checks.c.
Historical 0.7 evidence and the exact hardware report are retained.

Current 0.9 test: X reports actual host memory resources without game execution.
The original game memory guard is reproduced by emulate_startup_memory_guard.py
with the same arguments as the native startup script. With captured Pokopia
Info16=0, it observes the main exit import with status1. Only OS init precedes
this focused check; full SDK init was already demonstrated on hardware. The
new host report must establish a candidate's live resources/physical headroom.
