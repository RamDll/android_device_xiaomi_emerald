#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Makes <name>.patched.img from stock images in vendor/xiaomi/emerald-images. Three kinds of
# changes, all made in place in the stock EROFS image so that everything else stays
# byte-identical (labels, owners, other files):
#  - bytes: a few bytes in one file. The stock images compress files with LZ4 into
#    one-block pclusters, so the pcluster holding the bytes is decompressed, changed and
#    compressed back into the same block (it must still fit). Files smaller than a block
#    may instead be stored uncompressed right after their inode (flat inline layout); those
#    are changed in place.
#  - file: the whole content of one file. The new content is appended uncompressed after
#    the last block of the filesystem and the file's inode is switched to it (flat plain
#    layout, new size and block address); the superblock gets the new block count and
#    checksum. The old data blocks are left unused.
#  - append: text added at the end of one file; the stock content plus the text is then
#    written as with file.
# The AVB hashtree footer is then rebuilt with the stock parameters (checked first: rebuilt
# on the unchanged data it must give back the stock image byte for byte). The partition
# size grows only if the data no longer fits.
#
# Usage: ./patch-images.py   (after extract-images.sh)
# Needs dump.erofs and fsck.erofs (erofs-utils), liblz4, external/avb/avbtool.py and fec
# (out/host).
#

import ctypes
import filecmp
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys

MY_DIR = os.path.dirname(os.path.abspath(__file__))
ANDROID_ROOT = os.path.normpath(os.path.join(MY_DIR, "../../.."))
IMAGES_DIR = os.path.join(ANDROID_ROOT, "vendor/xiaomi/emerald-images")
AVBTOOL = os.path.join(ANDROID_ROOT, "external/avb/avbtool.py")
HOST_BIN = os.path.join(ANDROID_ROOT, "out/host/linux-x86/bin")

BLOCK = 4096
SB_OFFSET = 1024

# image -> (output, [("bytes", path in the image, file offset, stock bytes, new bytes),
#                    ("file", path in the image, new content (from ANDROID_ROOT), its sha256) or
#                    ("append", path in the image, text, sha256 of the stock content + text)])
PATCHES = {
    "vendor.img": ("vendor.patched.img", [
        # The fingerprint HAL runs FodEngineCore::init() twice at start: from the
        # XiaomiFingerprint constructor and again from main. Only the looper is guarded;
        # TouchFeatureCtl and DisplayFeatureCtl are initialized again and FodSensorMonitor
        # is created again. The first display monitor thread exits and leaves its
        # /dev/mi_display/disp_feature client registered for Fod and Brightness_clone
        # events with nobody reading it. Its queue fills up and the kernel logs
        # "Insufficient space" on every backlight change. Drop the second call.
        ("bytes", "/bin/hw/android.hardware.biometrics.fingerprint-service.odm", 0x9058,
         "2e1f0094",  # bl FodEngineCore::init()
         "1f2003d5"),  # nop
        # The stock vendor runs the AOSP example health HAL (V4). It passes the charge counter
        # of the Xiaomi fuel gauge on as it is, in mAh where Android expects uAh (Settings
        # showed "2 mAh"), and the stock maximum charging current and voltage on every port
        # (12.4 A x 11 V on a PC port too: "fast charging" on any cable). Ours from LineageOS
        # hardware/mediatek can handle both; see prebuilts/health/README.md.
        ("file", "/bin/hw/android.hardware.health-service.example",
         "device/xiaomi/emerald/prebuilts/health/android.hardware.health-service.mediatek",
         "9b5752a946c2b3102fc1f18da3824e91d66a67f8584986947b2f91671f46f665"),
    ]),
    "vendor_dlkm.img": ("vendor_dlkm.patched.img", [
        # LVTS (CPU, GPU and SoC thermal sensors) loses power in s2idle and the stock
        # module has empty suspend/resume callbacks, so every LVTS zone reads invalid
        # after the first sleep. Ours re-initializes LVTS on resume; source and build
        # notes in device/xiaomi/emerald-kernel/modules/soc_temp_lvts.
        ("file", "/lib/modules/soc_temp_lvts.ko",
         "device/xiaomi/emerald-kernel/vendor_dlkm/soc_temp_lvts.ko",
         "acb689596ea729af692a168dd87536327be444fb6a11eea793ab5e39945c0cc7"),
    ]),
}

lz4 = ctypes.CDLL("liblz4.so.1")


def die(msg):
    sys.exit(f"patch-images: {msg}")


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_sums(name):
    sums = {}
    path = os.path.join(MY_DIR, name)
    if os.path.exists(path):
        for line in open(path):
            if line.strip() and not line.startswith("#"):
                digest, image = line.split()
                sums[image] = digest
    return sums


def crc32c(data, crc=0xFFFFFFFF):
    """Raw CRC-32C update without the final inversion, as EROFS uses it."""
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc


def cat(image, path):
    return subprocess.run(["dump.erofs", "--cat", f"--path={path}", image], check=True,
                          capture_output=True).stdout


def footer_info(image):
    info = run("python3", AVBTOOL, "info_image", "--image", image)

    def field(name):
        m = re.search(rf"^\s*{name}:\s+(.+)$", info, re.M)
        if not m:
            die(f"{image}: no '{name}' in the AVB footer")
        return m.group(1).strip()

    # Only plain hashtree footers like the stock ones: anything else would come out different.
    if (field("Algorithm") != "NONE" or field("Rollback Index") != "0"
            or set(re.findall(r"^\s*Flags:\s+(\S+)$", info, re.M)) != {"0"}
            or field("Version of dm-verity") != "1"
            or field("Data Block Size") != f"{BLOCK} bytes"
            or field("Hash Block Size") != f"{BLOCK} bytes"
            or info.count("Hashtree descriptor:") != 1):
        die(f"{image}: unexpected AVB footer:\n{info}")
    args = ["--partition_name", field("Partition Name"),
            "--hash_algorithm", field("Hash Algorithm"),
            "--salt", field("Salt"),
            "--fec_num_roots", field("FEC num roots"),
            "--internal_release_string", field("Release String").strip("'")]
    for key, value in re.findall(r"^\s*Prop: (\S+) -> '(.*)'$", info, re.M):
        args += ["--prop", f"{key}:{value}"]
    return int(field("Image size").split()[0]), args


def partition_size(data_size, stock_size, args):
    """The stock size, or the smallest larger one the data and its footer fit in."""
    size = stock_size
    while True:
        out = run("python3", AVBTOOL, "add_hashtree_footer", "--calc_max_image_size",
                  "--partition_size", str(size), *args)
        if int(out) >= data_size:
            return size
        size += BLOCK


def read_sb(f):
    f.seek(SB_OFFSET)
    sb = bytearray(f.read(BLOCK - SB_OFFSET))
    if struct.unpack_from("<I", sb, 0)[0] != 0xE0F5E1E2:
        die("not an EROFS image")
    return sb


def sb_checksum(sb):
    plain = bytearray(sb)
    struct.pack_into("<I", plain, 4, 0)
    return crc32c(plain)


def nid_of(image, path):
    m = re.search(r"^NID: (\d+)", run("dump.erofs", f"--path={path}", image), re.M)
    if not m:
        die(f"{path}: not found")
    return int(m.group(1))


def extents(image, path):
    out = run("dump.erofs", "-e", f"--path={path}", image)
    return [tuple(int(x) for x in m) for m in re.findall(
        r"^\s*\d+:\s+(\d+)\.\.\s*(\d+) \|\s*\d+ :\s+(\d+)\.\.\s*(\d+) \|\s*(\d+)$", out, re.M)]


def inline_data(image, path):
    """Image offset and size of the data of a file stored whole after its inode, or None."""
    nid = nid_of(image, path)
    with open(image, "rb") as f:
        meta_blkaddr = struct.unpack_from("<I", read_sb(f), 40)[0]
        ipos = meta_blkaddr * BLOCK + nid * 32
        f.seek(ipos)
        inode = f.read(32)
    i_format, xattr_icount = struct.unpack_from("<HH", inode, 0)
    if (i_format >> 1) & 7 != 2:  # EROFS_INODE_FLAT_INLINE
        return None
    size = struct.unpack_from("<I", inode, 8)[0]
    if i_format & 1 or size >= BLOCK:
        die(f"{path}: only compact inline inodes of less than a block are handled")
    # erofs_xattr_ibody_header (12 bytes) and xattr_icount - 1 more 4-byte slots
    xattr_size = 12 + 4 * (xattr_icount - 1) if xattr_icount else 0
    return ipos + 32 + xattr_size, size


def patch_inline(image, path, pos, size, offset, old, new):
    if pos < BLOCK:
        die(f"{path}: inline data in block 0 is covered by the superblock checksum")
    with open(image, "r+b") as f:
        f.seek(pos)
        if f.read(size) != cat(image, path):
            die(f"{path}: inline data is not at {pos}")
        f.seek(pos + offset)
        found = f.read(len(old))
        if found != old:
            die(f"{path}+{offset:#x}: found {found.hex()}, expected {old.hex()}")
        f.seek(pos + offset)
        f.write(new)
    print(f"  {path}+{offset:#x}: {old.hex()} -> {new.hex()} (inline at {pos})")
    return [(pos + offset, pos + offset + len(new))]


def patch_bytes(image, path, offset, old, new):
    """Returns the changed byte ranges."""
    if len(new) != len(old):
        die(f"{path}+{offset:#x}: the new bytes must be as many as the old ones")
    inline = inline_data(image, path)
    if inline:
        pos, size = inline
        if offset + len(old) > size:
            die(f"{path}+{offset:#x}: past the end of the file")
        return patch_inline(image, path, pos, size, offset, old, new)
    for lstart, lend, pstart, pend, plen in extents(image, path):
        if lstart <= offset and offset + len(old) <= lend:
            break
    else:
        die(f"{path}+{offset:#x}: not inside one extent")
    if plen != BLOCK or pend - pstart != BLOCK:
        die(f"{path}: pcluster at {pstart} is not one block")

    with open(image, "r+b") as f:
        f.seek(pstart)
        block = f.read(BLOCK)
        margin = len(block) - len(block.lstrip(b"\0"))  # lz4_0padding: data is right-aligned
        llen = lend - lstart
        plain = ctypes.create_string_buffer(llen)
        if lz4.LZ4_decompress_safe(block[margin:], plain, BLOCK - margin, llen) != llen:
            die(f"{path}: pcluster at {pstart} does not decompress to {llen} bytes")
        data = bytearray(plain.raw)
        rel = offset - lstart
        if data[rel:rel + len(old)] != old:
            die(f"{path}+{offset:#x}: found {data[rel:rel + len(old)].hex()}, expected {old.hex()}")
        data[rel:rel + len(old)] = new

        packed = ctypes.create_string_buffer(2 * BLOCK)
        size = lz4.LZ4_compress_HC(bytes(data), packed, llen, 2 * BLOCK, 12)
        if not 0 < size <= BLOCK:
            die(f"{path}: patched pcluster compresses to {size} bytes, more than one block")
        check = ctypes.create_string_buffer(llen)
        if (lz4.LZ4_decompress_safe(packed.raw[:size], check, size, llen) != llen
                or check.raw != bytes(data)):
            die(f"{path}: patched pcluster does not decompress back")
        f.seek(pstart)
        f.write(bytes(BLOCK - size) + packed.raw[:size])
    print(f"  {path}+{offset:#x}: {old.hex()} -> {new.hex()} (pcluster {pstart}, {size} bytes)")
    return [(pstart, pstart + BLOCK)]


def replace_file(image, path, content):
    """Returns the changed byte ranges inside the old data."""
    nid = nid_of(image, path)
    with open(image, "r+b") as f:
        sb = read_sb(f)
        if sb_checksum(sb) != struct.unpack_from("<I", sb, 4)[0]:
            die("superblock checksum does not match the stock one")
        blocks, meta_blkaddr = struct.unpack_from("<II", sb, 36)
        f.seek(0, os.SEEK_END)
        if f.tell() != blocks * BLOCK:
            die(f"image size {f.tell()} is not the filesystem size {blocks * BLOCK}")

        ipos = meta_blkaddr * BLOCK + nid * 32
        f.seek(ipos)
        inode = bytearray(f.read(32))
        i_format = struct.unpack_from("<H", inode, 0)[0]
        if i_format & 1:
            die(f"{path}: extended inode, only compact ones are handled")
        if (i_format >> 1) & 7 not in (1, 3):
            die(f"{path}: not a compressed file")

        start = blocks
        f.seek(start * BLOCK)
        f.write(content + bytes(-len(content) % BLOCK))
        blocks += -(-len(content) // BLOCK)

        struct.pack_into("<H", inode, 0, i_format & ~(7 << 1))  # EROFS_INODE_FLAT_PLAIN
        struct.pack_into("<I", inode, 8, len(content))  # i_size
        struct.pack_into("<I", inode, 16, start)  # i_u.raw_blkaddr
        f.seek(ipos)
        f.write(inode)

        # The superblock checksum covers the rest of block 0, which holds the first inodes
        # when meta_blkaddr is 0: read it again after the inode write.
        sb = read_sb(f)
        struct.pack_into("<I", sb, 36, blocks)
        struct.pack_into("<I", sb, 4, sb_checksum(sb))
        f.seek(SB_OFFSET)
        f.write(sb)
    print(f"  {path}: {len(content)} bytes at block {start} (inode {nid})")
    return [(SB_OFFSET + 4, SB_OFFSET + 8), (SB_OFFSET + 36, SB_OFFSET + 40), (ipos, ipos + 32)]


def check_diff(a, b, size, ranges):
    """Fail unless the first size bytes of a and b differ only inside ranges."""
    with open(a, "rb") as fa, open(b, "rb") as fb:
        for pos in range(0, size, BLOCK):
            x, y = fa.read(BLOCK), fb.read(BLOCK)
            if x == y:
                continue
            for i in range(len(x)):
                if x[i] != y[i] and not any(s <= pos + i < e for s, e in ranges):
                    die(f"unexpected change at byte {pos + i}")


def main():
    if os.path.isdir(HOST_BIN):
        os.environ["PATH"] = HOST_BIN + os.pathsep + os.environ["PATH"]
    for tool in ("dump.erofs", "fsck.erofs", "fec"):
        if not shutil.which(tool):
            die(f"{tool} not found")
    stock_sums = read_sums("images.sha256")
    patched_sums = read_sums("images-patched.sha256")

    for name, (out_name, patches) in PATCHES.items():
        stock = os.path.join(IMAGES_DIR, name)
        out = os.path.join(IMAGES_DIR, out_name)
        tmp = out + ".tmp"
        print(f"{name} -> {out_name}")
        if sha256(stock) != stock_sums.get(name):
            die(f"{stock} does not match images.sha256 (run extract-images.sh)")
        sb = run("dump.erofs", "-s", stock)
        if (not re.search(r"blocksize:\s+4096$", sb, re.M)
                or not re.search(r"incompatible features:\s+lz4_0padding\s*$", sb, re.M)):
            die(f"{stock}: not a plain 4k lz4_0padding EROFS image")

        # The footer rebuilt from the unchanged data must give back the stock image.
        stock_size, args = footer_info(stock)
        shutil.copyfile(stock, tmp)
        run("python3", AVBTOOL, "erase_footer", "--image", tmp)
        data_size = os.path.getsize(tmp)
        run("python3", AVBTOOL, "add_hashtree_footer", "--image", tmp,
            "--partition_size", str(stock_size), *args)
        if not filecmp.cmp(stock, tmp, shallow=False):
            os.remove(tmp)
            die(f"{stock}: the rebuilt AVB footer differs from the stock one")
        run("python3", AVBTOOL, "erase_footer", "--image", tmp)

        expected, ranges = {}, []
        for patch in patches:
            if patch[0] == "bytes":
                _, path, offset, old, new = patch
                old, new = bytes.fromhex(old), bytes.fromhex(new)
                if path not in expected:
                    expected[path] = bytearray(cat(stock, path))
                ranges += patch_bytes(tmp, path, offset, old, new)
                expected[path][offset:offset + len(new)] = new
            elif patch[0] == "append":
                _, path, text, digest = patch
                if path in expected:
                    die(f"{path}: append after another patch of the same file")
                content = cat(stock, path) + text.encode()
                if hashlib.sha256(content).hexdigest() != digest:
                    die(f"{path}: sha256 of the stock content + text does not match")
                ranges += replace_file(tmp, path, content)
                expected[path] = content
            else:
                _, path, src, digest = patch
                content = open(os.path.join(ANDROID_ROOT, src), "rb").read()
                if hashlib.sha256(content).hexdigest() != digest:
                    die(f"{src}: sha256 does not match")
                ranges += replace_file(tmp, path, content)
                expected[path] = content

        for path, content in expected.items():
            if cat(tmp, path) != bytes(content):
                die(f"{path}: content in the patched image is not the expected one")
        check_diff(stock, tmp, data_size, ranges)
        if subprocess.run(["fsck.erofs", "--extract", tmp], capture_output=True).returncode:
            die(f"{out_name}: fsck.erofs failed")

        size = partition_size(os.path.getsize(tmp), stock_size, args)
        run("python3", AVBTOOL, "add_hashtree_footer", "--image", tmp,
            "--partition_size", str(size), *args)
        digest = sha256(tmp)
        if out_name in patched_sums and patched_sums[out_name] != digest:
            os.remove(tmp)
            die(f"{out_name}: sha256 {digest} does not match images-patched.sha256")
        os.replace(tmp, out)
        print(f"  {digest}  {out_name}" + ("" if size == stock_size else
                                           f" (partition {stock_size} -> {size} bytes)")
              + ("" if out_name in patched_sums else "  (not in images-patched.sha256 yet)"))


if __name__ == "__main__":
    main()
