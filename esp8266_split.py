#!/usr/bin/env python3
"""Decoupe un dump flash complet d'ESP8266 en ses differentes zones.

Usage :
    python esp8266_split.py backup.img                 # affiche la carte et ecrit ./backup_split/
    python esp8266_split.py backup.img -o sortie       # choisit le dossier de sortie
    python esp8266_split.py backup.img --segments      # ecrit aussi chaque segment de chaque image
    python esp8266_split.py backup.img --list          # affiche la carte sans rien ecrire

Zones reconnues :
  - images executables (magic 0xE9, ou 0xEA pour le format OTA du SDK Espressif),
    validees par leurs adresses de chargement et leur somme de controle ;
  - secteurs systeme en fin de flash (EEPROM Arduino, donnees RF, config WiFi du SDK) ;
  - zones vides (0xFF), signalees mais non ecrites ;
  - tout le reste est exporte en "data" (systeme de fichiers, donnees inconnues...).
"""
import argparse
import os
import struct
import sys

SECTOR = 0x1000

# Plages d'adresses memoire valides pour un segment ESP8266
VALID_RANGES = [
    (0x3FFE8000, 0x40000000, "dram"),    # .data / .rodata / .bss
    (0x40100000, 0x40110000, "iram"),    # code en RAM (le bootloader se loge en haut)
    (0x40200000, 0x40300000, "irom"),    # code execute depuis la flash
]

# Secteurs systeme, comptes depuis la fin de la flash
TAIL_SECTORS = {
    5: "eeprom_arduino",       # EEPROM.h du core Arduino
    4: "rf_init_data",         # esp_init_data_default (calibration RF)
    3: "sdk_config_a",         # config WiFi du SDK (SSID / mot de passe), copie 1
    2: "sdk_config_b",         # idem, copie 2
    1: "sdk_config_selector",  # indique quelle copie est active
}


def region_name(addr):
    for lo, hi, name in VALID_RANGES:
        if lo <= addr < hi:
            return name
    return None


def parse_segments(data, pos, count):
    """Lit `count` segments a partir de `pos`. Renvoie (segments, fin) ou None."""
    segs = []
    for _ in range(count):
        if pos + 8 > len(data):
            return None
        addr, length = struct.unpack_from("<II", data, pos)
        kind = region_name(addr)
        if kind is None or length == 0 or length > 0x100000 or pos + 8 + length > len(data):
            return None
        segs.append({"addr": addr, "len": length, "offset": pos + 8, "kind": kind})
        pos += 8 + length
    return segs, pos


def checksum_ok(data, segs, end):
    """Somme de controle : XOR de tous les octets des segments, graine 0xEF,
    stockee dans le dernier octet apres alignement sur 16."""
    chk = 0xEF
    for s in segs:
        for b in data[s["offset"]:s["offset"] + s["len"]]:
            chk ^= b
    padded = (end + 16) & ~15            # au moins 1 octet de bourrage
    if padded > len(data):
        return False, end
    return data[padded - 1] == chk, padded


def parse_image(data, off):
    """Tente de lire une image a l'offset `off`. Renvoie un dict ou None."""
    if off + 16 > len(data):
        return None
    magic, nseg, _mode, _sizefreq, entry = struct.unpack_from("<BBBBI", data, off)
    segs = []
    pos = off + 8

    if magic == 0xEA:
        # Format OTA "v2" : un bloc irom (adresse 0) puis une image 0xE9 classique
        if nseg != 4:
            return None
        addr, length = struct.unpack_from("<II", data, pos)
        if addr != 0 or length == 0 or pos + 8 + length + 8 > len(data):
            return None
        # La flash est mappee par fenetre de 1 Mo a partir de 0x40200000
        segs.append({"addr": 0x40200000 + (off % 0x100000) + 16, "len": length,
                     "offset": pos + 8, "kind": "irom"})
        pos += 8 + length
        magic2, nseg, _m, _s, entry = struct.unpack_from("<BBBBI", data, pos)
        if magic2 != 0xE9:
            return None
        pos += 8
        fmt = "ota_v2"
    elif magic == 0xE9:
        fmt = "standard"
    else:
        return None

    if not 1 <= nseg <= 16 or region_name(entry) not in ("iram", "irom"):
        return None
    parsed = parse_segments(data, pos, nseg)
    if parsed is None:
        return None
    more, end = parsed
    # Le bloc irom du format OTA n'entre pas dans la somme de controle
    ok, end = checksum_ok(data, more, end)
    segs += more
    return {"offset": off, "end": end, "format": fmt, "entry": entry,
            "segments": segs, "checksum_ok": ok}


def classify_image(img, index):
    if img["offset"] == 0 and img["end"] <= SECTOR:
        return "bootloader"
    return "app%d" % index


def build_map(data):
    """Renvoie la liste ordonnee des zones : dicts {offset, end, name, type, ...}."""
    size = len(data)
    zones = []

    # 1. Images executables, cherchees a chaque debut de secteur
    off, app_index = 0, 1
    while off < size:
        img = parse_image(data, off) if data[off] in (0xE9, 0xEA) else None
        if img:
            name = classify_image(img, app_index)
            if name != "bootloader":
                app_index += 1
            img.update(name=name, type="image")
            zones.append(img)
            off = (img["end"] + SECTOR - 1) & ~(SECTOR - 1)
        else:
            off += SECTOR

    # 2. Secteurs systeme en fin de flash
    taken = [(z["offset"], z["end"]) for z in zones]
    for n, name in TAIL_SECTORS.items():
        start = size - n * SECTOR
        if start < 0 or any(a < start + SECTOR and start < b for a, b in taken):
            continue
        if data[start:start + SECTOR] == b"\xff" * SECTOR:
            continue
        zones.append({"offset": start, "end": start + SECTOR, "name": name, "type": "system"})

    # 3. Le reste : regroupe les secteurs contigus vides (0xFF) ou non
    zones.sort(key=lambda z: z["offset"])
    filled, cursor = [], 0
    for z in zones + [{"offset": size, "end": size, "type": "end"}]:
        gap_start, gap_end = cursor, z["offset"]
        # Ignore le bourrage 0xFF entre la fin d'une image et le secteur suivant
        aligned = min((gap_start + SECTOR - 1) & ~(SECTOR - 1), gap_end)
        if data[gap_start:aligned].count(0xFF) == aligned - gap_start:
            gap_start = aligned
        run_start, run_blank = gap_start, None
        pos = gap_start
        while pos < gap_end:
            nxt = min((pos // SECTOR + 1) * SECTOR, gap_end)
            blank = data[pos:nxt].count(0xFF) == nxt - pos
            if run_blank is None:
                run_blank = blank
            elif blank != run_blank:
                filled.append({"offset": run_start, "end": pos,
                               "name": "vide" if run_blank else "data",
                               "type": "blank" if run_blank else "data"})
                run_start, run_blank = pos, blank
            pos = nxt
        if run_blank is not None:
            filled.append({"offset": run_start, "end": gap_end,
                           "name": "vide" if run_blank else "data",
                           "type": "blank" if run_blank else "data"})
        if z["type"] != "end":
            filled.append(z)
        cursor = max(cursor, z["end"])
    return filled


def main():
    ap = argparse.ArgumentParser(description="Decoupe un dump flash ESP8266 en zones.")
    ap.add_argument("dump", help="fichier dump complet (.bin / .img)")
    ap.add_argument("-o", "--outdir", help="dossier de sortie (defaut : <dump>_split)")
    ap.add_argument("--segments", action="store_true",
                    help="ecrit aussi chaque segment des images dans un fichier separe")
    ap.add_argument("--list", action="store_true", help="affiche la carte sans ecrire de fichier")
    args = ap.parse_args()

    with open(args.dump, "rb") as f:
        data = f.read()
    if len(data) % SECTOR:
        print("Attention : taille non multiple de 4096, dump peut-etre tronque.", file=sys.stderr)

    zones = build_map(data)
    outdir = args.outdir or os.path.splitext(args.dump)[0] + "_split"
    if not args.list:
        os.makedirs(outdir, exist_ok=True)

    print("Dump : %s (%d octets, %.1f Mo)\n" % (args.dump, len(data), len(data) / 2**20))
    print("%-10s %-10s %-9s %s" % ("debut", "fin", "taille", "zone"))
    for z in zones:
        length = z["end"] - z["offset"]
        line = "0x%06X   0x%06X   %-9d %s" % (z["offset"], z["end"], length, z["name"])
        if z["type"] == "image":
            line += "  [%s, entree 0x%08X, checksum %s]" % (
                z["format"], z["entry"], "OK" if z["checksum_ok"] else "INVALIDE")
        print(line)

        if z["type"] == "image":
            for i, s in enumerate(z["segments"]):
                print("    segment %d : %-4s 0x%08X  %6d octets  (offset dump 0x%06X)"
                      % (i, s["kind"], s["addr"], s["len"], s["offset"]))

        if args.list or z["type"] == "blank":
            continue
        base = "%06X_%s" % (z["offset"], z["name"])
        with open(os.path.join(outdir, base + ".bin"), "wb") as f:
            f.write(data[z["offset"]:z["end"]])
        if args.segments and z["type"] == "image":
            for i, s in enumerate(z["segments"]):
                fn = "%s_seg%d_%s_%08X.bin" % (base, i, s["kind"], s["addr"])
                with open(os.path.join(outdir, fn), "wb") as f:
                    f.write(data[s["offset"]:s["offset"] + s["len"]])

    if not args.list:
        print("\nFichiers ecrits dans : %s" % outdir)


if __name__ == "__main__":
    main()
