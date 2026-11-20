PC regression checks (Suite 0.2)

Native checks were built with LLVM clang-cl and the Visual Studio 2022 x64
headers/libraries. In an x64 Visual Studio developer shell with clang-cl on PATH,
run from this probe directory:

clang-cl /O2 /MD tests/core_test.c source/module.c /Fe:core_test.exe
core_test.exe tests/legacy-module.nxm
clang-cl /O2 /MD tests/group_test.c source/module.c /Fe:group_test.exe
group_test.exe fixture/provider.nxm fixture/client.nxm
clang-cl /O2 /MD /D_CRT_SECURE_NO_WARNINGS tests/nso_test.c source/module.c source/nso.c source/zbic.c /Fe:nso_test.exe /link bcrypt.lib
nso_test.exe tests/generated-nso.bin
clang-cl /O2 /MD /D_CRT_SECURE_NO_WARNINGS tests/real_modules.c source/module.c source/nso.c source/zbic.c /Fe:real_modules.exe /link bcrypt.lib
real_modules.exe "path/to/Yoshi/base/exefs"

The last check requires the user's own files, which are not distributed.
It expects one unsupported anonymous TLS layout, with no missing strong symbols.

ARM64 emulation requires Python and unicorn 2.1.4. After compiling the owned
fixtures (fixture/build-msys.sh), compile the actual resolver in a devkitPro shell:

aarch64-none-elf-gcc -nostdlib -shared -Wl,-T,fixture/module.ld -Wl,-Bsymbolic -Wl,-z,max-page-size=4096 source/tlsdesc.S tests/tls_stub.S -o tests/tls-trampoline.elf
python tests/emulate.py

The emulator runs the compiled fixture code and TLS resolver assembly. It
simulates the C host TLS lookup callback. It does not emulate Nintendo software
or establish hardware compatibility. Captured PC results are supplied here.