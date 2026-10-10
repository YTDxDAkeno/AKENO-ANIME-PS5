#!/usr/bin/env bash
# AKENO STREAM PS5 - Build and run the host screenshot tool.
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Renders every main screen with canned data into build/screenshots/*.png.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source "$root/tools/ninja-build.sh"
cxx=$(command -v "${HOST_CXX:-clang++}")
cc=$(command -v "${HOST_CC:-clang}")
build="$root/build/screenshots-build"
ffmpeg=$(bash "$root/tools/setup-ffmpeg.sh" host)
bash "$root/tools/make-media-fixtures.sh"
mkdir -p "$build"
ninja_begin "$build/build.ninja"
read -r -a freetype_cflags <<< "$(pkg-config --cflags freetype2)"
read -r -a freetype_libs <<< "$(pkg-config --libs freetype2)"
common=(-O2 -g -pthread -DAKENO_HOST=1 -I"$root/src" -I"$root/third_party" -I"$root/third_party/prosperotv/include"
    -I"$root/third_party/prosperotv/src" -I"$ffmpeg/include" -I"$root/tests/host" "${freetype_cflags[@]}")
mapfile -t sources < <(find src -type f \( -name '*.cpp' -o -name '*.c' \) \
    ! -path 'src/platform/ps5/*' ! -path 'src/media/native/*' ! -name 'main.cpp' | sort)
sources+=(third_party/prosperotv/src/iptv_stream.cpp third_party/qrcodegen/qrcodegen.c
    tests/host/host_platform.cpp tests/host/host_web_view.cpp tests/host/software_sink.cpp tests/host/canned_api.cpp
    tools/screenshots/screenshots.cpp)
objects=()
for source in "${sources[@]}"; do
    object="$build/obj/${source//\//_}.o"
    if [[ $source == *.c ]]; then compiler=$cc; standard=-std=c11; else compiler=$cxx; standard=-std=c++20; fi
    ninja_inputs=("$root/$source" "$compiler")
    ninja_edge CXX "$object" "${compiler_cache[@]}" "$compiler" "$standard" "${common[@]}" -w \
        -MD -MF "$object.d" -c "$root/$source" -o "$object"
    objects+=("$object")
done
ninja_inputs=("${objects[@]}")
ninja_edge LINK "$build/akeno-screenshots" "$cxx" -pthread "${objects[@]}" \
    "$ffmpeg/lib/libavformat.a" "$ffmpeg/lib/libavcodec.a" "$ffmpeg/lib/libswresample.a" "$ffmpeg/lib/libavutil.a" \
    "${freetype_libs[@]}" -lcurl -lcrypto -lm -o "$build/akeno-screenshots"
ninja_run >/dev/null
AKENO_SOURCE_ROOT="$root" "$build/akeno-screenshots"
