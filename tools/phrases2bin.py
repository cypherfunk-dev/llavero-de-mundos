#!/usr/bin/env python3
"""Convierte los .txt de frases (data/frases/*.txt) al binario que el llavero lee de la flash.

Formato de una línea (spec §5.2):
    [etiqueta, etiqueta, personaje]   Texto con {huecos}.
Las líneas vacías y las que empiezan por # se ignoran. Cada frase lleva exactamente un
personaje y como mucho un "propósito" (relleno, pregunta:agua, escena:existir:1...).
Todas las demás etiquetas son condiciones: la frase solo sale si se cumplen todas.

Salida: src/core/phrases_data.c con `const uint8_t PHRASE_BANK[]` (se lee directamente
de la flash, sin copiarlo a la RAM). El vocabulario sale de src/core/phrases_vocab.h.

Uso:
    tools/phrases2bin.py                 regenera src/core/phrases_data.c
    tools/phrases2bin.py --check         falla si el archivo generado no está al día
    tools/phrases2bin.py --stats         cuenta frases por personaje y propósito
    tools/phrases2bin.py --selftest      pruebas de la propia herramienta
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VOCAB_H = os.path.join(ROOT, "src", "core", "phrases_vocab.h")
DATA_DIR = os.path.join(ROOT, "data", "frases")
OUT_C = os.path.join(ROOT, "src", "core", "phrases_data.c")

MAGIC = b"LLVP"
VERSION = 1
MAX_TEXT = 110          # caracteres; el límite real (la caja de texto) lo comprueba el test del motor
MAX_TAGS = 12
HOLES = {"ausencia", "evento", "hora", "miradas", "racha", "estacion", "dia", "mascota", "pasos"}
# Qué condición garantiza que el hueco se puede rellenar (None: siempre se puede).
HOLE_NEEDS = {
    "ausencia": {"abs_gt", "abs_lt"},
    "evento": {"event"},
    "mascota": {"pet_kind"},
    "pasos": {"event"},
}


class PhraseError(Exception):
    pass


# ---------------------------------------------------------------- vocabulario

def read_vocab(path=VOCAB_H):
    text = open(path, encoding="utf-8").read()
    voc = {}
    for m in re.finditer(r"#define\s+(PV_\w+)\(X\)((?:[^\n]*\\\n)*[^\n]*)", text):
        name, body = m.group(1), m.group(2)
        if name == "PV_WORDS":
            voc[name] = [(w, k, int(a)) for w, k, a in
                         re.findall(r'X\(\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(-?\d+)\s*\)', body)]
        else:
            voc[name] = [(s, int(i), n) for s, i, n in
                         re.findall(r'X\(\s*(\w+)\s*,\s*(-?\d+)\s*,\s*"([^"]*)"\s*\)', body)]
    for need in ("PV_SPEAKERS", "PV_KINDS", "PV_TAGKINDS", "PV_WORDS", "PV_EVENTS",
                 "PV_QUESTIONS", "PV_HABITS", "PV_PETS"):
        if not voc.get(need):
            raise PhraseError("vocabulario: falta %s en %s" % (need, path))
    v = {}
    v["speaker"] = {n: i for _, i, n in voc["PV_SPEAKERS"]}
    v["kind"] = {n: i for _, i, n in voc["PV_KINDS"]}
    v["tagkind"] = {s: i for s, i, _ in voc["PV_TAGKINDS"]}
    v["event"] = {n: i for _, i, n in voc["PV_EVENTS"]}
    v["question"] = {n: i for _, i, n in voc["PV_QUESTIONS"]}
    v["habit"] = {n: i for _, i, n in voc["PV_HABITS"]}
    v["pet"] = {n: i for _, i, n in voc["PV_PETS"]}
    v["words"] = {}
    for w, k, a in voc["PV_WORDS"]:
        if k not in v["tagkind"]:
            raise PhraseError("vocabulario: la palabra %r usa un tipo desconocido %s" % (w, k))
        v["words"][w] = (v["tagkind"][k], a)
    return v


# ------------------------------------------------------------------- parseo

DUR = {"m": 1, "h": 60, "d": 1440}


def parse_duration(s, line):
    m = re.fullmatch(r"(\d+)([mhd])", s)
    if not m:
        raise PhraseError("%s: duración no válida %r (usa 5m, 2h o 3d)" % (line, s))
    v = int(m.group(1)) * DUR[m.group(2)]
    if v > 65535:
        raise PhraseError("%s: duración demasiado larga %r" % (line, s))
    return v


def parse_tags(raw, v, line, scenes):
    """Devuelve dict con speaker, kind, karg, kn, picaro y la lista de (tipo, arg)."""
    out = {"speaker": None, "kind": v["kind"]["charla"], "karg": 0, "kn": 0, "picaro": False,
           "tags": [], "kind_set": False, "has_event": False}
    tk = v["tagkind"]

    def purpose(kind_name, karg=0, kn=0):
        if out["kind_set"]:
            raise PhraseError("%s: más de un propósito (%s)" % (line, kind_name))
        out["kind_set"] = True
        out["kind"] = v["kind"][kind_name]
        out["karg"], out["kn"] = karg, kn

    def lookup(table, name, what):
        if name not in v[table]:
            raise PhraseError("%s: %s desconocido %r" % (line, what, name))
        return v[table][name]

    for tag in [t.strip() for t in raw.split(",")]:
        if not tag:
            raise PhraseError("%s: etiqueta vacía" % line)
        if tag in v["speaker"]:
            if out["speaker"] is not None:
                raise PhraseError("%s: más de un personaje" % line)
            out["speaker"] = v["speaker"][tag]
        elif tag == "picaro":
            out["picaro"] = True
        elif tag in v["words"]:
            kind, arg = v["words"][tag]
            out["tags"].append((kind, arg))
        elif tag in ("relleno", "sinacciones", "cuarta_pared", "rebeldia", "timbre", "invasion",
                     "glitch", "glitch_ok", "cartel", "moneda"):
            purpose(tag)
        elif re.fullmatch(r"(pregunta|si|no|mentira|retoma):\w+", tag):
            name, q = tag.split(":")
            purpose(name, lookup("question", q, "pregunta"))
        elif tag.startswith("escena:"):
            parts = tag.split(":")
            if len(parts) != 3 or not parts[2].isdigit() or not 1 <= int(parts[2]) <= 6:
                raise PhraseError("%s: usa escena:NOMBRE:N con N de 1 a 6" % line)
            sid = scenes.setdefault(parts[1], len(scenes) + 1)
            if sid > 255:
                raise PhraseError("%s: demasiadas escenas" % line)
            purpose("escena", sid, int(parts[2]))
        elif tag.startswith("evento:"):
            out["tags"].append((tk["PT_EVENT"], lookup("event", tag[7:], "evento")))
            out["has_event"] = True
        elif tag.startswith("habito:"):
            out["tags"].append((tk["PT_HABIT"], lookup("habit", tag[7:], "hábito")))
        elif tag.startswith("mascota:"):
            out["tags"].append((tk["PT_PET"], lookup("pet", tag[8:], "mascota")))
        elif tag.startswith("resp:"):
            m = re.fullmatch(r"resp:(\w+)=(si|no)", tag)
            if not m:
                raise PhraseError("%s: usa resp:PREGUNTA=si|no" % line)
            q = lookup("question", m.group(1), "pregunta")
            out["tags"].append((tk["PT_RESP"], q | ((1 if m.group(2) == "si" else 2) << 8)))
        elif tag.startswith("ausencia>") or tag.startswith("ausencia<"):
            kind = tk["PT_ABS_GT"] if tag[8] == ">" else tk["PT_ABS_LT"]
            out["tags"].append((kind, parse_duration(tag[9:], line)))
        elif re.fullmatch(r"miradas[<>]\d+", tag):
            kind = tk["PT_LOOKS_GT"] if tag[7] == ">" else tk["PT_LOOKS_LT"]
            out["tags"].append((kind, int(tag[8:])))
        elif re.fullmatch(r"racha>\d+d?", tag):
            out["tags"].append((tk["PT_STREAK_GT"], int(tag[6:].rstrip("d"))))
        elif re.fullmatch(r"coleccion>\d+", tag):
            out["tags"].append((tk["PT_COLL_GT"], int(tag[10:])))
        else:
            raise PhraseError("%s: etiqueta desconocida %r" % (line, tag))
    if out["speaker"] is None:
        raise PhraseError("%s: falta el personaje" % line)
    if len(out["tags"]) > MAX_TAGS:
        raise PhraseError("%s: demasiadas etiquetas" % line)
    if out["has_event"] and not out["kind_set"]:
        out["kind"] = v["kind"]["comentario"]
    return out


def check_text(text, info, v, line):
    if not text:
        raise PhraseError("%s: frase vacía" % line)
    if len(text) > MAX_TEXT:
        raise PhraseError("%s: frase de %d caracteres (máximo %d)" % (line, len(text), MAX_TEXT))
    if "\t" in text or "  " in text:
        raise PhraseError("%s: espacios raros en el texto" % line)
    kinds_present = set()
    for kind, _ in info["tags"]:
        for name, idx in v["tagkind"].items():
            if idx == kind:
                kinds_present.add({"PT_ABS_GT": "abs_gt", "PT_ABS_LT": "abs_lt", "PT_EVENT": "event",
                                   "PT_PET": "pet_kind"}.get(name, name))
    # mascota:X cuenta; sin_mascota (arg 0) no sirve para rellenar {mascota}
    pet_tag = [a for k, a in info["tags"] if k == v["tagkind"]["PT_PET"] and a != 0]
    if not pet_tag:
        kinds_present.discard("pet_kind")
    open_b, close_b = text.count("{"), text.count("}")
    found = re.findall(r"\{([^{}]*)\}", text)
    if open_b != close_b or open_b != len(found):
        raise PhraseError("%s: llaves mal cerradas" % line)
    for h in found:
        if h not in HOLES:
            raise PhraseError("%s: hueco desconocido {%s}" % (line, h))
        need = HOLE_NEEDS.get(h)
        if need and not (need & kinds_present):
            raise PhraseError("%s: {%s} necesita una etiqueta que lo garantice (%s)" %
                              (line, h, " o ".join(sorted(need))))
        if h == "pasos" and not any(k == v["tagkind"]["PT_EVENT"] and a == v["event"]["paseo"]
                                    for k, a in info["tags"]):
            raise PhraseError("%s: {pasos} necesita evento:paseo" % line)


def parse_files(data_dir, v):
    names = sorted(f for f in os.listdir(data_dir) if f.endswith(".txt"))
    if not names:
        raise PhraseError("no hay .txt en %s" % data_dir)
    scenes, phrases, seen = {}, [], set()
    for fname in names:
        stem = fname[:-4]
        with open(os.path.join(data_dir, fname), encoding="utf-8") as fh:
            for n, raw in enumerate(fh, 1):
                raw = raw.rstrip("\n").rstrip()
                if not raw.strip() or raw.lstrip().startswith("#"):
                    continue
                line = "%s:%d" % (fname, n)
                m = re.fullmatch(r"\[([^\]]*)\]\s+(.+)", raw)
                if not m:
                    raise PhraseError("%s: formato no válido (esperaba '[etiquetas]  texto')" % line)
                info = parse_tags(m.group(1), v, line, scenes)
                text = m.group(2).strip()
                check_text(text, info, v, line)
                if stem in v["speaker"] and info["speaker"] != v["speaker"][stem]:
                    raise PhraseError("%s: esta frase no es de %s" % (line, stem))
                key = (info["speaker"], text)
                if key in seen:
                    raise PhraseError("%s: frase repetida" % line)
                seen.add(key)
                phrases.append((info, text, line))
    # Cada escena tiene sus líneas 1..N seguidas y sin huecos.
    by_scene = {}
    for info, _, line in phrases:
        if info["kind"] == v["kind"]["escena"]:
            by_scene.setdefault(info["karg"], []).append(info["kn"])
    names_by_id = {i: n for n, i in scenes.items()}
    for sid, lines in by_scene.items():
        if sorted(lines) != list(range(1, len(lines) + 1)):
            raise PhraseError("escena %s: las líneas deben ser 1..N sin repetir (%s)" %
                              (names_by_id[sid], sorted(lines)))
        if len(lines) < 2:
            raise PhraseError("escena %s: necesita al menos dos líneas" % names_by_id[sid])
    # Las condiciones de una escena solo valen en la línea 1; el tono (picaro) es el mismo en toda ella.
    tone = {}
    for info, _, line in phrases:
        if info["kind"] != v["kind"]["escena"]:
            continue
        if info["kn"] > 1 and info["tags"]:
            raise PhraseError("%s: las condiciones de una escena solo se admiten en la línea 1" % line)
        if tone.setdefault(info["karg"], info["picaro"]) != info["picaro"]:
            raise PhraseError("%s: todas las líneas de una escena deben tener el mismo tono (picaro o no)" % line)
    # Modo apto para la abuela: cada grupo de frases que el motor necesita siempre debe tener
    # una versión blanca (la pregunta y sus reacciones, los límites, el timbre, la rebeldía...).
    groups = {}
    for info, _, line in phrases:
        k = info["kind"]
        if k in (v["kind"][n] for n in ("relleno", "sinacciones", "timbre", "invasion", "rebeldia",
                                        "pregunta", "si", "no", "mentira", "retoma", "glitch",
                                        "glitch_ok", "cartel", "moneda")):
            g = groups.setdefault((k, info["karg"], info["speaker"]), [0, 0, line])
            g[0] += 1
            g[1] += 0 if info["picaro"] else 1
    inv_k = {i: n for n, i in v["kind"].items()}
    for (k, karg, sp), (n, white, line) in groups.items():
        if white == 0:
            raise PhraseError("%s: el grupo %s (pregunta %d) no tiene ninguna frase blanca para el modo apto-abuela"
                              % (line, inv_k[k], karg))
    # Cada pregunta que hace un personaje necesita las reacciones de sí y de no de ese mismo personaje.
    for info, _, line in phrases:
        if info["kind"] == v["kind"]["pregunta"]:
            for r in ("si", "no"):
                if not any(i["kind"] == v["kind"][r] and i["karg"] == info["karg"] and
                           i["speaker"] == info["speaker"] for i, _, _ in phrases):
                    raise PhraseError("%s: falta la reacción '%s' del mismo personaje" % (line, r))
    # Cada pregunta tiene su pregunta y las reacciones de sí y de no.
    for q, qid in v["question"].items():
        have = {k: 0 for k in ("pregunta", "si", "no")}
        for info, _, _ in phrases:
            for k in have:
                if info["kind"] == v["kind"][k] and info["karg"] == qid:
                    have[k] += 1
        if not all(have.values()):
            raise PhraseError("pregunta %s: faltan frases %s" % (q, [k for k, c in have.items() if not c]))
    return phrases, scenes


# ----------------------------------------------------------------- binario

def encode(phrases):
    recs = []
    for info, text, _ in phrases:
        data = text.encode("utf-8")
        if len(data) > 255:
            raise PhraseError("frase demasiado larga en bytes: %r" % text)
        head = struct.pack("<7B", info["speaker"], info["kind"], info["karg"], info["kn"],
                           1 if info["picaro"] else 0, len(info["tags"]), len(data))
        tags = b"".join(struct.pack("<BH", k, a) for k, a in info["tags"])
        recs.append(head + tags + data)
    n = len(recs)
    base = 4 + 2 + 2 + 4 * n
    offs, pos = [], base
    for r in recs:
        offs.append(pos)
        pos += len(r)
    blob = MAGIC + struct.pack("<HH", VERSION, n) + b"".join(struct.pack("<I", o) for o in offs)
    return blob + b"".join(recs)


def render_c(blob, phrases, v):
    inv_sp = {i: n for n, i in v["speaker"].items()}
    counts = {}
    for info, _, _ in phrases:
        counts[inv_sp[info["speaker"]]] = counts.get(inv_sp[info["speaker"]], 0) + 1
    lines = [
        "/* GENERADO por tools/phrases2bin.py a partir de los .txt de data/frases: no editar a mano.",
        " * Para añadir frases, edita los .txt y ejecuta `make phrases`. Formato: ver el script.",
        " * %d frases, %d bytes (%s)." % (len(phrases), len(blob),
                                         ", ".join("%s %d" % kv for kv in sorted(counts.items()))),
        " * Es un array const: en el llavero se lee directamente de la flash (XIP), sin RAM. */",
        '#include "phrases.h"',
        "",
        "const uint8_t PHRASE_BANK[] = {",
    ]
    for i in range(0, len(blob), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in blob[i:i + 16]) + ",")
    lines += ["};", "", "const unsigned PHRASE_BANK_LEN = sizeof(PHRASE_BANK);", ""]
    return "\n".join(lines)


def build(data_dir=DATA_DIR, vocab=VOCAB_H):
    v = read_vocab(vocab)
    phrases, scenes = parse_files(data_dir, v)
    blob = encode(phrases)
    return render_c(blob, phrases, v), phrases, v


def stats(phrases, v):
    inv_sp = {i: n for n, i in v["speaker"].items()}
    inv_k = {i: n for n, i in v["kind"].items()}
    per = {}
    for info, _, _ in phrases:
        d = per.setdefault(inv_sp[info["speaker"]], {"total": 0, "picaro": 0})
        d["total"] += 1
        d["picaro"] += 1 if info["picaro"] else 0
    for name in sorted(per):
        d = per[name]
        print("%-8s %4d frases (%d picaras, %d%%)" % (name, d["total"], d["picaro"],
                                                      100 * d["picaro"] // d["total"]))
    kinds = {}
    for info, _, _ in phrases:
        kinds[inv_k[info["kind"]]] = kinds.get(inv_k[info["kind"]], 0) + 1
    print("por propósito:", ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    tot = len(phrases)
    pic = sum(1 for i, _, _ in phrases if i["picaro"])
    print("total %d frases, %d picaras (%d%%)" % (tot, pic, 100 * pic // tot))


# ------------------------------------------------------------------- pruebas

def selftest():
    import tempfile
    v = read_vocab()
    assert v["speaker"]["abuela"] == 1 and v["kind"]["escena"] == 12
    assert v["words"]["madrugada"] == (v["tagkind"]["PT_TOD"], 0)
    assert v["event"]["manzana_podrida"] == 1 and "paseo" in v["event"]
    scenes = {}
    t = parse_tags("noche, abuela", v, "x:1", scenes)
    assert t["speaker"] == 1 and t["tags"] == [(v["tagkind"]["PT_TOD"], 4)] and not t["picaro"]
    t = parse_tags("ausencia>3d, robot, picaro", v, "x:1", scenes)
    assert t["tags"] == [(v["tagkind"]["PT_ABS_GT"], 4320)] and t["picaro"]
    t = parse_tags("evento:manzana_podrida, abuela", v, "x:1", scenes)
    assert t["kind"] == v["kind"]["comentario"]
    t = parse_tags("resp:plantas=no, escena:existir:2, robot", v, "x:1", scenes)
    assert t["kind"] == v["kind"]["escena"] and t["kn"] == 2 and scenes == {"existir": 1}
    for bad in ("noche", "noche, abuela, robot", "nochee, abuela", "evento:nada, abuela",
                "escena:x:9, abuela", "relleno, rebeldia, abuela", "ausencia>3x, abuela",
                "resp:plantas=quizas, abuela", "pregunta:nada, abuela"):
        try:
            parse_tags(bad, v, "x:1", {})
        except PhraseError:
            continue
        raise AssertionError("debería rechazar: " + bad)
    info = parse_tags("abuela", v, "x:1", {})
    for bad_text in ("hola {nada}", "hola {evento", "{ausencia} sin ti", "x" * 200, "dos  espacios", "{pasos}"):
        try:
            check_text(bad_text, info, v, "x:1")
        except PhraseError:
            continue
        raise AssertionError("debería rechazar el texto: " + bad_text)
    info = parse_tags("ausencia>3d, robot", v, "x:1", {})
    check_text("{ausencia} sin verme.", info, v, "x:1")
    # Ida y vuelta con un banco mínimo: la cabecera y los offsets cuadran.
    phrases = [(parse_tags("noche, abuela", v, "a:1", {}), "Hola, ñandú.", "a:1"),
               (parse_tags("robot, picaro", v, "a:2", {}), "Beep.", "a:2")]
    blob = encode(phrases)
    assert blob[:4] == MAGIC
    ver, n = struct.unpack_from("<HH", blob, 4)
    assert (ver, n) == (VERSION, 2)
    off1 = struct.unpack_from("<I", blob, 8 + 4)[0]
    sp, kind, karg, kn, flags, ntags, ln = struct.unpack_from("<7B", blob, off1)
    assert sp == 2 and flags == 1 and ntags == 0 and blob[off1 + 7:off1 + 7 + ln] == b"Beep."
    # El banco real compila y es determinista.
    a, _, _ = build()
    b, _, _ = build()
    assert a == b
    # Un directorio con una línea mala da un error con archivo y línea.
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "abuela.txt"), "w", encoding="utf-8") as f:
            f.write("[abuela] bien\n[robot] no es de la abuela\n")
        try:
            parse_files(d, v)
        except PhraseError as e:
            assert "abuela.txt:2" in str(e)
        else:
            raise AssertionError("debería detectar el personaje equivocado")
    print("phrases2bin: pruebas OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DATA_DIR)
    ap.add_argument("--vocab", default=VOCAB_H)
    ap.add_argument("--out", default=OUT_C)
    ap.add_argument("--check", action="store_true", help="falla si --out no coincide con lo que saldría ahora")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    try:
        if args.selftest:
            selftest()
            return 0
        text, phrases, v = build(args.data, args.vocab)
        if args.stats:
            stats(phrases, v)
            return 0
        if args.check:
            cur = open(args.out, encoding="utf-8").read() if os.path.exists(args.out) else ""
            if cur != text:
                print("%s no está al día: ejecuta `make phrases`" % args.out, file=sys.stderr)
                return 1
            return 0
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("%s: %d frases" % (args.out, len(phrases)))
        return 0
    except PhraseError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
