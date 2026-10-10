#!/usr/bin/env bash
# AKENO STREAM PS5 - Incremental host test build (GoogleTest, ASan/UBSan).
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles every portable module of the application for the build machine:
# everything under src/ except the PS5 platform layer (src/platform/ps5),
# the native media backend glue (src/media/native) and the console entry
# point. tests/host provides host stand-ins for platform services.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/tools/ninja-build.sh"
gtest=$(bash "$root/tools/setup-test-dependencies.sh")
cxx=$(command -v "${HOST_CXX:-clang++}")
cc=$(command -v "${HOST_CC:-clang}")
build="$root/build/tests"
ffmpeg=$(bash "$root/tools/setup-ffmpeg.sh" host)
bash "$root/tools/make-media-fixtures.sh"
mkdir -p "$build"
ninja_begin "$build/build.ninja"

read -r -a freetype_cflags <<< "$(pkg-config --cflags freetype2)"
read -r -a freetype_libs <<< "$(pkg-config --libs freetype2)"
sanitize=(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
common=(-O1 "${sanitize[@]}" -pthread -DAKENO_HOST=1
    -I"$root/src" -I"$root/third_party" -I"$root/third_party/prosperotv/include"
    -I"$root/third_party/prosperotv/src" -I"$ffmpeg/include"
    -I"$root/tests/host" -isystem "$gtest/googletest/include" -I"$gtest/googletest" "${freetype_cflags[@]}")
strict=(-Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)

mapfile -t app_sources < <(cd "$root" && find src -type f \( -name '*.cpp' -o -name '*.c' \) \
    ! -path 'src/platform/ps5/*' ! -path 'src/media/native/*' ! -name 'main.cpp' | sort)
mapfile -t test_sources < <(cd "$root" && find tests/unit tests/host -type f -name '*.cpp' 2>/dev/null | sort)
third_party=(third_party/prosperotv/src/iptv_stream.cpp third_party/qrcodegen/qrcodegen.c)

objects=()
compile() { # source, kind (app|test|vendor|gtest)
    local source=$1 kind=$2 object args compiler
    object="$build/obj/${source//\//_}.o"
    if [[ $source == *.c ]]; then
        compiler=$cc
        args=(-std=c11 "${common[@]}")
    else
        compiler=$cxx
        args=(-std=c++20 "${common[@]}")
    fi
    case $kind in
        app|test) args+=("${strict[@]}") ;;
        vendor) args+=(-w) ;;
        gtest) args=(-std=c++20 -O1 -g -pthread -isystem "$gtest/googletest/include" -I"$gtest/googletest") ;;
    esac
    [[ $source == /* ]] || source="$root/$source"
    ninja_inputs=("$source" "$compiler")
    ninja_edge CXX "$object" "${compiler_cache[@]}" "$compiler" "${args[@]}" \
        -MD -MF "$object.d" -c "$source" -o "$object"
    objects+=("$object")
}
compile "$gtest/googletest/src/gtest-all.cc" gtest
compile "$gtest/googletest/src/gtest_main.cc" gtest
for s in "${app_sources[@]}"; do compile "$s" app; done
for s in "${third_party[@]}"; do compile "$s" vendor; done
for s in "${test_sources[@]}"; do compile "$s" test; done

libs=("$ffmpeg/lib/libavformat.a" "$ffmpeg/lib/libavcodec.a" "$ffmpeg/lib/libswresample.a"
    "$ffmpeg/lib/libavutil.a" "${freetype_libs[@]}" -lcurl -lcrypto -lm)
ninja_inputs=("${objects[@]}" "$cxx")
ninja_edge LINK "$build/akeno_tests" "$cxx" "${sanitize[@]}" -pthread "${objects[@]}" "${libs[@]}" \
    -o "$build/akeno_tests"
ninja_run
