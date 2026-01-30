#!/usr/bin/env python3
"""
Profile UI builder (Python)
- Parses a Geo Profiler JSON file (see docs/profile-json.md)
- Generates a single-file HTML viewer (no React, no network) with embedded data.
"""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional


@dataclass
class Entry:
    file: str
    line: int
    cycles: int
    count: int
    address: str
    source: str
    function_chain: Optional[str]
    function_chain_frames: Optional[List[Dict[str, Any]]]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Build static profile viewer from JSON")
    p.add_argument("--input", "-i", help="Input profile JSON file (defaults: $GEO_PROF_RESOLVED_JSON or $GEO_PROF_JSON)")
    p.add_argument("--out", "-o", help="Output directory (default: <input_dir>/profile-view)")
    p.add_argument("--sort", choices=["cycles", "count"], default="cycles", help="Default sort key")
    p.add_argument("--title", default="Geo Profiler Report", help="Page title")
    p.add_argument("--flame", action="store_true", help="Embed a static flame graph SVG (built from function_chain)")
    p.add_argument("--flame-metric", choices=["cycles", "count"], help="Metric for flame graph width (default: --sort)")
    p.add_argument("--elf", help="ELF path used for disassembly/mixed view (default: $GEO_PROF_ELF)")
    p.add_argument("--toolchain-prefix", help="Toolchain prefix (e.g. m68k-amigaos-) used to locate objdump (default: $E9K_TOOLCHAIN_PREFIX)")
    p.add_argument("--text-base", help="Runtime TEXT base address for PC translation (default: none)")
    p.add_argument("--data-base", help="Runtime DATA base address (default: none)")
    p.add_argument("--bss-base", help="Runtime BSS base address (default: none)")
    p.add_argument("--src-base", help="Source tree root for embedding source files")
    p.add_argument("--embed-source", choices=["none", "full", "context"], default="full", help="Embed source: none, full files (default), or line context slices")
    p.add_argument("--context-lines", type=int, default=12, help="Context lines on each side when --embed-source context is used")
    p.add_argument("--no-open", action="store_true", help="Do not open the generated index.html in the default browser")
    # Single-file only; no legacy React mode
    args = p.parse_args()

    # Apply environment defaults for file paths
    if not args.input:
        args.input = os.environ.get("GEO_PROF_RESOLVED_JSON") or os.environ.get("GEO_PROF_JSON")
    if not args.src_base:
        args.src_base = os.environ.get("GEO_PROF_SRC_BASE")
    if not args.elf:
        args.elf = os.environ.get("GEO_PROF_ELF")
    if not args.toolchain_prefix:
        args.toolchain_prefix = os.environ.get("E9K_TOOLCHAIN_PREFIX")
    if not args.out and args.input:
        try:
            inp = Path(args.input)
            args.out = str(inp.parent / "profile-view")
        except Exception:
            pass

    # Validate required paths
    if not args.input:
        p.error("--input not provided and neither $GEO_PROF_RESOLVED_JSON nor $GEO_PROF_JSON is set")
    if not args.out:
        p.error("--out not provided and could not infer a default. Provide --out explicitly.")

    return args


def validate_and_load(path: Path) -> List[Entry]:
    raw = path.read_text(encoding="utf-8")
    try:
        data = json.loads(raw)
    except Exception as e:
        raise SystemExit(f"Failed to parse JSON: {e}")
    if not isinstance(data, list):
        raise SystemExit("Input must be a JSON array of objects")

    entries: List[Entry] = []
    for i, obj in enumerate(data):
        if not isinstance(obj, dict):
            raise SystemExit(f"entry[{i}] must be an object")
        file = must_str(obj.get("file"), f"entry[{i}].file")
        line = must_int(obj.get("line"), f"entry[{i}].line", min_value=0)
        cycles = must_int(obj.get("cycles"), f"entry[{i}].cycles", min_value=0)
        count = must_int(obj.get("count"), f"entry[{i}].count", min_value=0)
        address = must_str(obj.get("address"), f"entry[{i}].address")
        source = obj.get("source") if isinstance(obj.get("source"), str) else ""
        function_chain = obj.get("function_chain") if isinstance(obj.get("function_chain"), str) else None
        frames = None
        fc = obj.get("function_chain_frames")
        if isinstance(fc, list):
            frames = []
            for fr in fc:
                if isinstance(fr, dict):
                    ffile = fr.get("file")
                    fline = fr.get("line")
                    ffunc = fr.get("function")
                    floc = fr.get("loc")
                    if isinstance(ffile, str) and isinstance(fline, int):
                        frames.append({"file": ffile, "line": fline, "function": ffunc, "loc": floc})
        entries.append(Entry(file, line, cycles, count, address, source, function_chain, frames))
    return entries


def must_str(v: Any, name: str) -> str:
    if not isinstance(v, str):
        raise SystemExit(f"{name} must be string")
    return v


def must_int(v: Any, name: str, *, min_value: Optional[int] = None) -> int:
    if not isinstance(v, int):
        # Accept JSON numbers that come as floats but are integers
        if isinstance(v, float) and v.is_integer():
            v = int(v)
        else:
            raise SystemExit(f"{name} must be integer")
    if min_value is not None and v < min_value:
        raise SystemExit(f"{name} must be >= {min_value}")
    return v


def derive(entries: List[Entry]) -> Dict[str, Any]:
    total_cycles = sum(e.cycles for e in entries)
    total_count = sum(e.count for e in entries)

    by_file: Dict[str, Dict[str, Any]] = {}
    for e in entries:
        rec = by_file.get(e.file)
        if not rec:
            rec = {"file": e.file, "cycles": 0, "count": 0, "lines": 0}
            by_file[e.file] = rec
        rec["cycles"] += e.cycles
        rec["count"] += e.count
        rec["lines"] += 1

    files = [
        {
            "file": f["file"],
            "cycles": f["cycles"],
            "count": f["count"],
            "lines": f["lines"],
            "cyclesPct": (f["cycles"] / total_cycles) if total_cycles else 0.0,
        }
        for f in by_file.values()
    ]

    return {
        "total_cycles": total_cycles,
        "total_count": total_count,
        "files": files,
    }


def to_output(entries: List[Entry], totals: Dict[str, Any], *, sort_key: str) -> Dict[str, Any]:
    total_cycles = totals["total_cycles"] or 0
    total_count = totals["total_count"] or 0
    # Sort entries by chosen key desc, then file, then line
    entries_sorted = sorted(
        entries,
        key=lambda e: (
            -(e.count if sort_key == "count" else e.cycles),
            e.file,
            e.line,
        ),
    )

    out_entries = []
    for e in entries_sorted:
        row = {
            "file": e.file,
            "line": e.line,
            "cycles": e.cycles,
            "count": e.count,
            "address": e.address,
            "source": e.source,
            "function_chain": e.function_chain,
            "cyclesPct": (e.cycles / total_cycles) if total_cycles else 0.0,
            "countPct": (e.count / total_count) if total_count else 0.0,
        }
        if e.function_chain_frames:
            row["function_chain_frames"] = e.function_chain_frames
        out_entries.append(row)

    return {
        "meta": {
            "generatedAt": datetime.utcnow().isoformat(timespec="seconds") + "Z",
            "totalCycles": total_cycles,
            "totalCount": total_count,
            "files": totals["files"],
            "sort": sort_key,
        },
        "entries": out_entries,
    }


# React/static helpers removed; single-file output only


def escape_html(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    out_dir = Path(args.out)
    # No static dir; single-file output only

    entries = validate_and_load(input_path)
    totals = derive(entries)
    out = to_output(entries, totals, sort_key=args.sort)

    # enrich meta
    out["meta"]["title"] = args.title
    out["meta"]["sourceFile"] = str(input_path.resolve())

    # Single-file generation
    tmpl = (Path(__file__).with_name("template_inline.html")).read_text(encoding="utf-8")
    # Always attempt to embed flame graph (no flag required)
    flame_svg = ""
    flame_json = None
    try:
        metric = args.flame_metric or args.sort
        flame_svg = build_flame_svg(out["entries"], metric=metric)
        flame_json = build_flame_tree(out["entries"], metric=metric)
    except Exception as e:
        flame_svg = f"<!-- Flame graph generation failed: {escape_html(str(e))} -->"
        flame_json = None
    # Optionally embed sources
    sources_json = None
    sources_obj: Optional[Dict[str, Any]] = None
    if args.embed_source != "none":
        if not args.src_base:
            print("Warning: --embed-source requested but --src-base not provided; skipping embedding")
        else:
            sources_obj = embed_sources(entries, Path(args.src_base), mode=args.embed_source, context_lines=max(0, args.context_lines))
            sources_json = json.dumps(sources_obj)
            try:
                # initialize status for sources if available later
                pass
            except Exception:
                pass

    # Default: embed ASM disassembly slices per entry when ELF is available
    asm_map: Dict[str, List[Dict[str, Any]]] = {}
    # Status for UI badge
    errors: List[str] = []
    status = {
        "asm": {"ok": False, "tool": None, "mixed": False, "source": None, "elf": args.elf or None, "toolchainPrefix": args.toolchain_prefix or None},
        "sources": {"mode": args.embed_source, "base": args.src_base or None, "files": 0},
        "errors": errors,
    }
    # If sources were embedded, update file count
    if sources_json:
        try:
            _src_tmp = json.loads(sources_json)
            status["sources"]["files"] = len((_src_tmp or {}).get("files", {}))
        except Exception:
            pass
    elf_path = args.elf
    asm_mix: Dict[str, List[Dict[str, Any]]] = {}
    if elf_path and Path(elf_path).exists():
        try:
            asm_index, asm_diag, asm_seq = build_disasm_index(Path(elf_path), toolchain_prefix=args.toolchain_prefix)
            if asm_index:
                status["asm"].update({"ok": True, "tool": asm_diag.get("tool"), "source": "elf"})
                status["asm"]["entriesTotal"] = len(out.get("entries") or [])
                status["asm"]["slices"] = 0
                status["asm"]["pcsParsed"] = 0
                status["asm"]["pcsAdjusted"] = 0
                status["asm"]["textBase"] = args.text_base
                status["asm"]["dataBase"] = args.data_base
                status["asm"]["bssBase"] = args.bss_base

                asm_addrs = sorted(asm_index.keys())
                asm_min = asm_addrs[0] if asm_addrs else 0
                asm_max = asm_addrs[-1] if asm_addrs else 0
                status["asm"]["vmaMin"] = f"0x{asm_min:08x}"
                status["asm"]["vmaMax"] = f"0x{asm_max:08x}"

                text_base_int = None
                try:
                    if args.text_base:
                        text_base_int = int(str(args.text_base), 0)
                except Exception:
                    text_base_int = None

                addr2 = addr2line_create(Path(elf_path), toolchain_prefix=args.toolchain_prefix, src_base=Path(args.src_base) if args.src_base else None, sources=sources_obj)
                # create slices for each entry using its representative PC
                for row in out["entries"]:
                    pc_hex = row.get("address") or ""
                    try:
                        pc = int(pc_hex, 16)
                    except Exception:
                        continue
                    status["asm"]["pcsParsed"] += 1

                    pc_adj = pc
                    if asm_addrs and text_base_int is not None and not (asm_min <= pc_adj <= asm_max):
                        # Heuristic: if profile PCs are runtime addresses, translate into ELF VMAs by subtracting the runtime text base.
                        # Assume ELF VMAs begin at asm_min.
                        bias = text_base_int - asm_min
                        candidate = pc_adj - bias
                        if asm_min <= candidate <= asm_max:
                            pc_adj = candidate
                            status["asm"]["pcsAdjusted"] += 1

                    slice_rows = disasm_slice(asm_index, pc_adj, context=24)
                    if addr2 and slice_rows:
                        addr2line_annotate_slice(addr2, slice_rows)
                    if slice_rows:
                        asm_map[pc_hex.lower()] = slice_rows
                        status["asm"]["slices"] += 1
                    # Build mixed slice using sequence
                    idxs = [i for i,e in enumerate(asm_seq) if e.get("type")=="insn" and e.get("address")==pc_adj]
                    if idxs:
                        i0 = idxs[0]
                        start = max(0, i0-60)
                        end = min(len(asm_seq), i0+61)
                        mixed = []
                        if asm_diag.get("mixedCandidate"):
                            for e in asm_seq[start:end]:
                                if e.get("type")=="src":
                                    mixed.append({"kind":"src","file":e.get("file"),"line":e.get("line"),"text":e.get("text")})
                                elif e.get("type")=="insn":
                                    mixed.append({"kind":"insn","address":f"0x{e.get('address',0):06x}","text":e.get("text"),"file":e.get("file"),"line":e.get("line"),"isPC": e.get("address")==pc_adj})
                        elif addr2:
                            mixed = addr2line_build_mixed(addr2, asm_seq[start:end], pc_adj, sources_obj)
                        if mixed:
                            asm_mix[pc_hex.lower()] = mixed
                # determine if any slice has file/line for mixed OR any mixed slice exists
                mixed_flag = False
                for sl in asm_map.values():
                    if any(r.get("file") and r.get("line") for r in sl):
                        mixed_flag = True
                        break
                if asm_mix:
                    mixed_flag = True
                status["asm"]["mixed"] = mixed_flag
        except Exception as e:
            errors.append(f"ELF disassembly failed: {e}")
    # Fallback: attempt ROM disassembly if provided
    if not asm_map:
        rom = os.environ.get("GEO_PROF_ROM")
        base = os.environ.get("GEO_PROF_ROM_BASE")
        if rom and Path(rom).exists() and base:
            try:
                base_int = int(str(base), 0)
                asm_index, asm_diag, asm_seq = build_disasm_index_from_rom(Path(rom), base_int, toolchain_prefix=args.toolchain_prefix)
                if asm_index:
                    status["asm"].update({"ok": True, "tool": asm_diag.get("tool"), "source": "rom", "mixed": False})
                    for row in out["entries"]:
                        pc_hex = row.get("address") or ""
                        try:
                            pc = int(pc_hex, 16)
                        except Exception:
                            continue
                        slice_rows = disasm_slice(asm_index, pc, context=24)
                        if slice_rows:
                            asm_map[pc_hex.lower()] = slice_rows
                        idxs = [i for i,e in enumerate(asm_seq) if e.get("type")=="insn" and e.get("address")==pc]
                        if idxs:
                            i0 = idxs[0]
                            start = max(0, i0-60)
                            end = min(len(asm_seq), i0+61)
                            mixed = []
                            for e in asm_seq[start:end]:
                                if e.get("type")=="src":
                                    mixed.append({"kind":"src","file":e.get("file"),"line":e.get("line"),"text":e.get("text")})
                                elif e.get("type")=="insn":
                                    mixed.append({"kind":"insn","address":f"0x{e.get('address',0):06x}","text":e.get("text"),"file":e.get("file"),"line":e.get("line"),"isPC": e.get("address")==pc})
                            if mixed:
                                asm_mix[pc_hex.lower()] = mixed
            except Exception as e:
                errors.append(f"ROM disassembly failed: {e}")
    if status["asm"]["ok"] and not status["asm"]["mixed"]:
        pcs_parsed = status.get("asm", {}).get("pcsParsed")
        if isinstance(pcs_parsed, int) and pcs_parsed == 0:
            errors.append("No entry addresses found (all address fields empty); ASM/Mixed view unavailable. Ensure analysis JSON includes an address per entry.")
        else:
            errors.append("ASM OK but no source line mapping found; Mixed view unavailable. Ensure ELF has DWARF line info (build with -g and do not strip), and that PCs match the ELF VMAs.")
    if not status["asm"]["ok"]:
        errors.append("No ASM available. Ensure a suitable objdump is in PATH and --elf is set (or ROM+BASE).")
    # Prepare logo image as data URI
    def _load_logo_data_uri() -> str:
        try:
            logo_path = Path(__file__).with_name("static") / "enable.png"
            import base64
            data = logo_path.read_bytes()
            return "data:image/png;base64," + base64.b64encode(data).decode("ascii")
        except Exception:
            return "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGP4BwQACfsD/ci6TdgAAAAASUVORK5CYII="

    # Optionally embed Pixel Operator fonts if present
    def _embed_font(face_name: str, filename: str, weight: str = 'normal') -> str:
        try:
            fpath = Path(__file__).with_name("static") / filename
            if not fpath.exists():
                return ""
            import base64
            data = fpath.read_bytes()
            b64 = base64.b64encode(data).decode('ascii')
            # Guess MIME type by extension
            if filename.lower().endswith('.otf'):
                mime = 'font/otf'
            elif filename.lower().endswith('.woff2'):
                mime = 'font/woff2'
            elif filename.lower().endswith('.woff'):
                mime = 'font/woff'
            else:
                mime = 'font/ttf'
            return (
                "@font-face{"
                f"font-family:'{face_name}';"
                f"src:url(data:{mime};base64,{b64}) format('truetype');"
                f"font-weight:{weight};font-style:normal;"
                "}"
            )
        except Exception:
            return ""

    # Embed a set of Pixel Operator faces if available
    known_files = [
        'PixelOperator.ttf','PixelOperator8.ttf','PixelOperator8-Bold.ttf','PixelOperator-Bold.ttf',
        'PixelOperatorHB.ttf','PixelOperatorHB8.ttf','PixelOperatorHBSC.ttf',
        'PixelOperatorMono.ttf','PixelOperatorMono8.ttf','PixelOperatorMono8-Bold.ttf','PixelOperatorMono-Bold.ttf','PixelOperatorMonoHB.ttf','PixelOperatorMonoHB8.ttf',
        'PixelOperatorSC.ttf','PixelOperatorSC-Bold.ttf'
    ]
    faces_css = []
    # Populate dropdown lists from known names regardless of embedding success
    ui_fonts = [Path(f).stem for f in known_files if 'Mono' not in Path(f).stem]
    mono_fonts = [Path(f).stem for f in known_files if 'Mono' in Path(f).stem]
    # Try to embed any fonts that exist locally
    for fname in known_files:
        face = Path(fname).stem
        css = _embed_font(face, fname, 'bold' if 'Bold' in face else 'normal')
        if css:
            faces_css.append(css)
    pixel_faces_css = "\n".join(faces_css)

    html = (
        tmpl
        .replace("__TITLE__", escape_html(args.title))
        .replace("""__DATA_JSON__""", json.dumps(out))
        .replace("__FLAME_SVG__", flame_svg)
        .replace("""__FLAME_JSON__""", json.dumps(flame_json) if flame_json else "null")
        .replace("""__SOURCES_JSON__""", sources_json or "null")
        .replace("""__STATUS_JSON__""", json.dumps(status))
        .replace("""__ASM_JSON__""", json.dumps(asm_map) if asm_map else "null")
        .replace("""__ASM_MIX_JSON__""", json.dumps(asm_mix) if asm_mix else "null")
        .replace("__LOGO_IMG__", _load_logo_data_uri())
        .replace("/*__PIXEL_FACES__*/", pixel_faces_css)
        .replace("__FONTS_JSON__", json.dumps({"ui": ui_fonts, "mono": mono_fonts}))
    )
    out_dir.mkdir(parents=True, exist_ok=True)
    index_path = out_dir / "index.html"
    index_path.write_text(html, encoding="utf-8")
    print(f"✅ Wrote single-file viewer to {index_path}")
    try_open(index_path, no_open=args.no_open)


def addr2line_create(elf: Path, *, toolchain_prefix: Optional[str], src_base: Optional[Path], sources: Optional[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    cmd = None
    tc = toolchain_cmd("addr2line", toolchain_prefix)
    if tc and which(tc):
        cmd = tc
    elif which("llvm-addr2line"):
        cmd = "llvm-addr2line"
    elif which("addr2line"):
        cmd = "addr2line"
    if not cmd:
        return None
    base = src_base.resolve() if src_base else None
    basename_index: Dict[str, Optional[str]] = {}
    if sources and isinstance(sources.get("files"), dict):
        for k in sources["files"].keys():
            b = Path(k).name
            if b in basename_index and basename_index[b] != k:
                basename_index[b] = None
            else:
                basename_index[b] = k
    return {
        "cmd": cmd,
        "elf": str(elf),
        "cache": {},  # addr(int) -> (file(str)|None, line(int)|None)
        "src_base": str(base) if base else None,
        "basename_index": basename_index,
    }


def addr2line_resolve(addr2: Dict[str, Any], addrs: List[int]) -> None:
    cache: Dict[int, Any] = addr2["cache"]
    todo = [a for a in addrs if a not in cache]
    if not todo:
        return
    cmd = [addr2["cmd"], "-e", addr2["elf"]] + [hex(a) for a in todo]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
    except Exception:
        for a in todo:
            cache[a] = (None, None)
        return
    lines = out.splitlines()
    if len(lines) != len(todo):
        # best-effort map what we can
        lines = (lines + ["??:0"] * len(todo))[: len(todo)]
    src_base = Path(addr2["src_base"]).resolve() if addr2.get("src_base") else None
    basename_index: Dict[str, Optional[str]] = addr2.get("basename_index") or {}
    for a, ln in zip(todo, lines):
        ln = (ln or "").strip()
        file_part = None
        line_part: Optional[int] = None
        if ":" in ln:
            fp, lp = ln.rsplit(":", 1)
            fp = fp.strip()
            lp = lp.strip()
            if fp and fp != "??":
                file_part = fp
            try:
                line_part = int(lp)
            except Exception:
                line_part = None
        if not file_part or not line_part or line_part <= 0:
            cache[a] = (None, None)
            continue
        # Normalize file to match embedded sources keys where possible
        norm_file = file_part
        try:
            p = Path(file_part)
            if src_base and p.is_absolute():
                rel = p.resolve().relative_to(src_base)
                norm_file = rel.as_posix()
        except Exception:
            pass
        # If embedded sources exist, attempt to match by basename when needed
        b = Path(norm_file).name
        if basename_index and norm_file not in basename_index.values():
            k = basename_index.get(b)
            if isinstance(k, str):
                norm_file = k
        cache[a] = (norm_file, line_part)

def addr2line_annotate_slice(addr2: Dict[str, Any], slice_rows: List[Dict[str, Any]]) -> None:
    addrs: List[int] = []
    for r in slice_rows:
        if r.get("file") and r.get("line"):
            continue
        a = r.get("address")
        if isinstance(a, str):
            try:
                addrs.append(int(a, 16))
            except Exception:
                continue
    addr2line_resolve(addr2, addrs)
    cache: Dict[int, Any] = addr2["cache"]
    for r in slice_rows:
        if r.get("file") and r.get("line"):
            continue
        a = r.get("address")
        if not isinstance(a, str):
            continue
        try:
            ai = int(a, 16)
        except Exception:
            continue
        fp, lp = cache.get(ai, (None, None))
        if fp and lp:
            r["file"] = fp
            r["line"] = lp


def source_lookup_build(sources: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    return {
        "sources": sources,
        "full_cache": {},  # file -> list[str]
    }


def source_lookup_line(lookup: Dict[str, Any], file: str, line: int) -> Optional[str]:
    sources = lookup.get("sources")
    if not sources or not isinstance(sources.get("files"), dict):
        return None
    rec = sources["files"].get(file)
    if not isinstance(rec, dict):
        return None
    if "content" in rec:
        cache: Dict[str, Any] = lookup["full_cache"]
        lines = cache.get(file)
        if lines is None:
            lines = str(rec.get("content") or "").splitlines()
            cache[file] = lines
        if line <= 0 or line > len(lines):
            return None
        return lines[line - 1]
    if "ranges" in rec and isinstance(rec.get("ranges"), list):
        for rng in rec["ranges"]:
            if not isinstance(rng, dict):
                continue
            start = rng.get("start")
            content = rng.get("content")
            if not isinstance(start, int) or not isinstance(content, str):
                continue
            chunk_lines = content.splitlines()
            end = start + len(chunk_lines) - 1
            if start <= line <= end:
                return chunk_lines[line - start]
    return None


def addr2line_build_mixed(addr2: Dict[str, Any], seq_window: List[Dict[str, Any]], pc: int, sources: Optional[Dict[str, Any]]) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    lookup = source_lookup_build(sources)
    # Resolve file/line for each instruction in the window
    insn_addrs: List[int] = []
    for e in seq_window:
        if e.get("type") == "insn":
            a = e.get("address")
            if isinstance(a, int):
                insn_addrs.append(a)
    addr2line_resolve(addr2, insn_addrs)
    cache: Dict[int, Any] = addr2["cache"]
    last_file = None
    last_line = None
    for e in seq_window:
        if e.get("type") != "insn":
            continue
        a = e.get("address")
        if not isinstance(a, int):
            continue
        fp, lp = cache.get(a, (None, None))
        if fp and lp and (fp != last_file or lp != last_line):
            src_text = source_lookup_line(lookup, fp, lp) or ""
            out.append({"kind": "src", "file": fp, "line": lp, "text": src_text})
            last_file = fp
            last_line = lp
        out.append({
            "kind": "insn",
            "address": f"0x{a:06x}",
            "text": e.get("text"),
            "file": fp,
            "line": lp,
            "isPC": (a == pc),
        })
    return out


# ---------------- Flame graph generation -----------------

def build_flame_svg(entries: List[Dict[str, Any]], *, metric: str = "cycles") -> str:
    """Build a static flame graph SVG string from entries using function_chain.
    entries: list of dicts with keys function_chain, file, line, cycles, count
    metric: 'cycles' or 'count'
    """
    # Build tree from stacks
    class Node:
        __slots__ = ("name", "children", "value")
        def __init__(self, name: str):
            self.name = name
            self.children: Dict[str, Node] = {}
            self.value = 0.0

    root = Node("root")
    total = 0.0
    for e in entries:
        w = float(e.get(metric, 0) or 0)
        if w <= 0:
            continue
        chain = e.get("function_chain")
        if isinstance(chain, str) and chain.strip():
            parts = [p.strip() for p in chain.split("->") if p.strip()]
        else:
            # Fallback: single-level using file:line
            parts = [f"{e.get('file','?')}:{e.get('line','?')}"]
        cur = root
        for p in parts:
            cur = cur.children.setdefault(p, Node(p))
        cur.value += w
        total += w

    # Aggregate up values
    def aggregate(n: Node) -> float:
        s = n.value
        for c in n.children.values():
            s += aggregate(c)
        n.value = s
        return s
    aggregate(root)

    # Layout: compute rectangles (x,y,w,h) per node
    bar_h = 18
    pad_x = 2
    pad_y = 2
    depth_max = 0
    rects: List[Dict[str, Any]] = []

    def layout(n: Node, x0: float, y: int, scale: float, depth: int):
        nonlocal depth_max
        depth_max = max(depth_max, depth)
        # children sorted by value
        x = x0
        for name, c in sorted(n.children.items(), key=lambda kv: kv[1].value, reverse=True):
            w = c.value * scale
            if w <= 0:
                continue
            rects.append({
                "name": name,
                "x": x,
                "y": y,
                "w": w,
                "h": bar_h,
                "value": c.value,
            })
            layout(c, x, y + bar_h + pad_y, scale, depth + 1)
            x += w

    if total <= 0:
        return "<!-- No data for flame graph -->"
    width = 1200.0
    layout(root, 0.0, 0, width / total, 0)
    height = (depth_max + 1) * (bar_h + pad_y)

    # Color helper: hash name -> hue
    def color(name: str) -> str:
        h = 0
        for ch in name:
            h = (h * 131 + ord(ch)) & 0xFFFFFFFF
        hue = h % 360
        sat = 65
        lum = 55
        return f"hsl({hue} {sat}% {lum}%)"

    # Build SVG
    svg_parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="100%" role="img" aria-label="Flame graph">',
        '<style>text{font:11px ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto} .fg-rect{stroke:#111;stroke-width:.5;}</style>'
    ]
    for r in rects:
        name = r["name"]
        x = r["x"]
        y = r["y"]
        w = r["w"]
        h = r["h"]
        fill = color(name)
        label = name
        svg_parts.append(f'<g class="fg-item">')
        svg_parts.append(f'<rect class="fg-rect" x="{x:.2f}" y="{y}" width="{w:.2f}" height="{h}" fill="{fill}"><title>{escape_xml(label)} • {r["value"]:.0f}</title></rect>')
        # Text label if wide enough
        if w >= 35:
            # Clip to rect
            svg_parts.append(f'<clipPath id="clip-{abs(hash((name,x,y)))}"><rect x="{x+2:.2f}" y="{y}" width="{max(0,w-4):.2f}" height="{h}"></rect></clipPath>')
            svg_parts.append(f'<text x="{x+4:.2f}" y="{y+h-5}" fill="#000" clip-path="url(#clip-{abs(hash((name,x,y)))})">{escape_xml(shorten(label, max_chars=int(w/7)))}</text>')
        svg_parts.append('</g>')
    svg_parts.append('</svg>')
    return "".join(svg_parts)


def build_flame_tree(entries: List[Dict[str, Any]], *, metric: str = "cycles") -> Dict[str, Any]:
    class Node:
        __slots__ = ("name", "children", "value")
        def __init__(self, name: str):
            self.name = name
            self.children: Dict[str, Node] = {}
            self.value = 0.0

    root = Node("root")
    for e in entries:
        w = float(e.get(metric, 0) or 0)
        if w <= 0:
            continue
        chain = e.get("function_chain")
        if isinstance(chain, str) and chain.strip():
            parts = [p.strip() for p in chain.split("->") if p.strip()]
        else:
            parts = [f"{e.get('file','?')}:{e.get('line','?')}"]
        cur = root
        for p in parts:
            cur = cur.children.setdefault(p, Node(p))
        cur.value += w

    def to_dict(n: Node) -> Dict[str, Any]:
        return {
            "name": n.name,
            "value": n.value,
            "children": [to_dict(c) for _, c in sorted(n.children.items(), key=lambda kv: kv[1].value, reverse=True)],
        }

    return to_dict(root)


def escape_xml(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def shorten(s: str, max_chars: int) -> str:
    if len(s) <= max_chars:
        return s
    if max_chars <= 3:
        return s[:max_chars]
    return s[: max_chars - 3] + "…"


def try_open(index_path: Path, *, no_open: bool = False) -> None:
    """Open the generated HTML in the default browser.
    - macOS: runs `open <file>` (as requested)
    - Windows: uses os.startfile
    - Linux/Unix: runs `xdg-open <file>`
    Pass --no-open to skip.
    """
    if no_open:
        return
    p = str(index_path)
    try:
        if sys.platform == "darwin":
            subprocess.Popen(["open", p])
        elif sys.platform.startswith("win"):
            os.startfile(p)  # type: ignore[attr-defined]
        else:
            subprocess.Popen(["xdg-open", p])
    except Exception:
        try:
            import webbrowser
            webbrowser.open(index_path.as_uri())
        except Exception:
            pass


def embed_sources(entries: List[Entry], src_base: Path, *, mode: str = "full", context_lines: int = 12) -> Dict[str, Any]:
    """Embed source contents for files referenced in entries.
    mode:
      - "full": embed entire file
      - "context": embed only ranges around referenced lines (+/- context_lines)
    Returns a dict:
      { base: <abs>, files: { filename: { path, content } | { path, ranges:[{start, content}] } } }
    """
    by_file: Dict[str, List[int]] = {}
    for e in entries:
        by_file.setdefault(e.file, []).append(e.line)
        # include inline frames
        if e.function_chain_frames:
            for fr in e.function_chain_frames:
                ffile = fr.get("file")
                fline = fr.get("line")
                if isinstance(ffile, str) and isinstance(fline, int):
                    by_file.setdefault(ffile, []).append(fline)

    files_map: Dict[str, Any] = {}
    for fname, lines in sorted(by_file.items()):
        path = locate_source(src_base, fname)
        if not path:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="latin-1")
        if mode == "full":
            files_map[fname] = {"path": str(path.resolve()), "content": text}
        else:
            # Build merged context ranges
            total_lines = text.count("\n") + 1
            ranges = []  # list of (start,end) inclusive 1-based
            for ln in sorted(set(lines)):
                s = max(1, ln - context_lines)
                e = min(total_lines, ln + context_lines)
                if not ranges:
                    ranges.append([s, e])
                else:
                    ps, pe = ranges[-1]
                    if s <= pe + 1:
                        ranges[-1][1] = max(pe, e)
                    else:
                        ranges.append([s, e])
            # Slice content per range
            text_lines = text.splitlines()
            out_ranges = []
            for s, e in ranges:
                # Convert to 0-based slice
                chunk = "\n".join(text_lines[s - 1 : e])
                out_ranges.append({"start": s, "content": chunk})
            files_map[fname] = {"path": str(path.resolve()), "ranges": out_ranges}

    return {"base": str(src_base.resolve()), "files": files_map}


def locate_source(src_base: Path, fname: str) -> Optional[Path]:
    """Attempt to resolve a source file under src_base using relative path or basename search."""
    p = (src_base / fname)
    if p.exists():
        return p
    # Search by basename
    target = Path(fname).name
    matches = list(src_base.rglob(target))
    if matches:
        # Heuristic: prefer exact relative path depth closest to provided
        return matches[0]
    return None


# ---------------- Disassembly helpers -----------------

def which(cmd: str) -> Optional[str]:
    for p in os.environ.get("PATH", "").split(os.pathsep):
        cand = Path(p) / cmd
        if cand.exists() and os.access(str(cand), os.X_OK):
            return str(cand)
    return None


def toolchain_cmd(tool: str, prefix: Optional[str]) -> Optional[str]:
    """Build a cross-tool command from the provided toolchain prefix.

    Accepts either "m68k-foo" or "m68k-foo-" styles.
    """
    prefix = (prefix or "").strip()
    if not prefix:
        return None
    return f"{prefix}{tool}" if prefix.endswith("-") else f"{prefix}-{tool}"


def build_disasm_index(elf: Path, *, toolchain_prefix: Optional[str] = None) -> (Dict[int, Dict[str, Any]], Dict[str, Any], List[Dict[str, Any]]):
    """Run objdump/llvm-objdump and return a mapping of address -> instruction text.
    Keeps only addresses with instructions to enable slicing around PCs.
    """
    cmds = []
    tc_objdump = toolchain_cmd("objdump", toolchain_prefix)
    if tc_objdump and which(tc_objdump):
        cmds.append([tc_objdump, "-dS", "-C", "-l", str(elf)])
    # LLVM generic (use -S/--source and show addresses, line numbers)
    if which("llvm-objdump"):
        cmds.append(["llvm-objdump", "-dS", "--source", "--demangle", "--no-show-raw-insn", "--line-numbers", "--addresses", str(elf)])
    # GNU generic
    if which("objdump"):
        cmds.append(["objdump", "-dS", "-C", "-l", str(elf)])
    out = None
    used_cmd = None
    for cmd in cmds:
        try:
            out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
            if out:
                used_cmd = str(Path(cmd[0]).name)
                break
        except Exception:
            continue
    if not out:
        return {}, {"ok": False, "tool": None, "mixedCandidate": False}, []

    lines = out.splitlines()
    m: Dict[int, Dict[str, Any]] = {}
    seq: List[Dict[str, Any]] = []
    import re
    addr_line_re = re.compile(r"^\s*([0-9a-fA-F]+):")
    file_re = re.compile(r"([^\s:][^:]*\.[A-Za-z0-9_]+):([0-9]{1,7})")
    file_only_re = re.compile(r"^\s*([^\s].*\.[A-Za-z0-9_]+):\s*$")
    line_only_re = re.compile(r"^\s*([0-9]{1,7})\b")
    mixed_candidate = False
    cur_file: Optional[str] = None
    cur_line: Optional[int] = None
    for ln in lines:
        ln = ln.rstrip()
        s = ln.strip()

        # Track source mapping lines from -S output. These commonly look like:
        #   /path/to/file.c:123
        # and MUST be processed before instruction parsing, since they contain ':'.
        m2 = file_re.match(s)
        if m2 and not addr_line_re.match(ln):
            cur_file = m2.group(1)
            try:
                cur_line = int(m2.group(2))
            except Exception:
                cur_line = None
            rest = s[m2.end():].strip()
            if rest:
                seq.append({"type": "src", "file": cur_file, "line": cur_line, "text": rest})
            continue

        mfo = file_only_re.match(s)
        if mfo and not addr_line_re.match(ln):
            cur_file = mfo.group(1)
            # line will follow on next line-only entries
            continue

        mlo = line_only_re.match(ln)
        if mlo and cur_file and not addr_line_re.match(ln):
            try:
                cur_line = int(mlo.group(1))
            except Exception:
                cur_line = None
            rest = ln[mlo.end():].rstrip()
            if rest:
                seq.append({"type": "src", "file": cur_file, "line": cur_line, "text": rest.strip()})
            continue

        # Match instruction addresses like: "  2cc44:" or "0002cc44:" etc.
        ma = addr_line_re.match(ln)
        if ma:
            try:
                addr = int(ma.group(1), 16)
            except Exception:
                addr = None
            if addr is None:
                continue
            tail = ln[ma.end():]
            ins = tail.strip()

            # Use the most recent source mapping seen (from -S output)
            src_file = cur_file
            src_line = cur_line
            # Also accept file:line at end of instruction line
            mobj = file_re.search(ln)
            if mobj:
                src_file = mobj.group(1)
                try:
                    src_line = int(mobj.group(2))
                except Exception:
                    src_line = None
            if ins:
                m[addr] = {"text": ins, "file": src_file, "line": src_line}
                if src_file and src_line and src_line > 0:
                    mixed_candidate = True
                seq.append({"type": "insn", "address": addr, "text": ins, "file": src_file, "line": src_line})
            continue

        # Bare source text line
        if s and not s.endswith(":"):
            seq.append({"type": "src", "file": cur_file, "line": cur_line, "text": s})
    return m, {"ok": True, "tool": used_cmd, "mixedCandidate": mixed_candidate}, seq


def disasm_slice(index: Dict[int, Dict[str, Any]], pc: int, *, context: int = 24) -> List[Dict[str, Any]]:
    if not index:
        return []
    addrs = sorted(index.keys())
    # Find nearest or exact address
    import bisect
    i = bisect.bisect_left(addrs, pc)
    if i >= len(addrs):
        i = len(addrs) - 1
    # Choose window
    start = max(0, i - context)
    end = min(len(addrs), i + context + 1)
    out: List[Dict[str, Any]] = []
    for a in addrs[start:end]:
        rec = index.get(a, {})
        out.append({
            "address": f"0x{a:06x}",
            "text": rec.get("text", ""),
            "isPC": (a == pc),
            "file": rec.get("file"),
            "line": rec.get("line"),
        })
    return out


def build_disasm_index_from_rom(rom: Path, vma_base: int, *, toolchain_prefix: Optional[str] = None) -> (Dict[int, Dict[str, Any]], Dict[str, Any], List[Dict[str, Any]]):
    """Disassemble a raw ROM using objdump in binary mode, return address->insn map.
    Requires objdump with m68k support. Addresses will be vma_base + offset.
    """
    cmds = []
    tc_objdump = toolchain_cmd("objdump", toolchain_prefix)
    if tc_objdump and which(tc_objdump):
        cmds.append([tc_objdump, "-b", "binary", "-m", "m68k", "--adjust-vma", hex(vma_base), "-D", str(rom)])
    if which("objdump"):
        cmds.append(["objdump", "-b", "binary", "-m", "m68k", "--adjust-vma", hex(vma_base), "-D", str(rom)])
    out = None
    used_cmd = None
    for cmd in cmds:
        try:
            out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL, text=True)
            if out:
                used_cmd = str(Path(cmd[0]).name)
                break
        except Exception:
            continue
    if not out:
        return {}, {"ok": False, "tool": None, "mixedCandidate": False}, []
    # Reuse the same parser as build_disasm_index
    lines = out.splitlines()
    m: Dict[int, Dict[str, Any]] = {}
    seq: List[Dict[str, Any]] = []
    for ln in lines:
        ln = ln.rstrip()
        if ":" in ln:
            parts = ln.split(":", 1)
            addr_str = parts[0].strip()
            try:
                addr = int(addr_str, 16)
            except ValueError:
                continue
            ins = parts[1].strip()
            if ins:
                m[addr] = {"text": ins, "file": None, "line": None}
                seq.append({"type": "insn", "address": addr, "text": ins, "file": None, "line": None})
        else:
            s = ln.strip()
            if s and not s.endswith(":"):
                seq.append({"type": "src", "file": None, "line": None, "text": s})
    return m, {"ok": True, "tool": used_cmd, "mixedCandidate": False}, seq


if __name__ == "__main__":
    main()
