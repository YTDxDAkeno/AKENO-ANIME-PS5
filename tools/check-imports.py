#!/usr/bin/env python3
# AKENO STREAM PS5 - Import binding guard.
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reproduces how tooling/native binds the linked executable's imports: each
# undefined dynamic symbol goes to the first NEEDED module whose stub exports
# it. Writes the table to build/imports.txt and fails when an import lands in
# a module that does not work in a native title (libScePosixForWebKit's
# functions resolve to null there - a call through one crashes or misbehaves).
"""Check the PS5 executable's import bindings."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SDK_LIB = ROOT / ".deps/native/ps5-payload-sdk/target/lib"
EXTRA_STUBS = ROOT / "build/import-stubs"
FORBIDDEN = {"libScePosixForWebKit": "its functions are not available to native titles"}


def readelf() -> str:
    for name in ("llvm-readelf-18", "llvm-readelf", "readelf"):
        path = shutil.which(name)
        if path:
            return path
    raise SystemExit("readelf is required")


def run(tool: str, *args: str) -> str:
    return subprocess.run([tool, *args], check=True, capture_output=True, text=True).stdout


def soname_and_needed(tool: str, path: pathlib.Path) -> tuple[str, list[str]]:
    soname, needed = "", []
    for line in run(tool, "--dynamic", "--wide", str(path)).splitlines():
        if "(SONAME)" in line or "(NEEDED)" in line:
            value = line[line.index("[") + 1 : line.rindex("]")]
            value = value[: -len(".sprx")] + ".prx" if value.endswith(".sprx") else value
            if "(SONAME)" in line:
                soname = value
            else:
                needed.append(value)
    return soname, needed


def dynamic_symbols(tool: str, path: pathlib.Path, defined: bool) -> set[str]:
    names = set()
    for line in run(tool, "--dyn-syms", "--wide", str(path)).splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        bind, ndx, name = fields[4], fields[6], fields[7].split("@")[0]
        if not name or bind not in ("GLOBAL", "WEAK"):
            continue
        if (ndx != "UND") == defined:
            names.add(name)
    return names


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: tools/check-imports.py build/eboot.elf", file=sys.stderr)
        return 2
    tool = readelf()
    # The converted executable keeps no ordinary symbol table; check the
    # linker's output that the converter consumed.
    linked = pathlib.Path(sys.argv[1]).with_name("llvm-pie.elf")
    if not linked.is_file():
        print(f"missing {linked}; build the app first", file=sys.stderr)
        return 2
    stubs: dict[str, set[str]] = {}
    for directory in (EXTRA_STUBS, SDK_LIB):
        for path in sorted(directory.glob("*.so")) if directory.is_dir() else []:
            soname, _ = soname_and_needed(tool, path)
            if soname and soname not in stubs:
                stubs[soname] = dynamic_symbols(tool, path, defined=True)
    _, needed = soname_and_needed(tool, linked)
    imports = sorted(dynamic_symbols(tool, linked, defined=False))
    table, problems = [], []
    for name in imports:
        provider = next((module for module in needed if name in stubs.get(module, set())), None)
        module = provider.split(".")[0] if provider else "UNRESOLVED"
        table.append(f"{name} {module}")
        if provider is None:
            problems.append(f"{name}: no NEEDED module exports it")
        elif module in FORBIDDEN:
            problems.append(f"{name}: binds to {module} ({FORBIDDEN[module]})")
    report = ROOT / "build/imports.txt"
    report.write_text("\n".join(table) + "\n", encoding="utf-8")
    modules = sorted({line.split()[1] for line in table})
    print(f"{len(imports)} imports from {len(modules)} modules: {', '.join(modules)}")
    print(f"Binding table: {report.relative_to(ROOT)}")
    if problems:
        print("Import check failed:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
