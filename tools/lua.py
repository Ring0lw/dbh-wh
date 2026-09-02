import collections
import os
import re
import struct
import sys

game = os.path.expanduser("~/.local/share/Steam/steamapps/common/Detroit Become Human")
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out", "lua")

text = re.compile(rb"[\x09\x0a\x0d\x20-\x7e]{24,}")
luaish = re.compile(rb"QDT\.|QDR\.|\bfunction\b|\blocal\b|\bthen\b|\bend\b")


def chunk_path(i):
    return os.path.join(game, "BigFile_PC.dat" if i == 0 else f"BigFile_PC.d{i:02d}")


def index():
    d = open(os.path.join(game, "BigFile_PC.idx"), "rb").read()
    n = struct.unpack(">I", d[0x65:0x69])[0]
    return [struct.unpack(">7I", d[0x69 + 28 * i:0x69 + 28 * i + 28]) for i in range(n)]


def main():
    ents = sorted(index(), key=lambda e: (e[6], e[3]))
    os.makedirs(out, exist_ok=True)
    kinds = collections.Counter()
    total = 0
    cur = None
    f = None
    for kind, _, oid, off, size, _, ch in ents:
        if ch != cur:
            if f:
                f.close()
            f = open(chunk_path(ch), "rb")
            cur = ch
        f.seek(off)
        b = f.read(size)
        if b.find(b"QDT.") < 0 and b.find(b"QDR.") < 0:
            continue
        runs = [r for r in text.findall(b) if len(luaish.findall(r)) >= 2]
        if not runs:
            continue
        src = b"\n".join(runs)
        kinds[kind] += 1
        total += len(src)
        with open(os.path.join(out, f"{kind}_{oid}.lua"), "wb") as w:
            w.write(src)
    if f:
        f.close()
    print(f"{sum(kinds.values())} resources with lua, {total / 1e6:.1f} MB of text")
    for k, n in kinds.most_common():
        print(f"kind {k}: {n}")


if __name__ == "__main__":
    main()
