#!/usr/bin/env python3
"""Build a compact SQLite catalog from the Z-Wheel launcher assets.

The Z-Wheel package (roms/274755) ships an offline catalog under
`assets/games/<DSL game_id>/` with `boxartlg.jpg`, `boxart.bmp`, `rating.jpg`
and `description_{en,es,pt}.txt` per game. Those folders are keyed by the
shop/DSL game id, not by the package folder id or the BREW class id, so this
tool re-keys them to the real catalog (status/catalog-roms.md: name + CLSID +
folder game_id) by matching the catalog game name inside each description.

Output is a single SQLite file with one row per matched game and the box art /
descriptions stored as zlib-compressed blobs, so a launcher can attach it and
show full box art + descriptions per CLSID without shipping the raw asset tree.

This is an offline inspection/tooling step (run on the dev machine); it does not
run in the emulator and reads only local research assets.

Usage:
    python tools/misc/zwheel_catalog.py [--assets <dir>] [--catalog <md>]
                                        [--out zwheel_catalog.sqlite]
"""
import argparse
import glob
import os
import re
import sqlite3
import zlib


def parse_catalog(md_path):
    """Parse status/catalog-roms.md -> list of (folder_game_id, clsid, name)."""
    rows = []
    row_re = re.compile(r"^\|\s*([0-9]+|\*\(unknown\)\*)\s*\|\s*`([0-9A-Fa-f]+)`\s*\|\s*(.+?)\s*\|")
    with open(md_path, encoding="utf-8") as fh:
        for line in fh:
            m = row_re.match(line)
            if not m:
                continue
            gid_raw, clsid, name = m.group(1), m.group(2).upper(), m.group(3)
            # strip parenthetical notes like "*(included in ...)*"
            name = re.sub(r"\*\(.*?\)\*", "", name).strip()
            folder_id = int(gid_raw) if gid_raw.isdigit() else None
            rows.append((folder_id, clsid, name))
    return rows


def parse_minmemnode(path):
    """Parse minmemnode.dat -> list of (clsid, name). Lines are `;GameName`
    followed by `0xCLSID = nodesize`. These short clean names ("Baja",
    "Pacmania") often match the marketing descriptions better than the catalog's
    full titles, so they are merged into the match candidates."""
    rows = []
    name = None
    if not os.path.isfile(path):
        return rows
    with open(path, encoding="latin-1") as fh:
        for line in fh:
            s = line.strip()
            cm = re.match(r"^;\s*([A-Za-z0-9].*)$", s)
            if cm and "specify" not in s.lower() and "DEFAULT" not in s:
                name = cm.group(1).strip()
                continue
            vm = re.match(r"^(0x[0-9A-Fa-f]+)\s*=", s)
            if vm and name:
                rows.append((vm.group(1)[2:].upper(), name))
                name = None
    return rows


def read_tt_game_info(path):
    """Read the launcher's own SQLite game DB (tt_game_info) - the AUTHORITATIVE
    DSL game_id -> class_id -> name mapping the guest built. GAMEINFO has
    game_id + class_id (decimal) + boxart_path; TITLETEXT has game_id ->
    titletext (per language). Returns {dsl_game_id: (clsid_hex, name)}.
    Beats fuzzy description matching - this is exactly what the launcher uses."""
    out = {}
    if not os.path.isfile(path):
        return out
    db = sqlite3.connect(path)
    names = {}
    try:
        for gid, txt in db.execute("SELECT game_id, titletext FROM TITLETEXT"):
            names.setdefault(str(gid), txt)  # first lang wins; names match across langs
        for gid, cid in db.execute("SELECT game_id, class_id FROM GAMEINFO"):
            out[str(gid)] = ("%08X" % int(cid), names.get(str(gid)))
    except sqlite3.Error:
        pass
    db.close()
    return out


def load_overrides(path):
    """Optional manual DSL_game_id -> CLSID map (CSV: dsl_id,clsid[,name]) for the
    titles whose marketing description carries no usable game name."""
    out = {}
    if not path or not os.path.isfile(path):
        return out
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.replace("\t", ",").split(",")]
            if len(parts) >= 2 and parts[0].isdigit():
                out[parts[0]] = (parts[1].replace("0x", "").upper(),
                                 parts[2] if len(parts) >= 3 else None)
    return out


def norm(text):
    """Lowercase + strip accents/punctuation for fuzzy name containment."""
    text = text.lower()
    repl = {"á": "a", "à": "a", "ã": "a", "â": "a", "é": "e", "ê": "e",
            "í": "i", "ó": "o", "ô": "o", "õ": "o", "ú": "u", "ç": "c"}
    for k, v in repl.items():
        text = text.replace(k, v)
    return re.sub(r"[^a-z0-9 ]+", " ", text)


def read_descriptions(game_dir):
    descs = {}
    for lang in ("en", "es", "pt"):
        path = os.path.join(game_dir, "description_%s.txt" % lang)
        if os.path.isfile(path):
            # The description files are UTF-8 (Portuguese/Spanish accents). Read
            # them as UTF-8 so the stored TEXT is correct; reading as latin-1
            # double-encodes and shows mojibake ("TÃ­tulo") in the launcher.
            try:
                with open(path, "rb") as fh:
                    raw = fh.read()
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError:
                    text = raw.decode("latin-1")
                # Strip the leading store-availability notice, always wrapped in
                # "** ... **" (e.g. "** This title will only be available... **").
                text = re.sub(r"^\s*\*\*.*?\*\*\s*", "", text, flags=re.DOTALL)
                descs[lang] = text.strip()
            except OSError:
                pass
    return descs


def read_blob(game_dir, name):
    path = os.path.join(game_dir, name)
    if os.path.isfile(path):
        with open(path, "rb") as fh:
            return fh.read()
    return None


def match_game(descs, catalog_norm):
    """Return (folder_id, clsid, name) of the catalog game named in descs."""
    blob = norm(" ".join(descs.values()))
    best = None
    for key_norm, entry in catalog_norm:
        # require the catalog name (normalized) to appear as a phrase; prefer the
        # longest matching name so "Zeebo" does not beat "Zeebo Extreme Baja".
        if key_norm and key_norm in blob:
            if best is None or len(key_norm) > len(best[0]):
                best = (key_norm, entry)
    return best[1] if best else None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", default="roms/274755/mod/274755/assets/games",
                    help="Z-Wheel assets/games directory")
    ap.add_argument("--catalog", default="status/catalog-roms.md",
                    help="catalog markdown with name + CLSID + game_id")
    ap.add_argument("--out", default="catalog.sqlite",
                    help="output SQLite path")
    ap.add_argument("--minmemnode", default="roms/274755/mod/274755/minmemnode.dat",
                    help="minmemnode.dat for extra clean name->CLSID candidates")
    ap.add_argument("--overrides", default=None,
                    help="optional CSV (dsl_id,clsid[,name]) for titles whose "
                         "description has no usable game name")
    ap.add_argument("--gameinfo", default="roms/274755/mod/274755/tt_game_info",
                    help="launcher tt_game_info SQLite (authoritative "
                         "game_id->class_id->name); used before fuzzy matching")
    args = ap.parse_args()

    catalog = parse_catalog(args.catalog)
    # CLSID -> folder_game_id from the catalog, so a minmemnode/override match by
    # CLSID can still carry the folder id.
    clsid_to_fid = {clsid: fid for fid, clsid, _ in catalog}
    clsid_to_name = {clsid: name for _, clsid, name in catalog if name}
    # Candidate (normalized-name -> entry) from BOTH the catalog full titles and
    # the minmemnode short names; longest-first so "Zeebo Extreme Baja" wins over
    # "Baja" and specific names beat generic ones.
    cand = {}
    for fid, clsid, name in catalog:
        if name:
            cand[norm(name)] = (fid, clsid, clsid_to_name.get(clsid, name))
    for clsid, name in parse_minmemnode(args.minmemnode):
        key = norm(name)
        if key and key not in cand:
            cand[key] = (clsid_to_fid.get(clsid), clsid, clsid_to_name.get(clsid, name))
    catalog_norm = sorted(cand.items(), key=lambda x: -len(x[0]))
    overrides = load_overrides(args.overrides)
    gameinfo = read_tt_game_info(args.gameinfo)  # authoritative dsl_id -> (clsid, name)

    if os.path.exists(args.out):
        os.remove(args.out)
    db = sqlite3.connect(args.out)
    db.execute("""
        CREATE TABLE games (
            clsid TEXT PRIMARY KEY,
            name TEXT,
            folder_game_id INTEGER,
            dsl_game_id INTEGER,
            boxart_jpg BLOB,   -- zlib-compressed JPEG (boxartlg.jpg)
            boxart_bmp BLOB,   -- zlib-compressed BMP  (boxart.bmp)
            rating_jpg BLOB,   -- zlib-compressed JPEG (rating.jpg)
            desc_en TEXT, desc_es TEXT, desc_pt TEXT
        )""")

    matched = unmatched = 0
    for game_dir in sorted(glob.glob(os.path.join(args.assets, "*"))):
        if not os.path.isdir(game_dir):
            continue
        dsl_id = os.path.basename(game_dir.rstrip("/\\"))
        descs = read_descriptions(game_dir)
        if dsl_id in overrides:
            ov_clsid, ov_name = overrides[dsl_id]
            entry = (clsid_to_fid.get(ov_clsid), ov_clsid,
                     ov_name or clsid_to_name.get(ov_clsid, ov_clsid))
        elif dsl_id in gameinfo:
            # Authoritative: the launcher's own tt_game_info DB. Prefer the
            # catalog's fuller title by CLSID, fall back to the DB titletext.
            gi_clsid, gi_name = gameinfo[dsl_id]
            entry = (clsid_to_fid.get(gi_clsid), gi_clsid,
                     clsid_to_name.get(gi_clsid, gi_name or gi_clsid))
        else:
            entry = match_game(descs, catalog_norm)
        if not entry:
            unmatched += 1
            print("  unmatched DSL id %s" % dsl_id)
            continue
        fid, clsid, name = entry

        def comp(blob):
            return zlib.compress(blob, 9) if blob else None

        db.execute(
            "INSERT OR REPLACE INTO games VALUES (?,?,?,?,?,?,?,?,?,?)",
            (clsid, name, fid, int(dsl_id) if dsl_id.isdigit() else None,
             comp(read_blob(game_dir, "boxartlg.jpg")),
             comp(read_blob(game_dir, "boxart.bmp")),
             comp(read_blob(game_dir, "rating.jpg")),
             descs.get("en"), descs.get("es"), descs.get("pt")))
        matched += 1
        print("  %s -> 0x%s  %s" % (dsl_id, clsid, name))

    db.commit()
    db.close()
    size = os.path.getsize(args.out)
    print("\nWrote %s: %d games matched, %d unmatched (%.1f KB)"
          % (args.out, matched, unmatched, size / 1024.0))
    print("Blobs are zlib-compressed; decompress with zlib.decompress() before"
          " decoding the JPEG/BMP.")


if __name__ == "__main__":
    main()
