#!/usr/bin/env bash
# AKENO STREAM PS5 - Pinned FFmpeg build for the native title (and a host twin for tests).
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Why not PacBrew's FFmpeg: PacBrew 0.40.2 ships FFmpeg 7.0 configured for
# payloads (x86 asm, every component, network protocols, autodetected libc
# functions). The v0.3 player linked it into this sandboxed native title and
# never produced a frame on firmware 12.20 (ERROR -51). This configuration
# follows the one ProsperoTV (GPL-3.0-or-later, BlackBearReloaded) uses in a
# released native title: FFmpeg 8.0.1, no x86 asm, no network, no autodetected
# system libraries, and the portability fixes below. FFmpeg never opens files
# or sockets itself here; the app hands it bytes through custom AVIO.
#
# usage: tools/setup-ffmpeg.sh [ps5|host]   (prints the install prefix)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=8.0.1
tarball_hash=05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41
git_commit=894da5ca7d742e4429ffb2af534fcda0103ef593
target=${1:-ps5}
[[ $target == ps5 || $target == host ]] || { echo 'usage: tools/setup-ffmpeg.sh [ps5|host]' >&2; exit 2; }

deps="$root/.deps"
archive="$deps/ffmpeg-$version.tar.xz"
source_dir="$deps/ffmpeg-$version"
build="$deps/ffmpeg-$target/build"
prefix="$deps/ffmpeg-$target/root"
stamp=$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)
if [[ -f $prefix/.complete && $(<"$prefix/.complete") == "$stamp" && -f $prefix/lib/libavformat.a ]]; then
    printf '%s\n' "$prefix"
    exit 0
fi
mkdir -p "$deps"

# Source: the official release tarball, verified. Environments that cannot reach
# ffmpeg.org may instead provide a git checkout of the exact release commit.
if [[ ! -f $source_dir/configure ]]; then
    if [[ ! -f $archive ]]; then
        if ! curl --fail --location --silent --show-error --max-time 300 \
            "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o "$archive.download"; then
            rm -f -- "$archive.download"
            echo "==> [ffmpeg] ffmpeg.org unreachable; trying the release tag on GitHub" >&2
            rm -rf -- "$source_dir.git"
            git clone --quiet --depth 1 --branch "n$version" https://github.com/FFmpeg/FFmpeg.git \
                "$source_dir.git" >&2
            actual=$(git -C "$source_dir.git" rev-parse HEAD)
            [[ $actual == "$git_commit" ]] || {
                echo "FFmpeg n$version resolved to $actual, expected $git_commit" >&2
                exit 2
            }
            rm -rf -- "$source_dir.git/.git"
            # Release tarballs carry VERSION; without it version.sh would ask
            # the enclosing repository's git for a revision.
            printf '%s\n' "$version" > "$source_dir.git/VERSION"
            mv "$source_dir.git" "$source_dir"
        else
            mv "$archive.download" "$archive"
        fi
    fi
    if [[ ! -f $source_dir/configure ]]; then
        printf '%s  %s\n' "$tarball_hash" "$archive" | sha256sum --check --strict >&2
        tar -xJf "$archive" -C "$deps"
    fi
fi

rm -rf -- "$build" "$prefix"
mkdir -p "$build" "$prefix"
cross=()
if [[ $target == ps5 ]]; then
    sdk="$root/.deps/native/ps5-payload-sdk"
    [[ -x $sdk/bin/prospero-clang ]] || bash "$root/tools/setup-native-dependencies.sh" >/dev/null
    cross=(--target-os=freebsd --arch=x86_64 --enable-cross-compile
        --cc="$sdk/bin/prospero-clang" --ar="$sdk/bin/prospero-ar"
        --ranlib="$sdk/bin/prospero-ranlib" --nm="$sdk/bin/prospero-nm")
fi

cd "$build"
"$source_dir/configure" --prefix="$prefix" "${cross[@]}" \
    --disable-autodetect --disable-everything --disable-programs --disable-doc \
    --disable-network --disable-avdevice --disable-avfilter --disable-swscale \
    --disable-shared --enable-static --disable-debug \
    --enable-pthreads --disable-w32threads --disable-os2threads --disable-x86asm \
    --enable-avcodec --enable-avutil --enable-swresample --enable-avformat \
    --enable-demuxer=mov,matroska,mpegts,aac,mp3,h264,hevc,srt \
    --enable-muxer=mpegts,adts \
    --enable-parser=h264,hevc,aac,aac_latm,ac3,mpegaudio \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc \
    --enable-decoder=aac,aac_latm,ac3,eac3,mp2,mp3,h264,hevc,subrip,ass,movtext,webvtt \
    >&2

if [[ $target == ps5 ]]; then
    # The SDK's link step accepts unresolved imports, so configure believes
    # every probed function exists. Undo the probes that name functions the
    # console does not export to a native title, so FFmpeg uses its own
    # portable fallbacks instead of calling a null import.
    # gmtime_r, localtime_r, isatty and mkstemp are absent from libSceLibcInternal;
    # arc4random_buf only resolves to libScePosixForWebKit, which a native
    # title does not load (the call would jump to address 0).
    for name in GMTIME_R LOCALTIME_R ISATTY MKSTEMP PTHREAD_SET_NAME_NP PTHREAD_SETNAME_NP \
        ARC4RANDOM_BUF GETAUXVAL ELF_AUX_INFO SYSCTL; do
        sed -i -E "s/^#define HAVE_${name} 1$/#define HAVE_${name} 0/" config.h
    done
fi
make -j"${BUILD_JOBS:-$(nproc)}" >&2
make install >&2
printf '%s\n' "$stamp" > "$prefix/.complete"
printf '%s\n' "$prefix"
