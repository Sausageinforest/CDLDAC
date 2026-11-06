#!/usr/bin/env python3
import sys
import os
import struct
import hashlib

# Retail PKG AES Key (from geohot's ps3 tools)
RETAIL_KEY = bytes([
    0x2E, 0x7B, 0x71, 0xD7, 0xC9, 0xC9, 0xA1, 0x4E,
    0xA3, 0x22, 0x1F, 0x18, 0x88, 0x28, 0xB8, 0xF8
])

def finalize_pkg(pkg_path):
    if not os.path.isfile(pkg_path):
        print(f"Skipping {pkg_path} (file not found)")
        return

    try:
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    except ImportError:
        print("Error: cryptography package is required. Run 'pip install cryptography'")
        sys.exit(1)

    print(f"Finalizing {pkg_path}...")
    with open(pkg_path, "rb") as f:
        data = bytearray(f.read())
    
    nlen = len(data)
    if nlen < 0x140:
        print(f"Error: {pkg_path} is too small to be a valid PKG ({nlen} bytes)")
        return

    if data[4] == 0x80:
        print(f"  {pkg_path} is already retail finalized (byte 4 == 0x80). Skipping.")
        return

    # In PS3 PKG header:
    # 0..4: magic (\x7F\x50\x4B\x47)
    # 4..8: type/debugFlag
    # 24..32: packageSize (u64)
    # 32..40: dataOffset (u64)
    # 40..48: dataSize (u64)
    data_offset = struct.unpack(">Q", data[32:40])[0]
    data_size = struct.unpack(">Q", data[40:48])[0]
    print(f"  PKG size: {nlen}, data_offset: {data_offset}, data_size: {data_size}")

    if data_offset >= nlen:
        print(f"Error: data_offset ({data_offset}) exceeds file size ({nlen})")
        return

    end_ptr = min(data_offset + data_size, nlen)

    # 1. Decrypt Debug PKG payload using SHA1-based keystream
    sha_crap = bytearray(0x40)
    sha_crap[0:8]   = data[0x60:0x68]
    sha_crap[8:16]  = data[0x60:0x68]
    sha_crap[16:24] = data[0x68:0x70]
    sha_crap[24:32] = data[0x68:0x70]
    
    dptr = data_offset
    counter = 0
    while dptr < end_ptr:
        h = hashlib.sha1(sha_crap).digest()
        chunk_len = min(16, end_ptr - dptr)
        for i in range(chunk_len):
            data[dptr + i] ^= h[i]
        counter += 1
        sha_crap[0x38:0x40] = struct.pack(">Q", counter)
        dptr += 16

    # 2. Re-encrypt payload using Retail AES-128-CTR key
    pkg_iv = bytes(data[0x70:0x80])
    cipher = Cipher(algorithms.AES(RETAIL_KEY), modes.CTR(pkg_iv))
    encryptor = cipher.encryptor()
    data[data_offset:end_ptr] = (
        encryptor.update(bytes(data[data_offset:end_ptr])) + encryptor.finalize()
    )

    # 3. Finalize header flags (set retail finalized bit 0x80)
    data[4] = 0x80
    zero_start = data_offset + data_size
    zero_end = min(zero_start + 0x60, nlen)
    if zero_start < nlen:
        data[zero_start:zero_end] = b"\x00" * (zero_end - zero_start)

    # 4. Update SHA1 hash at the end of the package
    if nlen >= 0x20:
        final_hash = hashlib.sha1(data[:nlen-0x20]).digest()
        data[nlen-0x20:nlen-0x20+len(final_hash)] = final_hash

    with open(pkg_path, "wb") as f:
        f.write(data)
    print(f"Successfully finalized {pkg_path} (Retail signed for CEX/HEN).")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: package_finalize.py <file.pkg> ...")
        sys.exit(1)
    for p in sys.argv[1:]:
        finalize_pkg(p)
