#!/usr/bin/env python3
# AKENO STREAM PS5 - Encrypt the playback lab's clip with Clear Key.
# Copyright (C) 2026 AKENO STREAM contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes assets/selftest/h264-aac-360p-cenc.m4s from h264-aac-360p-frag.mp4.

The playback lab plays it through MediaSource and EME with the W3C Clear Key
key system to show whether the console's browser can decrypt and play
encrypted video at all - the pipeline DRM-protected sites need besides their
own key system. Clear Key protects nothing: the key is published here and in
the lab page (src/web/pages.cpp).

ISO/IEC 23001-7 'cenc' scheme: AES-128-CTR with 8-byte per-sample IVs. Video
samples are encrypted per NAL unit (subsamples: the length prefix, the NAL
header and non-VCL units stay clear; the protected part of a slice is a whole
number of 16-byte blocks); audio samples are encrypted whole. Each fragment
gains saiz, saio and senc boxes; the sample entries become encv / enca with a
sinf (frma, schm, schi/tenc); moov carries a common-format pssh with the key
ID. FFmpeg 6 writes a broken saiz/saio pair for fragmented files, hence this
tool. The output is deterministic (fixed key, key ID and IVs). VP9 + Opus
input works too, for trying the tool in a desktop Chromium without H.264.

    python3 tools/make-clearkey-clip.py [input] [output]
    ffmpeg -decryption_key fedcba9876543210fedcba9876543210 -i <output> ...
"""
import struct
import subprocess
import sys
from pathlib import Path

KID = bytes.fromhex("0123456789abcdef0123456789abcdef")
KEY = bytes.fromhex("fedcba9876543210fedcba9876543210")
COMMON_SYSTEM_ID = bytes.fromhex("1077efecc0b24d02ace33c1e52e2fb4b")
CONTAINERS = {b"moov", b"trak", b"mdia", b"minf", b"stbl", b"mvex", b"moof", b"traf", b"dinf"}


def box(kind, payload):
    return struct.pack(">I4s", 8 + len(payload), kind) + payload


def full_box(kind, version, flags, payload):
    return box(kind, struct.pack(">I", (version << 24) | flags) + payload)


def parse(data, start=0, end=None):
    """Boxes as (type, start, size) between start and end."""
    end = len(data) if end is None else end
    out = []
    while start < end:
        size, kind = struct.unpack(">I4s", data[start:start + 8])
        if size < 8 or start + size > end:
            raise SystemExit(f"bad box {kind!r} at {start}")
        out.append((kind, start, size))
        start += size
    return out


def find(data, start, size, kind):
    return [b for b in parse(data, start + 8, start + size) if b[0] == kind]


def ctr(iv8, plain):
    """AES-128-CTR with the counter block IV || 0^64, as 'cenc' requires."""
    if not plain:
        return b""
    result = subprocess.run(
        ["openssl", "enc", "-aes-128-ctr", "-nosalt", "-K", KEY.hex(), "-iv", (iv8 + bytes(8)).hex()],
        input=plain, capture_output=True, check=True)
    assert len(result.stdout) == len(plain)
    return result.stdout


# ---------------------------------------------------------------------------
# moov: tracks, their sample entries become encrypted ones.
def sinf(original):
    tenc = full_box(b"tenc", 0, 0, bytes([0, 0, 1, 8]) + KID)
    return box(b"sinf", box(b"frma", original) + full_box(b"schm", 0, 0, b"cenc" + struct.pack(">I", 0x10000))
               + box(b"schi", tenc))


def rebuild(data, kind, start, size, tracks):
    """Re-serialises a moov box, editing stsd and adding a pssh."""
    if kind == b"stsd":
        header = data[start + 8:start + 16]
        entries = b""
        for entry_kind, s, n in parse(data, start + 16, start + size):
            encrypted = {b"avc1": b"encv", b"vp09": b"encv", b"mp4a": b"enca", b"Opus": b"enca"}.get(entry_kind)
            if not encrypted:
                raise SystemExit(f"unsupported sample entry {entry_kind!r}")
            entries += box(encrypted, data[s + 8:s + n] + sinf(entry_kind))
        return box(b"stsd", header + entries)
    if kind in (b"stsz", b"stco"):
        count_at = start + (16 if kind == b"stsz" else 12)
        if struct.unpack(">I", data[count_at:count_at + 4])[0]:
            raise SystemExit("the input carries samples in moov; expected empty_moov fragments")
    if kind not in CONTAINERS:
        return data[start:start + size]
    payload = b"".join(rebuild(data, k, s, n, tracks) for k, s, n in parse(data, start + 8, start + size))
    if kind == b"moov":
        payload += full_box(b"pssh", 1, 0, COMMON_SYSTEM_ID + struct.pack(">I", 1) + KID + struct.pack(">I", 0))
    return box(kind, payload)


def track_info(data, moov):
    """track_ID -> (handler, NAL length size, trex default sample size)."""
    tracks = {}
    _, ms, mn = moov
    for _, ts, tn in find(data, ms, mn, b"trak"):
        tkhd = find(data, ts, tn, b"tkhd")[0]
        version = data[tkhd[1] + 8]
        track_id = struct.unpack(">I", data[tkhd[1] + (28 if version else 20):][:4])[0]
        mdia = find(data, ts, tn, b"mdia")[0]
        handler = data[find(data, *mdia[1:], b"hdlr")[0][1] + 16:][:4]
        stbl = find(data, *find(data, *mdia[1:], b"minf")[0][1:], b"stbl")[0]
        stsd = find(data, *stbl[1:], b"stsd")[0]
        entry = parse(data, stsd[1] + 16, stsd[1] + stsd[2])[0]
        length_size = 4
        if entry[0] == b"avc1":
            avcc = [b for b in parse(data, entry[1] + 8 + 78, entry[1] + entry[2]) if b[0] == b"avcC"][0]
            length_size = (data[avcc[1] + 8 + 4] & 3) + 1
        tracks[track_id] = {"handler": handler, "entry": entry[0], "length_size": length_size, "default_size": 0}
    for mvex in find(data, ms, mn, b"mvex"):
        for _, s, _ in find(data, *mvex[1:], b"trex"):
            track_id, _, _, size = struct.unpack(">IIII", data[s + 12:s + 28])
            if track_id in tracks:
                tracks[track_id]["default_size"] = size
    return tracks


# ---------------------------------------------------------------------------
# Fragments.
def subsamples(sample, length_size):
    """(clear, protected) byte counts of an H.264 sample, and the protected bytes."""
    entries, protected_data, clear, pos = [], b"", 0, 0
    while pos < len(sample):
        nal_size = int.from_bytes(sample[pos:pos + length_size], "big")
        nal = sample[pos + length_size:pos + length_size + nal_size]
        if len(nal) != nal_size or nal_size == 0:
            raise SystemExit("malformed NAL unit")
        protected = (nal_size - 1) // 16 * 16 if 1 <= (nal[0] & 0x1F) <= 5 else 0
        clear += length_size + nal_size - protected
        if protected:
            while clear > 0xFFFF:
                entries.append((0xFFFF, 0))
                clear -= 0xFFFF
            entries.append((clear, protected))
            protected_data += nal[nal_size - protected:]
            clear = 0
        pos += length_size + nal_size
    while clear > 0:
        entries.append((min(clear, 0xFFFF), 0))
        clear -= min(clear, 0xFFFF)
    return entries, protected_data


def vp9_subsamples(sample):
    """VP9 (no superframes): the frame's start, with its uncompressed header,
    stays clear; the rest in whole 16-byte blocks."""
    protected = (len(sample) - 64) // 16 * 16 if len(sample) > 80 else 0
    return [(len(sample) - protected, protected)], sample[len(sample) - protected:]


class Counter:
    value = 0

    def next(self):
        self.value += 1
        return struct.pack(">Q", self.value)


def fragment(data, moof, mdat, tracks, ivs):
    """The re-written moof and mdat of one fragment."""
    _, mo, mn = moof
    _, ds, dn = mdat
    body = bytearray(data[ds:ds + dn])
    trafs = []
    for _, ts, tn in find(data, mo, mn, b"traf"):
        tfhd = find(data, ts, tn, b"tfhd")[0]
        flags = struct.unpack(">I", data[tfhd[1] + 8:tfhd[1] + 12])[0] & 0xFFFFFF
        if flags & 0x1 or not flags & 0x020000:
            raise SystemExit("expected default-base-is-moof fragments without base offsets")
        track_id = struct.unpack(">I", data[tfhd[1] + 12:tfhd[1] + 16])[0]
        track = tracks[track_id]
        pos = tfhd[1] + 16 + (4 if flags & 0x2 else 0) + (4 if flags & 0x8 else 0)
        default_size = struct.unpack(">I", data[pos:pos + 4])[0] if flags & 0x10 else track["default_size"]
        runs = find(data, ts, tn, b"trun")
        if len(runs) != 1:
            raise SystemExit("expected one trun per traf")
        _, rs, rn = runs[0]
        run_flags = struct.unpack(">I", data[rs + 8:rs + 12])[0] & 0xFFFFFF
        count = struct.unpack(">I", data[rs + 12:rs + 16])[0]
        if not run_flags & 0x1:
            raise SystemExit("trun without data offset")
        data_offset = struct.unpack(">i", data[rs + 16:rs + 20])[0]
        pos = rs + 20 + (4 if run_flags & 0x4 else 0)
        per_sample = sum(4 for bit in (0x100, 0x200, 0x400, 0x800) if run_flags & bit)
        sizes = []
        for i in range(count):
            field = pos + i * per_sample + (4 if run_flags & 0x100 else 0)
            sizes.append(struct.unpack(">I", data[field:field + 4])[0] if run_flags & 0x200 else default_size)
        # Encrypt each sample in place in the mdat copy.
        at = mo + data_offset - ds
        video = track["handler"] == b"vide"
        aux = []
        for size in sizes:
            sample = bytes(body[at:at + size])
            iv = ivs.next()
            if video:
                if track["entry"] == b"vp09":
                    entries, protected = vp9_subsamples(sample)
                else:
                    entries, protected = subsamples(sample, track["length_size"])
                cipher = ctr(iv, protected)
                out, src, used = bytearray(), 0, 0
                for clear, n in entries:
                    out += sample[src:src + clear] + cipher[used:used + n]
                    src += clear + n
                    used += n
                assert src == len(sample)
                body[at:at + size] = out
                aux.append(iv + struct.pack(">H", len(entries)) + b"".join(struct.pack(">HI", c, p) for c, p in entries))
            else:
                body[at:at + size] = ctr(iv, sample)
                aux.append(iv)
            at += size
        children = [data[s:s + n] for k, s, n in parse(data, ts + 8, ts + tn)]
        trafs.append({"children": children, "trun_index": [k for k, _, _ in parse(data, ts + 8, ts + tn)].index(b"trun"),
                      "aux": aux, "video": video})
    # Layout: moof = mfhd + trafs, each traf + saiz + saio + senc.
    mfhd = data[find(data, mo, mn, b"mfhd")[0][1]:][:16]
    sizes_of = []
    for t in trafs:
        aux = t["aux"]
        if t["video"]:
            saiz = full_box(b"saiz", 0, 0, struct.pack(">BI", 0, len(aux)) + bytes(len(a) for a in aux))
        else:
            saiz = full_box(b"saiz", 0, 0, struct.pack(">BI", 8, len(aux)))
        senc = full_box(b"senc", 0, 2 if t["video"] else 0, struct.pack(">I", len(aux)) + b"".join(aux))
        t["saiz"], t["senc"] = saiz, senc
        sizes_of.append(8 + sum(len(c) for c in t["children"]) + len(saiz) + 20 + len(senc))
    new_size = 8 + len(mfhd) + sum(sizes_of)
    delta = new_size - mn
    out, offset = b"", 8 + len(mfhd)
    for t, traf_size in zip(trafs, sizes_of):
        senc_data = offset + traf_size - len(t["senc"]) + 16  # first IV, relative to the moof
        saio = full_box(b"saio", 0, 0, struct.pack(">II", 1, senc_data))
        children = list(t["children"])
        trun = bytearray(children[t["trun_index"]])
        old = struct.unpack(">i", trun[16:20])[0]
        trun[16:20] = struct.pack(">i", old + delta)
        children[t["trun_index"]] = bytes(trun)
        traf = box(b"traf", b"".join(children) + t["saiz"] + saio + t["senc"])
        assert len(traf) == traf_size
        out += traf
        offset += traf_size
    new_moof = box(b"moof", mfhd + out)
    assert len(new_moof) == new_size
    return new_moof + data[ds:ds + 8] + bytes(body[8:])


def main():
    root = Path(__file__).resolve().parent.parent
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "assets/selftest/h264-aac-360p-frag.mp4"
    target = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "assets/selftest/h264-aac-360p-cenc.m4s"
    data = source.read_bytes()
    top = parse(data)
    moov = [b for b in top if b[0] == b"moov"][0]
    tracks = track_info(data, moov)
    ivs = Counter()
    out = b""
    i = 0
    while i < len(top):
        kind, start, size = top[i]
        if kind == b"moov":
            out += rebuild(data, kind, start, size, tracks)
        elif kind == b"moof":
            if i + 1 >= len(top) or top[i + 1][0] != b"mdat":
                raise SystemExit("every moof must be followed by its mdat")
            out += fragment(data, top[i], top[i + 1], tracks, ivs)
            i += 1
        elif kind == b"mfra":
            pass  # its offsets would be stale; players do not need it
        else:
            out += data[start:start + size]
        i += 1
    target.write_bytes(out)
    print(f"wrote {target} ({len(out)} bytes, {ivs.value} samples encrypted)")


if __name__ == "__main__":
    main()
