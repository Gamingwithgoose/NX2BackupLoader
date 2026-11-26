"""Compile and run a desktop base/update integration fixture; no game content."""
import argparse
import pathlib
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--scratch-root", default=tempfile.gettempdir())
    args = parser.parse_args()
    project = pathlib.Path(__file__).resolve().parents[3]
    source = project / "probes/game-launch/source"
    scratch_root = pathlib.Path(args.scratch_root).resolve()
    scratch_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nx2-folder-union-", dir=scratch_root) as scratch:
        work = pathlib.Path(scratch)
        assert work.resolve().is_relative_to(scratch_root)
        game = work / "games/Test Game"
        base = game / "base/romfs"
        update = game / "updates/917504/romfs"
        files = {
            base / "arc/base.bin": b"base archive",
            base / "audio/music.bin": b"base audio",
            base / "common.bin": b"old base data with different size",
            base / "shared/keep.bin": b"unchanged",
            base / "dir_to_file/hidden.bin": b"must be hidden",
            base / "file_to_dir": b"must also be hidden",
            update / "common.bin": b"updated",
            update / "new.bin": b"new update asset",
            update / "dir_to_file": b"replacement file",
            update / "file_to_dir/new.bin": b"replacement directory",
        }
        for name, content in files.items():
            name.parent.mkdir(parents=True, exist_ok=True)
            name.write_bytes(content)
        for root in [game / "base", game / "updates/917504"]:
            (root / "exefs").mkdir(parents=True)
            for name in ["rtld", "main", "sdk"]:
                (root / "exefs" / name).write_bytes(b"fixture")
            npdm = bytearray(128)
            npdm[:4] = b"META"
            struct.pack_into("<I", npdm, 0x1C, 1024 * 1024)
            (root / "exefs/main.npdm").write_bytes(npdm)
        exe = work / "test_folder_union.exe"
        subprocess.run([
            args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-D_CRT_SECURE_NO_WARNINGS", "-D_CRT_NONSTDC_NO_DEPRECATE",
            '-DCACHE_DIR="cache"', "-I" + str(source), str(pathlib.Path(__file__).with_suffix(".c")),
            str(source / "folder.c"), str(source / "rom_view.c"),
            str(source / "index_cache.c"), "-o", str(exe),
        ], check=True, cwd=work)
        subprocess.run([str(exe), str(work / "games")], check=True, cwd=work)


if __name__ == "__main__":
    main()
