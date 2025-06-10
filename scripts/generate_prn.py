#!/usr/bin/env python3

import tifffile as tiff
import numpy as np
import struct
import sys
import os

# Constants expected by the Nocai printer
SIGNATURE   = 0x00005555
COLORS      = 4       # Y, M, C, K
BITS        = 1       # Ink dot size (not actual bits per pixel)
PASS        = 1
VSDMODE     = 0
PAPERWIDTH  = 0
RESERVED    = 0


def pack_with_dot_classification(channel_data: np.ndarray, threshold: np.ndarray) -> list[bytes]:
    height, width = channel_data.shape
    dot_map = np.zeros((height, width), dtype=np.uint8)

    # Phase 1: Dot class based on threshold
    for y in range(height):
        for x in range(width):
            if channel_data[y, x] < 128:
                continue  # not a dot
            t = threshold[y, x]
            if t >= 192:
                dot_map[y, x] = 1  # small
            elif t >= 128:
                dot_map[y, x] = 2  # medium
            else:
                dot_map[y, x] = 3  # large

    # Phase 2: 4x4 neighborhood promotion
    for y in range(1, height - 2):
        for x in range(1, width - 2):
            if dot_map[y, x] == 3:
                continue
            block = dot_map[y-1:y+3, x-1:x+3]
            if np.count_nonzero(block) >= 12:  # 75% of 4x4
                dot_map[y, x] = 3

    # Pack 2BPP: 4 pixels per byte
    lines = []
    for row in dot_map:
        packed = bytearray()
        byte = 0
        for i, level in enumerate(row):
            shift = (3 - (i % 4)) * 2
            byte |= (level & 0x03) << shift
            if (i + 1) % 4 == 0:
                packed.append(byte)
                byte = 0
        if len(row) % 4 != 0:
            packed.append(byte)
        while len(packed) % 4 != 0:
            packed.append(0)
        lines.append(bytes(packed))
    return lines


def generate_nocai_prn_2bit(c_path, m_path, y_path, k_path, c_mask_path, m_mask_path, y_mask_path, k_mask_path, xdpi, ydpi, output_path):
    C = tiff.imread(c_path)
    M = tiff.imread(m_path)
    Y = tiff.imread(y_path)
    K = tiff.imread(k_path)
    
    Cmask = tiff.imread(c_mask_path)
    Mmask = tiff.imread(m_mask_path)
    Ymask = tiff.imread(y_mask_path)
    Kmask = tiff.imread(k_mask_path)

    height, width = Y.shape
    print(f"Image loaded: {width}x{height}, assuming input is 1-bit, packing to 2-bit.")

    y_lines = pack_with_dot_classification(Y, Ymask)
    m_lines = pack_with_dot_classification(M, Mmask)
    c_lines = pack_with_dot_classification(C, Cmask)
    k_lines = pack_with_dot_classification(K, Kmask)

    print("Sample packed bytes (row 0):")
    print("Y:", y_lines[0][:8])
    print("M:", m_lines[0][:8])
    print("C:", c_lines[0][:8])
    print("K:", k_lines[0][:8])

    bytes_per_line = len(y_lines[0])  # Should be width // 4 with padding
    print(f"bytes_per_line = {bytes_per_line}")

    # Construct header (12 DWORDs)
    header = struct.pack('<12I',
        SIGNATURE,
        xdpi,
        ydpi,
        bytes_per_line,
        height,
        width,
        PAPERWIDTH,
        COLORS,
        BITS,
        PASS,
        VSDMODE,
        RESERVED
    )

    with open(output_path, 'wb') as f:
        f.write(header)
        for _ in range(PASS):
            for row in range(height):
                f.write(y_lines[row])
                f.write(m_lines[row])
                f.write(c_lines[row])
                f.write(k_lines[row])

    print(f"\n✅ PRN file created: {output_path}")
    print("Header contents:")
    print(f"  XDPI          = {xdpi}")
    print(f"  YDPI          = {ydpi}")
    print(f"  Width         = {width}")
    print(f"  Height        = {height}")
    print(f"  BytesPerLine  = {bytes_per_line}")
    print(f"  Total Bytes   = {os.path.getsize(output_path)}")

if __name__ == "__main__":
    if len(sys.argv) != 12:
        print("Usage: ./generate_prn_2bit.py c.tiff m.tiff y.tiff k.tiff c_mask.tiff m_mask.tiff y_mask.tiff k_mask.tiff XDPI YDPI output.prn")
        sys.exit(1)

    c_file = sys.argv[1]
    m_file = sys.argv[2]
    y_file = sys.argv[3]
    k_file = sys.argv[4]
    c_mask = sys.argv[5]
    m_mask = sys.argv[6]
    y_mask = sys.argv[7]
    k_mask = sys.argv[8]
    xdpi = int(sys.argv[9])
    ydpi = int(sys.argv[10])
    output_file = sys.argv[11]

    generate_nocai_prn_2bit(c_file, m_file, y_file, k_file, c_mask, m_mask, y_mask, k_mask, xdpi, ydpi, output_file)

