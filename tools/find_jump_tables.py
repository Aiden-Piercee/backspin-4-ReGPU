#!/usr/bin/env python3
"""Find the absolute jump tables XenonAnalyse missed, in an xexdump image.

  find_jump_tables.py <image.bin> <image.sections> <switch_tables.toml> [--append]

Looks for every bctr fed by a table read (lis rA / addi rA,rA / rlwinm rB,rIdx,2,0,29 /
lwzx rC,rA,rB / mtctr rC) that has no [[switch]] entry within the few instructions before it.
A missed table becomes PPC_CALL_INDIRECT_FUNC(ctr) in the generated code, and the game dies
with "indirect call to X: no recompiled function there" the first time it takes that branch
(Swing_DLL 0x885370B4, whose table XenonAnalyse skips because a loop label sits between the
bounds check and the table read).

The entry count comes from the nearest cmplwi rIdx,K before the table read (K + 1 entries); a
table placed right after its bctr without one is read until a word stops looking like a code
address near the bctr. Prints the missing entries in XenonAnalyse's format; --append adds
them to the switch table file.
"""
import re
import struct
import sys

BCTR = 0x4E800420


def main():
    binp, sections, tables = sys.argv[1:4]
    img = open(binp, 'rb').read()
    lines = open(sections).read().splitlines()
    base = int(lines[0].split()[1], 16)
    text = []
    for line in lines[1:]:
        p = line.split()
        if p and p[-1] == 'code':
            text.append((int(p[-3], 16), int(p[-2], 16)))
    known = [int(m, 16) for m in re.findall(r'base = 0x([0-9A-Fa-f]+)', open(tables).read())]

    def w(a):
        return struct.unpack('>I', img[a - base:a - base + 4])[0]

    def s16(x):
        return x - 0x10000 if x & 0x8000 else x

    found = []
    for start, size in text:
        for a in range(start, start + size, 4):
            if w(a) != BCTR or any(a - 0x60 <= k <= a for k in known):
                continue
            mt = w(a - 4)  # mtctr rC = mtspr 9, rC
            if mt >> 26 != 31 or (mt >> 1) & 0x3FF != 467 or ((mt >> 16) & 0x1F | ((mt >> 11) & 0x1F) << 5) != 9:
                continue
            rc = (mt >> 21) & 31
            hi = lo = idx = ra = rb = None
            for b in range(a - 8, a - 48, -4):
                i = w(b)
                op = i >> 26
                if op == 31 and (i >> 1) & 0x3FF == 23 and (i >> 21) & 31 == rc and ra is None:  # lwzx
                    ra, rb = (i >> 16) & 31, (i >> 11) & 31
                elif op == 21 and ra is not None and idx is None and (i >> 16) & 31 == rb and (i >> 11) & 31 == 2 \
                        and (i >> 6) & 31 == 0 and (i >> 1) & 31 == 29:  # rlwinm rB,rIdx,2,0,29
                    idx = (i >> 21) & 31
                elif op == 14 and ra is not None and lo is None and (i >> 21) & 31 == ra and (i >> 16) & 31 == ra:  # addi
                    lo = s16(i & 0xFFFF)
                elif op == 15 and ra is not None and hi is None and (i >> 21) & 31 == ra and (i >> 16) & 31 == 0:  # lis
                    hi = i & 0xFFFF
            if None in (hi, lo, idx):
                continue
            table = ((hi << 16) + lo) & 0xFFFFFFFF
            count = None
            for b in range(a - 8, a - 160, -4):
                i = w(b)
                if i >> 26 == 10 and (i >> 21) & 1 == 0 and (i >> 16) & 31 == idx:  # cmplwi crX,rIdx,K
                    count = (i & 0xFFFF) + 1
                    break
            if (count is None or count > 512) and table == a + 4:
                count = 0
                while count < 512:
                    t = w(table + 4 * count)
                    if t & 3 or not a - 0x10000 <= t <= a + 0x10000:
                        break
                    count += 1
            if not count or count > 512:
                print(f'# {a:08X}: table {table:08X} r{idx}: no bound found', file=sys.stderr)
                continue
            found.append((a, idx, [w(table + 4 * k) for k in range(count)]))

    out = ''
    for a, idx, labels in found:
        out += f'[[switch]]\nbase = 0x{a:08X}\nr = {idx}\ndefault = 0x{labels[-1]:08X}\nlabels = [\n'
        out += ''.join(f'    0x{label:08X},\n' for label in labels) + ']\n\n'
    print(out, end='')
    print(f'{len(found)} missing jump tables', file=sys.stderr)
    if found and '--append' in sys.argv:
        with open(tables, 'a') as f:
            f.write('\n# ---- MISSED BY XENONANALYSE (tools/find_jump_tables.py) ----\n' + out)


if __name__ == '__main__':
    main()
