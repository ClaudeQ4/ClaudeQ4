#!/usr/bin/env python3
"""
The MSVC sysroot (Windows SDK + MSVC CRT/STL) has every header and library name in lower
case, but Windows is case-insensitive and JUCE / our sources sometimes write <Windows.h>,
"D2d1.lib" etc.  This creates symlink overlays for every mixed-case name the sources use.
usage: make_case_overlay.py <sysroot> <overlay-dir> <source dirs...>
"""
import os, re, sys, pathlib

sysroot, overlay = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
inc_root = sysroot / "include"
lib_root = sysroot / "lib" / "x86_64-unknown-windows-msvc"
inc_out, lib_out = overlay / "include", overlay / "lib"
inc_out.mkdir(parents=True, exist_ok=True)
lib_out.mkdir(parents=True, exist_ok=True)

inc_re = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
lib_re = re.compile(r'#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"([^"]+)"', re.I)

incs, libs = set(), set()
for d in sys.argv[3:]:
    for p in pathlib.Path(d).rglob("*"):
        if p.suffix.lower() in (".h", ".hpp", ".c", ".cpp", ".cc", ".cxx", ".inl", ".rc") and p.is_file():
            try:
                t = p.read_text(errors="ignore")
            except Exception:
                continue
            incs.update(inc_re.findall(t))
            libs.update(lib_re.findall(t))

n_inc = n_lib = 0
for name in sorted(incs):
    if name == name.lower():
        continue
    target = inc_root / name.lower()
    if target.is_file() and not (inc_root / name).exists():
        link = inc_out / name
        link.parent.mkdir(parents=True, exist_ok=True)
        if not link.exists():
            os.symlink(target, link)
            n_inc += 1

for name in sorted(libs):
    fname = name if name.lower().endswith(".lib") else name + ".lib"
    target = lib_root / fname.lower()
    if fname != fname.lower() and target.is_file():
        link = lib_out / fname
        if not link.exists():
            os.symlink(target, link)
            n_lib += 1
    elif not target.is_file():
        print("note: library not in sysroot:", name)

print(f"overlay: {n_inc} header links, {n_lib} library links")
