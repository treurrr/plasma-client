#!/usr/bin/env python3
"""Pair every vanilla method with the function in this tree that ports it.

Usage: port_ledger.py build [--sources DIR]
       port_ledger.py where Class[.method] | what path:line | class Class
       port_ledger.py coverage [--package P] [--depth N] | todo [--package P] [--limit N] [--untouched]
       port_ledger.py stale | check [--base REF] | report
Every query takes --json. docs/PORT-LEDGER.md says how it matches and what it can't see.
"""
import argparse
import bisect
import fnmatch
import json
import os
import re
import subprocess
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(os.environ.get("PORT_LEDGER_OUT", ROOT / ".local/port-ledger"))
SOURCES = Path.home() / "mc-reference/1.21.11/src"
MAP_DIR = ROOT / "tools/port_map"
OUR_CODE = ["src/**/*.cpp", "src/**/*.hpp", "src/**/*.h", "include/**/*.hpp", "include/**/*.h",
            "tools/oracle/*.cpp"]

# What the client runs, as globs under the sources root. Everything else is indexed (so a
# citation of it is not stale) but left out of coverage.
SCOPE = [
    "net/minecraft/client/**",
    "net/minecraft/world/entity/**",
    "net/minecraft/world/phys/**",
    "net/minecraft/world/level/block/**",
    "net/minecraft/world/level/material/**",
    "net/minecraft/world/level/border/**",
    "net/minecraft/world/level/BlockGetter.java",
    "net/minecraft/world/level/BlockCollisions.java",
    "net/minecraft/world/level/CollisionGetter.java",
    "net/minecraft/world/level/ClipContext.java",
    "net/minecraft/world/level/GrassColor.java",
    "net/minecraft/world/level/FoliageColor.java",
    "net/minecraft/world/level/DryFoliageColor.java",
    "net/minecraft/world/level/ColorMapColorUtil.java",
    "net/minecraft/world/level/levelgen/*RandomSource.java",
    "net/minecraft/world/level/levelgen/RandomSupport.java",
    "net/minecraft/world/item/**",
    "net/minecraft/world/inventory/**",
    "net/minecraft/world/effect/**",
    "net/minecraft/world/attribute/**",
    "net/minecraft/world/timeline/**",
    "net/minecraft/world/food/**",
    "net/minecraft/world/*.java",
    "net/minecraft/network/protocol/**",
    "net/minecraft/network/chat/**",
    "net/minecraft/network/syncher/**",
    "net/minecraft/network/FriendlyByteBuf.java",
    "net/minecraft/network/VarInt.java",
    "net/minecraft/network/VarLong.java",
    "net/minecraft/network/LpVec3.java",
    "net/minecraft/network/Utf8String.java",
    "net/minecraft/core/**",
    "net/minecraft/sounds/**",
    "net/minecraft/util/Mth.java",
    "net/minecraft/util/RandomSource.java",
    "net/minecraft/util/random/**",
    "net/minecraft/util/ARGB.java",
    "net/minecraft/util/Ease.java",
    "net/minecraft/util/SmoothDouble.java",
    "net/minecraft/util/StringDecomposer.java",
    "net/minecraft/util/FormattedCharSequence.java",
    "net/minecraft/util/valueproviders/**",
    "com/mojang/blaze3d/**",
    "com/mojang/math/**",
]

STATUS_ORDER = ["ported", "inline", "mapped", "named", "absent", "skip", "trivial", "missing"]
FOUND = {"ported", "inline", "mapped", "named"}


def in_scope(rel):
    return any(fnmatch.fnmatchcase(rel, g) or (g.endswith("/**") and rel.startswith(g[:-2]))
               for g in SCOPE)


def package_of(rel):
    d = rel.rsplit("/", 1)[0]
    return d.split("/", 2)[-1] if d.startswith("net/minecraft/") else d.split("/", 1)[-1]


def blank_match(m):
    s = m.group(0)
    if s[0] == "/":
        return re.sub(r"[^\n]", " ", s)
    return s[0] + re.sub(r"[^\n]", " ", s[1:-1]) + s[-1]


JAVA_NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|"""(?:\\.|[^\\])*?"""|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
CPP_NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|R"([^(\s]*)\(.*?\)\1"|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])\'', re.S)


class Lines:
    def __init__(self, text):
        self.starts = [m.end() for m in re.finditer("\n", text)]

    def __call__(self, offset):
        return bisect.bisect_right(self.starts, offset) + 1


def strip_generics(s):
    prev = None
    while prev != s:
        prev, s = s, re.sub(r"<[^<>]*>", "", s)
    return s


# ---------------------------------------------------------------- vanilla

JAVA_CLASS = re.compile(r"(?<![\w$.@])(class|interface|enum|record|@interface)\s+([A-Za-z_$][\w$]*)")
JAVA_METHOD = re.compile(r"([\w$\]>?])\s+([A-Za-z_$][\w$]*)\s*\(")
JAVA_NOT_NAMES = {"if", "for", "while", "switch", "catch", "synchronized", "return", "new", "throw",
                  "else", "try", "do", "assert", "yield", "case"}
TRIVIAL_BODY = re.compile(r"\s*(?:return\s+[\w$.]+\s*;|this\.[\w$]+\s*=\s*[\w$]+\s*;|[\w$.]+\s*;)?\s*")


def java_supers(after):
    after = strip_generics(after)
    out = []
    for kw in ("extends", "implements"):
        m = re.search(rf"\b{kw}\s+([\w$.,\s]+?)(?=\bimplements\b|\bpermits\b|$)", after)
        if m:
            out += [s.strip().split(".")[-1] for s in m.group(1).split(",") if s.strip()]
    return out


def parse_java(rel, text, classes):
    b = JAVA_NOISE.sub(blank_match, text)
    line = Lines(b)
    pkg = package_of(rel)
    pkgdots = rel.rsplit("/", 1)[0].replace("/", ".")
    stack = []  # (kind, class key or None, body start)
    start = 0

    def top_class():
        return stack[-1][1] if stack and stack[-1][0] == "class" else None

    for m in re.finditer(r"[{};]", b):
        c = m.group()
        hdr = re.sub(r"@[\w$.]+(\s*\([^()]*\))?", lambda a: re.sub(r"[^\n]", " ", a.group(0)), b[start:m.start()])
        start = m.end()
        owner = top_class()
        if c == "}":
            if stack:
                kind, key, body = stack.pop()
                if kind == "method":
                    meth = key
                    meth["trivial"] = bool(TRIVIAL_BODY.fullmatch(b[body:m.start()]))
            continue
        in_body = owner is not None or not stack
        cm = JAVA_CLASS.search(hdr) if in_body and "=" not in hdr.split("(")[0] else None
        if c == "{" and cm:
            name = cm.group(2)
            dotted = f"{classes[owner]['name']}.{name}" if owner else name
            key = f"{pkgdots}.{dotted}"
            classes[key] = {"name": dotted, "file": rel, "line": line(m.start() - len(hdr) + cm.start(2)),
                            "pkg": pkg, "scope": in_scope(rel), "supers": java_supers(hdr[cm.end():]),
                            "methods": {}}
            stack.append(("class", key, m.end()))
            continue
        meth = None
        if owner is not None:
            simple = classes[owner]["name"].split(".")[-1]
            h = hdr.strip()
            mm = JAVA_METHOD.search(h)
            ctor = re.match(r"(?:<[^>]*>\s*)?([A-Za-z_$][\w$]*)\s*\(", h)
            name = None
            if ctor and ctor.group(1) == simple:
                name, pos = simple, ctor.start(1)
            elif mm and mm.group(2) not in JAVA_NOT_NAMES and "=" not in h[:mm.start(2)] \
                    and not re.search(r"\bnew\s*$", h[:mm.start(2)]):
                name, pos = mm.group(2), mm.start(2)
            if name:
                off = m.start() - len(hdr) + len(hdr) - len(hdr.lstrip())
                entry = classes[owner]["methods"].setdefault(name, {"lines": [], "trivial": True})
                entry["lines"].append(line(off + pos))
                meth = {"trivial": c == ";"}
                if c == ";":
                    continue
                entry.setdefault("_bodies", []).append(meth)
        if c == "{":
            stack.append(("method", meth, m.end()) if meth else ("other", None, m.end()))
    for cls in classes.values():
        if cls["file"] != rel:
            continue
        for e in cls["methods"].values():
            bodies = e.pop("_bodies", [])
            e["trivial"] = all(x["trivial"] for x in bodies) if bodies else True
    return b


def build_vanilla(sources):
    classes, uses, dotted = {}, Counter(), Counter()
    for path in sorted(sources.rglob("*.java")):
        rel = path.relative_to(sources).as_posix()
        if not rel.startswith(("net/minecraft/", "com/mojang/")):
            continue
        b = parse_java(rel, path.read_text(encoding="utf-8", errors="replace"), classes)
        uses.update(set(re.findall(r"\b[A-Z][\w$]*", b)))
        dotted.update({".".join(p) for p in re.findall(r"\b([A-Z][\w$]*)\s*\.\s*([A-Z][\w$]*)", b)})
    for cls in classes.values():
        parts = cls["name"].split(".")
        cls["uses"] = uses[parts[0]] if len(parts) == 1 else dotted[".".join(parts[-2:])] + 1
    return classes


# ---------------------------------------------------------------- our code

REF = r"`?([A-Z][\w$]*(?:(?:\.|::|#)[A-Za-z_$<][\w$<>]*)+)(?:\([^)`]*\))?`?"
LEAD = re.compile(rf"\s*{REF}((?:\s*(?:,|and|/|&)\s*{REF})*)")
TICKED = re.compile(r"`([A-Z][\w$]*(?:(?:\.|::|#)[A-Za-z_$<][\w$<>]*)+)(?:\([^)`]*\))?`")
CPP_NOT_NAMES = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "alignof",
                 "static_assert", "defined", "__attribute__", "alignas", "noexcept", "requires"}


def split_ref(ref):
    parts = [p for p in re.split(r"\.|::|#", ref) if p]
    return parts[:-1], parts[-1]


def lead_refs(text):
    """The citations a comment opens with: `A.b`, `C.d`: ..."""
    m = LEAD.match(text)
    if not m:
        return []
    refs = [m.group(1)] + re.findall(REF, m.group(2))
    ticked = text.lstrip().startswith("`")
    return [(r, ticked) for r in refs]


def comment_blocks(raw_lines):
    """(first line, last line, text) of every run of // lines; numbers are 1-based."""
    out, cur = [], None
    for i, ln in enumerate(raw_lines, 1):
        s = ln.strip()
        if s.startswith("//"):
            body = s.lstrip("/").strip()
            if cur:
                cur[1], cur[2] = i, cur[2] + " " + body
            else:
                cur = [i, i, body]
        elif cur:
            out.append(tuple(cur))
            cur = None
    if cur:
        out.append(tuple(cur))
    return out


def cpp_function_name(h):
    """The name a function header declares, or None."""
    depth = 0
    for i, ch in enumerate(h):
        if ch == "(" and depth == 0:
            nm = re.search(r"((?:[\w~]+\s*::\s*)*(?:operator\s*(?:\(\s*\)|[^\s\w(]+|\w+)|[\w~]+))\s*$", h[:i])
            if not nm:
                if re.search(r"operator\s*$", h[:i]):
                    return "operator()"
                return None
            name = re.sub(r"\s+", "", nm.group(1))
            last = name.split("::")[-1]
            if last in CPP_NOT_NAMES or "=" in h[:nm.start(1)] or last[:1].isdigit():
                return None
            return name
        if ch in "<[":
            depth += 1
        elif ch in ">]":
            depth = max(0, depth - 1)
    return None


def parse_cpp(rel, text):
    """Functions with their span, and every citation in the file."""
    b = CPP_NOISE.sub(blank_match, text)
    b = re.sub(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), b)
    line = Lines(b)
    funcs, stack, start, in_func = [], [], 0, 0
    for m in re.finditer(r"[{};]", b):
        c, hdr = m.group(), b[start:m.start()]
        start = m.end()
        if c == ";":
            continue
        if c == "}":
            if stack:
                kind, rec = stack.pop()
                if kind == "func":
                    in_func -= 1
                    rec["end"] = line(m.start())
            continue
        if in_func:
            stack.append(("other", None))
            continue
        lead = len(hdr) - len(hdr.lstrip())
        h = re.sub(r"^(?:(?:public|private|protected)\s*:\s*)+", "", hdr.strip())
        skipped = len(hdr.strip()) - len(h)
        h0 = h
        while True:
            t = re.match(r"template\s*<", h)
            if not t:
                break
            depth, i = 1, t.end()
            while i < len(h) and depth:
                depth += {"<": 1, ">": -1}.get(h[i], 0)
                i += 1
            h = h[i:].lstrip()
        if re.match(r"(?:inline\s+)?namespace\b|extern\s+\"", h):
            stack.append(("ns", None))
            continue
        cm = re.match(r"(?:\w+\s+)*?(?:class|struct|union|enum(?:\s+class)?)\s+(?:\[\[[^\]]*\]\]\s*|alignas\s*\([^)]*\)\s*)*([A-Za-z_]\w*)", h)
        if cm and "(" not in h[:cm.start(1)]:
            stack.append(("class", cm.group(1)))
            continue
        name = cpp_function_name(h) if "(" in h else None
        if not name:
            stack.append(("other", None))
            continue
        owners = [n for k, n in stack if k == "class"]
        full = "::".join(owners + [name]) if owners and "::" not in name else name
        hstart = m.start() - len(hdr) + lead + skipped
        rec = {"file": rel, "name": full, "short": name.split("::")[-1], "line": line(hstart),
               "body": line(m.start()), "end": None, "cites": [], "inline": []}
        funcs.append(rec)
        stack.append(("func", rec))
        in_func += 1
    del h0
    by_line = {f["line"]: f for f in funcs}
    raw = text.splitlines()
    cites = []
    for first, last, body in comment_blocks(raw):
        refs = lead_refs(body)
        owner = by_line.get(last + 1)
        kind = "port"
        if owner is None:
            inside = [f for f in funcs if f["end"] and f["body"] < first <= f["end"]]
            if inside:
                owner, kind = min(inside, key=lambda f: f["end"] - f["body"]), "inline"
        symbol = owner["name"] if owner else next_symbol(raw, last)
        for r, ticked in refs:
            cites.append({"file": rel, "line": first, "ref": r, "ticked": ticked, "kind": kind,
                          "symbol": symbol})
            if owner:
                owner["cites" if kind == "port" else "inline"].append(r)
        lead = {r for r, _ in refs}
        for r in TICKED.findall(body):
            if r not in lead:
                cites.append({"file": rel, "line": first, "ref": r, "ticked": True, "kind": "mention",
                              "symbol": symbol})
    for c in cites:
        if c["kind"] == "port" and c["symbol"] is None:
            c["kind"] = "mention"
    return funcs, cites


def next_symbol(raw, last):
    text = " ".join(raw[last:last + 3])
    text = re.split(r"[{;]", text)[0]
    if not text.strip() or text.lstrip().startswith(("//", "#")):
        return None
    cm = re.search(r"\b(?:class|struct|union|enum)\s+(\w+)", text)
    if cm and "(" not in text[:cm.start()]:
        return cm.group(1)
    if "(" in text:
        name = cpp_function_name(text)
        if name:
            return name
    ids = re.findall(r"[A-Za-z_]\w*", re.split(r"[=\[]", text)[0])
    return ids[-1] if ids else None


def our_files():
    seen = set()
    for g in OUR_CODE:
        for p in sorted(ROOT.glob(g)):
            rel = p.relative_to(ROOT).as_posix()
            if rel not in seen and "/build" not in rel:
                seen.add(rel)
                yield rel


def parse_ours(read=lambda rel: (ROOT / rel).read_text(encoding="utf-8", errors="replace")):
    funcs, cites = [], []
    for rel in our_files():
        f, c = parse_cpp(rel, read(rel))
        funcs += f
        cites += c
    return funcs, cites


def parse_map(files=None):
    rows = []
    for path in sorted(MAP_DIR.glob("*.tsv")) if files is None else files:
        text = path.read_text(encoding="utf-8") if isinstance(path, Path) else path[1]
        name = path.relative_to(ROOT).as_posix() if isinstance(path, Path) else path[0]
        for n, ln in enumerate(text.splitlines(), 1):
            if not ln.strip() or ln.startswith("#"):
                continue
            cols = (ln.split("\t") + ["", "", ""])[:4]
            rows.append({"file": name, "line": n, "key": cols[0].strip(), "verdict": cols[1].strip(),
                         "symbol": cols[2].strip(), "note": cols[3].strip()})
    return rows


# ---------------------------------------------------------------- matching

class Index:
    def __init__(self, classes):
        self.classes = classes
        self.by_dotted, self.by_simple = defaultdict(list), defaultdict(list)
        for key, c in classes.items():
            self.by_dotted[c["name"]].append(key)
            self.by_simple[c["name"].split(".")[-1]].append(key)

    def find_class(self, path):
        dotted = ".".join(path)
        cands = self.by_dotted.get(dotted) or self.by_simple.get(path[-1], [])
        if not cands and "." in dotted:
            cands = [k for k in self.classes if k.endswith("." + dotted)]
        return sorted(cands, key=lambda k: (not self.classes[k]["scope"], k))

    def declaring(self, key, method, depth=0):
        """The class that declares `method` for `key`, walking its supertypes."""
        c = self.classes[key]
        if method in c["methods"]:
            return key
        if depth > 12:
            return None
        for sup in c["supers"]:
            for k in self.find_class([sup]):
                found = self.declaring(k, method, depth + 1)
                if found:
                    return found
        return None

    def resolve(self, ref):
        """(class key, method, problem) for a citation."""
        path, method = split_ref(ref)
        cands = self.find_class(path)
        if not cands:
            return None, method, f"no class {'.'.join(path)}"
        for k in cands:
            d = self.declaring(k, method)
            if d:
                return d, method, None
        if method in (path[-1], "<init>", "new"):
            return cands[0], path[-1], None
        return cands[0], method, f"{self.classes[cands[0]]['name']} has no method {method}"


def snake(name):
    return re.sub(r"(?<=[a-z0-9])([A-Z])", r"_\1", name).lower()


def is_citation(cite, idx):
    """A ticked ref that opens a comment, or any ref whose class exists."""
    if cite["kind"] == "mention":
        return bool(idx.find_class(split_ref(cite["ref"])[0]))
    return cite["ticked"] or bool(idx.find_class(split_ref(cite["ref"])[0]))


def match(classes, funcs, cites, rows):
    idx = Index(classes)
    status = defaultdict(dict)  # key -> method -> [status, where]

    def put(key, method, st, where):
        cur = status[key].get(method)
        if cur is None or STATUS_ORDER.index(st) < STATUS_ORDER.index(cur[0]):
            status[key][method] = [st, where]

    resolved = []
    for c in cites:
        if not is_citation(c, idx):
            continue
        key, method, problem = idx.resolve(c["ref"])
        c = dict(c, cls=key, method=method, stale=problem)
        resolved.append(c)
        if problem or c["kind"] == "mention":
            continue
        put(key, method, "ported" if c["kind"] == "port" else "inline", f"{c['file']}:{c['line']} {c['symbol']}")

    fn_keys = {(f["file"], f["name"]) for f in funcs} | {(f["file"], f["short"]) for f in funcs}
    class_rows, pkg_rows = {}, []
    for r in rows:
        r["problem"] = None
        v = r["verdict"]
        if v not in ("port", "inline", "skip", "absent"):
            r["problem"] = f"verdict {v!r} is not port, inline, skip or absent"
            continue
        if v in ("port", "inline"):
            file, _, fn = r["symbol"].partition("::")
            if (file, fn) not in fn_keys:
                r["problem"] = f"no function {r['symbol']}"
        if r["key"].endswith("/"):
            pkg_rows.append(r)
            continue
        parts = r["key"].split(".")
        cands = idx.find_class(parts) if parts[-1][:1].isupper() else []
        if cands and parts[-1][:1].isupper():
            for k in cands:
                class_rows[k] = r
            continue
        key, method, problem = idx.resolve(r["key"])
        if problem:
            r["problem"] = r["problem"] or problem
            continue
        r["cls"], r["method"] = key, method
        st = {"port": "mapped", "inline": "mapped"}.get(v, v)
        put(key, method, st, f"{r['symbol'] or r['note']}" + (f" ({r['file']}:{r['line']})"))

    cited_classes = defaultdict(set)  # file -> class keys it cites
    for c in resolved:
        if c["cls"] and not c["stale"]:
            cited_classes[c["file"]].add(c["cls"])
    by_short = defaultdict(list)
    for f in funcs:
        by_short[f["short"]].append(f)

    for key, cls in classes.items():
        if not cls["scope"]:
            continue
        crow = class_rows.get(key) or next((class_rows[k] for k in class_rows
                                            if key.startswith(k + ".")), None)
        prow = next((r for r in pkg_rows if (cls["pkg"] + "/").startswith(r["key"])), None)
        for method, m in cls["methods"].items():
            if method in status[key]:
                continue
            names = {snake(method), method}
            if re.match(r"(get|is)[A-Z]", method):
                names.add(snake(re.sub(r"^(get|is)", "", method)).lstrip("_"))
            hit = next((f for n in names for f in by_short.get(n, ()) if key in cited_classes[f["file"]]), None)
            if crow or prow:
                r = crow or prow
                put(key, method, r["verdict"] if r["verdict"] in ("skip", "absent") else "mapped",
                    r["note"] or r["symbol"])
            elif hit:
                put(key, method, "named", f"{hit['file']}:{hit['line']} {hit['name']}")
            elif m["trivial"]:
                put(key, method, "trivial", "")
            else:
                put(key, method, "missing", "")
    return resolved, status


# ---------------------------------------------------------------- ledger

def build(sources):
    t = time.time()
    if not sources.is_dir():
        sys.exit(f"no vanilla sources at {sources} (use --sources DIR)")
    classes = build_vanilla(sources)
    funcs, cites = parse_ours()
    rows = parse_map()
    resolved, status = match(classes, funcs, cites, rows)
    ledger = {"sources": str(sources), "built": time.time(), "classes": classes, "functions": funcs,
              "citations": resolved, "map": rows, "status": status}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "ledger.json").write_text(json.dumps(ledger))
    n_in = sum(1 for c in classes.values() if c["scope"])
    print(f"{len(classes)} classes ({n_in} in scope), {len(funcs)} functions, {len(resolved)} citations, "
          f"{len(rows)} map rows in {time.time() - t:.1f} s -> {OUT / 'ledger.json'}", file=sys.stderr)
    return ledger


def newest_input():
    paths = [ROOT / r for r in our_files()] + list(MAP_DIR.glob("*.tsv")) + [Path(__file__)]
    return max(p.stat().st_mtime for p in paths if p.exists())


def load(sources, fresh=False):
    path = OUT / "ledger.json"
    if path.exists():
        ledger = json.loads(path.read_text())
        if not fresh or ledger["built"] >= newest_input():
            return ledger
    return build(Path(sources) if sources else SOURCES)


def vpath(ledger, key, line=None):
    c = ledger["classes"][key]
    return f"{c['file']}:{line or c['line']}"


def find(ledger, name):
    idx = Index(ledger["classes"])
    parts = name.replace("::", ".").replace("#", ".").split(".")
    if parts[-1][:1].isupper():
        return idx.find_class(parts), None
    key, method, problem = idx.resolve(name)
    return ([key] if key else []), (method if not problem or key else None)


def emit(args, data, text):
    print(json.dumps(data, indent=1) if args.json else text)


def method_rows(ledger, key):
    st = ledger["status"].get(key, {})
    cls = ledger["classes"][key]
    return [{"method": m, "line": e["lines"][0], "status": st.get(m, ["out-of-scope", ""])[0],
             "where": st.get(m, ["", ""])[1]} for m, e in sorted(cls["methods"].items(), key=lambda x: x[1]["lines"][0])]


def ports_of(ledger, key, method):
    out = [c for c in ledger["citations"] if c["cls"] == key and c["method"] == method and not c["stale"]]
    return [{"file": c["file"], "line": c["line"], "symbol": c["symbol"], "kind": c["kind"]} for c in out]


def counts(rows):
    n = Counter(r["status"] for r in rows)
    return {s: n[s] for s in STATUS_ORDER if n[s]}


def cmd_where(args, ledger):
    keys, method = find(ledger, args.name)
    if not keys:
        sys.exit(f"no vanilla class for {args.name}")
    out, lines = [], []
    for key in keys:
        cls = ledger["classes"][key]
        if method:
            if method not in cls["methods"]:
                continue
            e = cls["methods"][method]
            st = ledger["status"].get(key, {}).get(method, ["out-of-scope", ""])
            ports = ports_of(ledger, key, method)
            out.append({"class": cls["name"], "method": method, "vanilla": [vpath(ledger, key, l) for l in e["lines"]],
                        "status": st[0], "where": st[1], "ports": ports})
            lines.append(f"{cls['name']}.{method}  {st[0]}")
            lines += [f"  vanilla  {vpath(ledger, key, l)}" for l in e["lines"]]
            lines += [f"  {p['kind']:<7}  {p['file']}:{p['line']}  {p['symbol']}" for p in ports]
            if st[1] and not ports:
                lines.append(f"  {st[0]:<7}  {st[1]}")
        else:
            rows = method_rows(ledger, key)
            done = [r for r in rows if r["status"] in FOUND]
            out.append({"class": cls["name"], "vanilla": vpath(ledger, key), "uses": cls["uses"],
                        "counts": counts(rows), "ports": done})
            lines.append(f"{cls['name']}  {vpath(ledger, key)}  used in {cls['uses']} files")
            lines.append("  " + ", ".join(f"{k} {v}" for k, v in counts(rows).items()))
            lines += [f"  {r['method']:<32} {r['status']:<7} {r['where']}" for r in done]
    if not out:
        sys.exit(f"no method {method} in {', '.join(ledger['classes'][k]['name'] for k in keys)}")
    emit(args, out, "\n".join(lines))


def cmd_what(args, ledger):
    path, _, ln = args.loc.rpartition(":")
    path = os.path.relpath(Path(path).resolve(), ROOT) if Path(path).exists() else path
    ln = int(ln)
    fs = [f for f in ledger["functions"] if f["file"] == path and f["line"] <= ln <= (f["end"] or f["line"])]
    if not fs:
        sys.exit(f"no function at {path}:{ln}")
    f = min(fs, key=lambda f: (f["end"] or f["line"]) - f["line"])
    cites = [c for c in ledger["citations"] if c["file"] == path and c["symbol"] == f["name"]
             and c["kind"] != "mention" and f["line"] - 40 <= c["line"] <= (f["end"] or f["line"])]
    named = [(k, m) for k, ms in ledger["status"].items() for m, (s, w) in ms.items()
             if s == "named" and w.startswith(f"{path}:{f['line']} ")]
    data = {"function": f["name"], "file": path, "line": f["line"], "end": f["end"], "ports": [], "named": []}
    lines = [f"{path}:{f['line']}-{f['end']}  {f['name']}"]
    for c in cites:
        if c["stale"]:
            lines.append(f"  stale    {c['ref']}: {c['stale']}")
            continue
        cls = ledger["classes"][c["cls"]]
        e = cls["methods"].get(c["method"], {"lines": [cls["line"]]})
        data["ports"].append({"ref": f"{cls['name']}.{c['method']}", "kind": c["kind"],
                              "vanilla": [vpath(ledger, c["cls"], l) for l in e["lines"]]})
        lines.append(f"  {c['kind']:<7}  {cls['name']}.{c['method']}  {vpath(ledger, c['cls'], e['lines'][0])}")
    for k, m in named:
        cls = ledger["classes"][k]
        data["named"].append({"ref": f"{cls['name']}.{m}", "vanilla": vpath(ledger, k, cls["methods"][m]["lines"][0])})
        lines.append(f"  named    {cls['name']}.{m}  {vpath(ledger, k, cls['methods'][m]['lines'][0])}")
    if len(lines) == 1:
        lines.append("  cites no vanilla method")
    emit(args, data, "\n".join(lines))


def cmd_class(args, ledger):
    keys, _ = find(ledger, args.name)
    if not keys:
        sys.exit(f"no vanilla class {args.name}")
    key = keys[0]
    cls = ledger["classes"][key]
    rows = method_rows(ledger, key)
    lines = [f"{cls['name']}  {vpath(ledger, key)}  used in {cls['uses']} files",
             "  " + ", ".join(f"{k} {v}" for k, v in counts(rows).items())]
    lines += [f"  {r['line']:>5}  {r['method']:<34} {r['status']:<8} {r['where']}" for r in rows]
    if len(keys) > 1:
        lines.append("also: " + ", ".join(keys[1:]))
    emit(args, {"class": cls["name"], "vanilla": vpath(ledger, key), "methods": rows}, "\n".join(lines))


def coverage(ledger, package=None, depth=None):
    package = (package or "").strip("/")
    if depth is None:
        depth = (package.count("/") + 2) if package else 2
    table = defaultdict(Counter)
    for key, cls in ledger["classes"].items():
        if not cls["scope"] or not (cls["pkg"] + "/").startswith(package + "/" if package else ""):
            continue
        group = "/".join(cls["pkg"].split("/")[:depth])
        for m in cls["methods"]:
            table[group][ledger["status"][key][m][0]] += 1
    out = []
    for group in sorted(table):
        n = table[group]
        total = sum(n.values())
        worth = total - n["skip"] - n["trivial"]
        found = sum(n[s] for s in FOUND)
        out.append({"package": group, "methods": total, **{s: n[s] for s in STATUS_ORDER},
                    "found": round(100 * found / worth, 1) if worth else 100.0})
    return out


COV_COLS = ["methods", "ported", "inline", "mapped", "named", "absent", "skip", "trivial", "missing"]


def coverage_table(rows):
    head = "| package | " + " | ".join(COV_COLS) + " | found |"
    sep = "|---|" + "---:|" * (len(COV_COLS) + 1)
    body = [f"| {r['package']} | " + " | ".join(str(r[c]) for c in COV_COLS) + f" | {r['found']}% |" for r in rows]
    return "\n".join([head, sep] + body)


def total_row(rows, name):
    t = {"package": name}
    for c in COV_COLS:
        t[c] = sum(r[c] for r in rows)
    worth = t["methods"] - t["skip"] - t["trivial"]
    t["found"] = round(100 * sum(t[s] for s in FOUND) / worth, 1) if worth else 100.0
    return t


def cmd_coverage(args, ledger):
    rows = coverage(ledger, args.package, args.depth)
    rows.append(total_row(rows, "total"))
    emit(args, rows, coverage_table(rows))


def todo(ledger, package, untouched):
    package = (package or "").strip("/")
    out = []
    for key, cls in ledger["classes"].items():
        if not cls["scope"] or not (cls["pkg"] + "/").startswith(package + "/" if package else ""):
            continue
        rows = method_rows(ledger, key)
        missing = [r for r in rows if r["status"] == "missing"]
        if not missing or (untouched and any(r["status"] in FOUND for r in rows)):
            continue
        out.append({"class": cls["name"], "vanilla": vpath(ledger, key), "uses": cls["uses"],
                    "counts": counts(rows), "missing": [{"method": r["method"], "line": r["line"]} for r in missing]})
    out.sort(key=lambda r: (-r["uses"], r["class"]))
    return out


def cmd_todo(args, ledger):
    out = todo(ledger, args.package, args.untouched)[:args.limit]
    lines = []
    for r in out:
        lines.append(f"{r['class']}  {r['vanilla']}  used in {r['uses']} files  "
                     + ", ".join(f"{k} {v}" for k, v in r["counts"].items()))
        lines += [f"    {m['line']:>5}  {m['method']}" for m in r["missing"][:args.methods]]
        if len(r["missing"]) > args.methods:
            lines.append(f"    ... {len(r['missing']) - args.methods} more")
    emit(args, out, "\n".join(lines) or "nothing left")


def stale_items(ledger, files=None):
    out = [{"file": c["file"], "line": c["line"], "ref": c["ref"], "problem": c["stale"]}
           for c in ledger["citations"] if c["stale"] and (files is None or c["file"] in files)]
    out += [{"file": r["file"], "line": r["line"], "ref": r["key"], "problem": r["problem"]}
            for r in ledger["map"] if r.get("problem") and (files is None or r["file"] in files)]
    return out


def cmd_stale(args, ledger):
    out = stale_items(ledger)
    emit(args, out, "\n".join(f"{s['file']}:{s['line']}  {s['ref']}: {s['problem']}" for s in out) or "no stale citations")


def git(*a):
    return subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def cmd_check(args, ledger):
    base = git("merge-base", args.base, "HEAD").strip()
    changed = [p for p in git("diff", "--name-only", base).splitlines() if p]
    changed += [p for p in git("ls-files", "--others", "--exclude-standard").splitlines() if p]
    ours = set(our_files())
    code = [p for p in changed if p in ours or (p.endswith((".cpp", ".hpp", ".h")) and p.startswith(("src/", "include/", "tools/oracle/")))]
    maps = [p for p in changed if p.startswith("tools/port_map/") and p.endswith(".tsv")]
    hunks = defaultdict(list)
    for p in code:
        if p not in ours:
            continue
        for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", git("diff", "-U0", base, "--", p), re.M):
            a, n = int(m.group(1)), int(m.group(2) or 1)
            hunks[p].append((a, a + max(n, 1) - 1))
        if p not in git("ls-files", "--", p):
            hunks[p].append((1, 10 ** 9))

    def base_text(p):
        try:
            return git("show", f"{base}:{p}")
        except subprocess.CalledProcessError:
            return ""

    def keyset(cites, rows):
        s = {(c["cls"], c["method"]) for c in cites if c["cls"] and not c["stale"] and c["kind"] != "mention"}
        return s | {(r["cls"], r["method"]) for r in rows if r.get("cls") and r["verdict"] in ("port", "inline") and not r["problem"]}

    classes = ledger["classes"]
    base_funcs, base_cites = [f for f in ledger["functions"] if f["file"] not in code], \
        [c for c in ledger["citations"] if c["file"] not in code]
    raw = []
    for p in code:
        f, c = parse_cpp(p, base_text(p))
        base_funcs += f
        raw += c
    base_rows = [r for r in ledger["map"] if r["file"] not in maps]
    for p in maps:
        base_rows += parse_map([(p, base_text(p))])
    old_resolved, _ = match(classes, base_funcs, raw, base_rows)
    base_cites += old_resolved
    before, after = keyset(base_cites, base_rows), keyset(ledger["citations"], ledger["map"])
    old_cite_keys = {(c["file"], c["symbol"], c["ref"]) for c in base_cites}

    def name(k):
        return f"{classes[k[0]]['name']}.{k[1]}"

    def touched(f):
        return any(a <= (f["end"] or f["line"]) and f["line"] <= b for a, b in hunks.get(f["file"], ()))

    added = [c for c in ledger["citations"] if c["file"] in code and c["kind"] != "mention"
             and (c["file"], c["symbol"], c["ref"]) not in old_cite_keys]
    cited_fns = {(c["file"], c["symbol"]) for c in ledger["citations"] if c["kind"] == "port"}
    file_classes = defaultdict(set)
    for c in ledger["citations"]:
        if c["cls"] and not c["stale"]:
            file_classes[c["file"]].add(c["cls"])
    uncited, ports = [], []
    for f in ledger["functions"]:
        if not touched(f):
            continue
        if (f["file"], f["name"]) in cited_fns:
            ports.append(f"{f['file']}:{f['line']} {f['name']}: " + ", ".join(f["cites"]))
            continue
        for k in file_classes[f["file"]]:
            for m in classes[k]["methods"]:
                if snake(m) == f["short"]:
                    uncited.append(f"{f['file']}:{f['line']} {f['name']} looks like {classes[k]['name']}.{m}")
    data = {"base": base, "files": code + maps, "ports_touched": ports,
            "citations_added": [f"{c['file']}:{c['line']} {c['symbol']}: {c['ref']}" for c in added],
            "stale": stale_items(ledger, set(code + maps)), "uncited_lookalikes": uncited,
            "gaps_closed": sorted(name(k) for k in after - before if k[0] in classes),
            "ports_lost": sorted(name(k) for k in before - after if k[0] in classes)}
    lines = [f"against {args.base} ({base[:10]}): {len(code)} code files, {len(maps)} map files changed"]
    for title, k in [("ports touched", "ports_touched"), ("citations added", "citations_added"),
                     ("gaps closed", "gaps_closed"), ("ports lost", "ports_lost"),
                     ("functions that look like ports but cite nothing", "uncited_lookalikes")]:
        if data[k]:
            lines.append(f"{title} ({len(data[k])}):")
            lines += [f"  {x}" for x in data[k]]
    if data["stale"]:
        lines.append(f"stale ({len(data['stale'])}):")
        lines += [f"  {s['file']}:{s['line']}  {s['ref']}: {s['problem']}" for s in data["stale"]]
    emit(args, data, "\n".join(lines))
    return 1 if data["stale"] else 0


def cmd_report(args, ledger):
    top = coverage(ledger, depth=1)
    deep = coverage(ledger, depth=2)
    client = coverage(ledger, "client", 2)
    md = ["# Port ledger report", "",
          f"Built from {ledger['sources']} on {time.strftime('%Y-%m-%d %H:%M', time.localtime(ledger['built']))}.",
          "", "## Coverage by top-level package", "", coverage_table(top + [total_row(top, "total")]),
          "", "## Coverage by package", "", coverage_table(deep),
          "", "## Client", "", coverage_table(client + [total_row(client, "client total")]),
          "", "## Most-used client classes with nothing ported", ""]
    for r in todo(ledger, "client", True)[:40]:
        md.append(f"- `{r['class']}` ({r['uses']} files): {r['vanilla']}")
    st = stale_items(ledger)
    md += ["", f"## Stale citations ({len(st)})", ""] + [f"- {s['file']}:{s['line']} `{s['ref']}`: {s['problem']}" for s in st]
    md += ["", "## Every method", ""]
    for key in sorted(ledger["classes"], key=lambda k: (ledger["classes"][k]["pkg"], ledger["classes"][k]["name"])):
        cls = ledger["classes"][key]
        if not cls["scope"]:
            continue
        md.append(f"### {cls['name']} ({cls['pkg']})")
        md += [f"- {r['method']}: {r['status']}" + (f", {r['where']}" if r["where"] else "") for r in method_rows(ledger, key)]
    (OUT / "report.md").write_text("\n".join(md) + "\n")
    emit(args, {"report": str(OUT / "report.md"), "top": top},
         coverage_table(top + [total_row(top, "total")]) + f"\n\nwrote {OUT / 'report.md'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sources", help=f"decompiled vanilla sources (default {SOURCES})")
    ap.add_argument("--json", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build")
    sub.add_parser("where").add_argument("name")
    sub.add_parser("what").add_argument("loc")
    sub.add_parser("class").add_argument("name")
    p = sub.add_parser("coverage")
    p.add_argument("--package")
    p.add_argument("--depth", type=int)
    p = sub.add_parser("todo")
    p.add_argument("--package")
    p.add_argument("--limit", type=int, default=20)
    p.add_argument("--methods", type=int, default=8)
    p.add_argument("--untouched", action="store_true", help="only classes with nothing ported")
    sub.add_parser("stale")
    sub.add_parser("check").add_argument("--base", default="main")
    sub.add_parser("report")
    for s in sub.choices.values():
        s.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.cmd == "build":
        build(Path(args.sources) if args.sources else SOURCES)
        return 0
    ledger = load(args.sources, fresh=True)
    return {"where": cmd_where, "what": cmd_what, "class": cmd_class, "coverage": cmd_coverage,
            "todo": cmd_todo, "stale": cmd_stale, "check": cmd_check, "report": cmd_report}[args.cmd](args, ledger) or 0


if __name__ == "__main__":
    sys.exit(main())
