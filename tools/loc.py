import collections
import os
import re
import struct
import sys

game = os.path.expanduser("~/.local/share/Steam/steamapps/common/Detroit Become Human")
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out")
loc_kind = 1016


def chunk_path(i):
    return os.path.join(game, "BigFile_PC.dat" if i == 0 else f"BigFile_PC.d{i:02d}")


def index():
    d = open(os.path.join(game, "BigFile_PC.idx"), "rb").read()
    n = struct.unpack(">I", d[0x65:0x69])[0]
    return [struct.unpack(">7I", d[0x69 + 28 * i:0x69 + 28 * i + 28]) for i in range(n)]


class rd:
    def __init__(self, b, p=0):
        self.b = b
        self.p = p

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        return v

    def raw(self, n):
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def str(self):
        return self.raw(self.u32()).decode("latin1")


def table(r):
    if r.u8() != 1:
        raise ValueError(f"table version at {r.p - 1:#x}")
    lang = r.str()
    rows = []
    for _ in range(r.u32()):
        key = r.str()
        text = r.raw(r.u32()).decode("utf-16-le", "replace")
        extra = r.u32()
        rows.append((key, text, extra))
    for _ in range(r.u32()):
        r.str()
        if r.u8() != 1:
            continue
        if r.u8() == 1:
            r.u32()
            r.u32()
        else:
            r.u32()
            r.raw(r.u32())
    return lang, rows


def container(b):
    p = b.find(b"LOCALIZ_")
    if p < 0:
        raise ValueError("no LOCALIZ_ tag")
    r = rd(b, p + 8)
    ver = r.u32()
    size = r.u32()
    end = r.p + size
    cnt = struct.unpack_from("<I", b, r.p)[0]
    if not (0 < cnt <= 64 and b[r.p + 4] == 1):
        r.u8()
    tables = [table(r) for _ in range(r.u32())]
    return ver, tables, r.p, end


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "")


markup = re.compile(r"\{[^}]*\}|<[^>]*>")


def clean(s):
    return " ".join(markup.sub(" ", s).split())


who_re = re.compile(r"_(?:PC|FA|VO|FO)_(?:X\d{2}([CKMAX])?)?([A-Z0-9]+?)(?:_|$)")


def readings(key):
    m = who_re.search(key)
    if not m:
        return None, None
    letter, name = m.group(1), m.group(2)
    return name, (letter + name if letter else None)


def speaker(key, cnt):
    a, b = readings(key)
    if a is None or a == "CHOICE":
        return ""
    if b and cnt.get(b, 0) > cnt.get(a, 0):
        return b
    return a


def main():
    ents = [e for e in index() if e[0] == loc_kind]
    os.makedirs(out, exist_ok=True)
    got = collections.defaultdict(list)
    bad = 0
    for kind, _, oid, off, size, _, ch in ents:
        with open(chunk_path(ch), "rb") as f:
            f.seek(off)
            b = f.read(size)
        try:
            ver, tables, p, end = container(b)
        except Exception as e:
            bad += 1
            print(f"container {oid} chunk {ch} off {off:#x}: {e}", file=sys.stderr)
            continue
        if p != end:
            print(f"container {oid}: parsed to {p:#x}, payload ends {end:#x}", file=sys.stderr)
        for lang, rows in tables:
            for key, text, extra in rows:
                got[lang].append((oid, key, text, extra))

    cnt = collections.Counter()
    for oid, key, text, extra in got.get("ENG", []):
        a, b = readings(key)
        if a:
            cnt[a] += 1

    seen = collections.Counter()
    for lang, rows in got.items():
        with open(os.path.join(out, f"{lang}.tsv"), "w", encoding="utf-8") as w, \
             open(os.path.join(out, f"{lang}.txt"), "w", encoding="utf-8") as s:
            for oid, key, text, extra in rows:
                who = speaker(key, cnt)
                w.write(f"{oid}\t{key}\t{who}\t{extra}\t{esc(text)}\t{clean(text)}\n")
                if key.startswith("X") and who:
                    s.write(f"[{key}] {who}: {clean(text)}\n")
                seen[lang] += 1
    print(f"{len(ents)} containers, {bad} failed")
    for lang, n in sorted(seen.items()):
        print(f"{lang:4} {n}")


if __name__ == "__main__":
    main()
