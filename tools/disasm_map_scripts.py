#!/usr/bin/env python3
"""disasm_map_scripts.py — bytecode disassembler for Doom II RPG location scripts.

Decodes the tileEvents/mapByteCode VM embedded in mapXX.bin files into an
annotated, deterministic listing. Intended as the persistent replacement for
the ad-hoc per-session decoders (see docs/research/2026-08-25-unhandled-script-events.md,
docs/original-code/tile-events-vm.md).

Method (verified):
  * Section discovery walks the CAFEBABE marker chain backwards from the file
    tail, constrained by the header counts numTileEvents / mapByteCodeSize
    (three independent size checks: staticFuncs=24 B, tileEvents=N*8 B,
    bytecode=mapByteCodeSize B). Falls back to a full forward parse mirroring
    src/LoadingManager.cpp:463-553 (with the file-side layout: nodeChildOffset1/2
    back-to-back before a single marker).
  * Code discovery is CFG-based from entry IPs — every tileEvent entry point,
    every defined staticFunc slot, plus optional --funcs extras. Never a linear
    scan (desync-prone).
  * Operand shapes mirror new_src/domain/game/ScriptVM.cpp consumption (the
    executable truth of the rewrite) and src/ScriptThread.cpp for opcodes the
    rewrite does not implement yet. All inline operands are BIG-endian; jump /
    call offsets are relative to the address AFTER the operands (the trailing
    ++IP of the dispatch loop).
  * Unknown opcode => `.byte` line + WARNING; that CFG path stops (no silent desync).

Usage:
  python3 tools/disasm_map_scripts.py <path/to/mapXX.bin> [-o out.txt]
                                      [--funcs ip1,ip2] [--ipa <Doom 2 RPG.ipa>]
                                      [--verify]

  --verify runs the built-in map00 anchor suite (docs/original-code/
  tile-events-vm.md section 5, docs/research/2026-08-25 section 2-3) and exits
  nonzero if any anchor fails. Intended to be run against tmp_map00.bin.

Stdlib only. Output is deterministic (sorted, no timestamps).
"""

import argparse
import re
import struct
import sys
from collections import deque

MARKER = b"\xBE\xBA\xFE\xCA"  # writeMarker(0xCAFEBABE) little-endian on disk (src/Resource.cpp:92-97)

SCR_NAMES = [
    "SCR_INIT_MAP", "SCR_END_GAME", "SCR_BOSS_75", "SCR_BOSS_50",
    "SCR_BOSS_25", "SCR_BOSS_DEAD", "SCR_PER_TURN", "SCR_ATTACK_NPC",
    "SCR_MONSTER_DEATH", "SCR_MONSTER_ACTIVATE", "SCR_CHICKEN_KICKED",
    "SCR_ITEM_PICKUP",
]
SCR_NOT_DEFINED = 65535

OP_NAMES = {
    0: "EVAL", 1: "JUMP", 2: "RETURN", 3: "MESSAGE", 4: "LERPSPRITE",
    5: "STARTCINEMATIC", 6: "SETSTATE", 7: "CALL_FUNC", 8: "ITEM_COUNT",
    9: "TILE_EMPTY", 10: "WEAPON_EQUIPPED", 11: "CHANGE_MAP", 12: "CAMERA_STR",
    13: "DIALOG", 14: "WAIT", 15: "GOTO", 16: "ABORT_MOVE", 17: "ENTITY_FRAME",
    18: "ADV_CAMERAKEY", 19: "DAMAGEMONSTER", 20: "DAMAGEPLAYER", 21: "DOOROP",
    22: "MONSTERFLAGOP", 23: "EVENTOP", 24: "HIDE", 25: "DROPITEM",
    26: "PREVSTATE", 27: "NEXTSTATE", 28: "WAKEMONSTER", 29: "SHOW_PLAYERATTACK",
    30: "MONSTER_PARTICLES", 31: "SPAWN_PARTICLES", 32: "FADEOP",
    33: "GIVEITEM", 34: "NAMEENTITY", 35: "DROPMONSTERITEM", 36: "SETDEATHFUNC",
    37: "PLAYSOUND", 38: "NPCCHAT", 39: "STOCKSTATION", 40: "LERPFLAT",
    41: "GIVELOOT", 42: "MARKTILE", 43: "UPDATEJOURNAL", 44: "BRIBE_ENTITY",
    45: "PLAYER_ADD_STAT", 46: "PLAYER_ADD_RECIPE", 47: "RESPAWN_MONSTER",
    48: "SCREEN_SHAKE", 49: "SPEECHBUBBLE", 50: "AWARDSECRET", 51: "AIGOAL",
    52: "ADVANCETURN", 53: "MINIGAME", 54: "ENDMINIGAME", 55: "ENDROUND",
    56: "PLAYERATTACK", 57: "SET_FOG_COLOR", 58: "LERP_FOG",
    59: "LERPSPRITEOFFSET", 60: "DISABLED_WEAPONS", 61: "LERPSCALE",
    62: "GIVEAWARD", 65: "STARTMIXING", 66: "DEBUGPRINT", 67: "GOTO_MENU",
    68: "START_INTERCINEMATIC", 69: "TURN_PLAYER", 70: "STATUS_EFFECT",
    71: "JOURNAL_TILE", 72: "MAKE_CORPSE", 73: "INVENTORY_OP", 74: "END_GAME",
    75: "LERPSPRITEPARABOLA", 76: "TOGGLE_OVERLAY", 77: "FOG_AFFECTS_SKYMAP",
    78: "ENABLE_HELP", 79: "SET_MM_RENDER_HACK", 80: "START_ARMORREPAIR",
    81: "FORCE_BOT_RETURN", 82: "UNMARKTILE", 83: "ASSIGN_LOOTSET",
    84: "START_TARGETPRACTICE", 85: "GIVE_AUTOMAP", 86: "ANGER_VIOS",
    87: "UNHIDE_AUTOMAP", 88: "HIDE_AUTOMAP", 89: "PORTAL_EVENT",
    90: "PITCH_CONTROL", 91: "USED_CHAINSAW", 92: "ENTITY_BREATHES",
    93: "DESTROY_PLAYER", 94: "START_TREADMILL", 95: "STOPSOUND",
    96: "LERPSPRITEPARABOLA_SCALE", 97: "SET_CALDEX_RENDER_HACK", 255: "END",
}

EVAL_OPS = {0: "&&", 1: "||", 2: "<=", 3: "<", 4: "==", 5: "!=", 6: "!"}
DIR_NAMES = {16: "E", 32: "NE", 64: "N", 128: "NW", 256: "W", 512: "SW",
             1024: "S", 2048: "SE"}
FACE_NAMES = ["E", "NE", "N", "NW", "W", "SW", "S", "SE"]
DOOR_ACT = {0: "OPEN", 1: "UNLOCK+OPEN", 2: "LOCK", 3: "UNLOCK"}
LS_FLAGS = [(1, "ASYNC"), (2, "BLOCK"), (4, "NO_TIME"), (8, "DEFAULT_Z")]


def exec_type_names(w1):
    names = []
    for bit, name in ((1, "ENTER"), (2, "EXIT"), (4, "TRIGGER"), (8, "FACE")):
        if w1 & bit:
            names.append(name)
    return names


def trigger_desc(w1):
    parts = exec_type_names(w1)
    dirs = [DIR_NAMES[b] for b in sorted(DIR_NAMES) if w1 & b]
    if dirs:
        parts.append("dir:" + "|".join(dirs))
    atk = [n for b, n in ((4096, "MELEE"), (8192, "RANGED"), (16384, "EXPLOSION")) if w1 & b]
    if atk:
        parts.append("atk:" + "|".join(atk))
    if w1 & 0x10000:
        parts.append("BLOCKINPUT")
    if w1 & 0x20000:
        parts.append("EXIT_GOTO")
    if w1 & 0x40000:
        parts.append("SKIP_TURN")
    if w1 & 0x80000:
        parts.append("DISABLED")
    return " ".join(parts)


def ls_flag_names(flags):
    return "|".join(name for bit, name in LS_FLAGS if flags & bit) or "0"


class Truncated(Exception):
    """Operand read past end of bytecode blob."""


class Reader:
    """Big-endian inline operand reader (src/ScriptThread.cpp:2095-2119);
    positions are absolute bytecode offsets, operands start at ip+1."""

    def __init__(self, bc, ip):
        self.bc = bc
        self.p = ip + 1

    def _need(self, n):
        if self.p + n > len(self.bc):
            raise Truncated()

    def u8(self):
        self._need(1)
        v = self.bc[self.p]
        self.p += 1
        return v

    def s8(self):
        v = self.u8()
        return v - 0x100 if v & 0x80 else v

    def u16(self):
        self._need(2)
        v = (self.bc[self.p] << 8) | self.bc[self.p + 1]
        self.p += 2
        return v

    def s16(self):
        v = self.u16()
        return v - 0x10000 if v & 0x8000 else v

    def i32(self):
        self._need(4)
        v = struct.unpack_from(">i", self.bc, self.p)[0]
        self.p += 4
        return v


class Insn:
    __slots__ = ("ip", "op", "name", "size", "raw", "text", "succ", "refs",
                 "fields", "unknown", "note")

    def __init__(self, ip, op, name, size, raw, text, succ, refs, fields,
                 unknown=False, note=None):
        self.ip = ip
        self.op = op
        self.name = name
        self.size = size
        self.raw = raw
        self.text = text          # operand annotation, no targets
        self.succ = succ          # list of successor IPs (code flow)
        self.refs = refs          # [(kind, key)] for xref summary
        self.fields = fields      # decoded named fields (for --verify)
        self.unknown = unknown
        self.note = note


def decode_eval_expr(r):
    """EV_EVAL term grammar: count, terms..., false-offset.
    Term: bit7 -> var[t&0x7F]; bit6 -> 14-bit signed const; else operator."""
    def atomic(s):
        return re.search(r"[() ]", s) is None

    count = r.u8()
    stack = []
    for _ in range(count):
        t = r.u8()
        if t & 0x80:
            stack.append("v%d" % (t & 0x7F))
        elif t & 0x40:
            nxt = r.u8()
            val = (((t & 0x3F) << 8) | nxt) << 18 >> 18
            stack.append(str(val))
        elif t in EVAL_OPS:
            if t == 6:  # NOT
                if not stack:
                    stack.append("!")
                else:
                    top = stack.pop()
                    stack.append(top if not atomic(top) else
                                 "!" + top)
            else:
                a = stack.pop() if stack else "?"
                b = stack.pop() if stack else "?"
                body = "%s %s %s" % (b, EVAL_OPS[t], a)
                stack.append(body if atomic(b) and atomic(a) else "(%s)" % body)
        else:
            stack.append("?op%d" % t)
    expr = stack[-1] if stack else "true"
    off = r.u8()
    return expr, off


def fmt_loot_entry(e):
    cls = (e >> 12) & 0xF
    idx = (e >> 6) & 0x3F
    cnt = e & 0x3F
    if cls == 6:
        return "text str=%d" % (e & 0xFFF)
    if cls == 5:
        return "quest=%d" % (e & 0xFFF)
    return "cls=%d idx=%d cnt=%d" % (cls, idx, cnt)


# --- opcode decoders -------------------------------------------------------
# Each returns (fields, text, refs, note). Successors are derived generically
# from the opcode class (jump/cond/call/fallthrough) after the handler ran;
# handlers may append explicit extra successors via fields["extra_succ"].

def h_eval(r):
    expr, off = decode_eval_expr(r)
    return {"false_off": off}, "[ %s ]" % expr, [], None


def h_jump(r):
    return {"offset": r.u16()}, "", [], None


def h_message(r):
    v = r.u16()
    sid = v & 0x7FFF
    return {"str": sid, "style3": bool(v & 0x8000)}, \
        "str=%d%s" % (sid, " style=important" if v & 0x8000 else ""), \
        [("string", sid)], None


def h_lerpsprite(r):
    p = r.u8() | r.u8() << 8 | r.u8() << 16
    f = {}
    sprite = (p >> 14) & 0xFF
    dx, dy = (p >> 9) & 0x1F, (p >> 4) & 0x1F
    fl = p & 0xF
    txt = "sprite=%d dst=(%d,%d) flags=%s" % (sprite, dx, dy, ls_flag_names(fl))
    if not fl & 8:
        z = r.u8() - 48
        txt += " z=%d" % z
        f["z"] = z
    ms = 0
    if not fl & 4:
        ms = r.u8() * 100
        txt += " t=%dms" % ms
    f.update(sprite=sprite, dx=dx, dy=dy, flags=fl, ms=ms)
    return f, txt, [("sprite", sprite)], None


def h_startcinematic(r):
    cam = r.u8()
    return {"camera": cam}, "camera=%d" % cam, [("camera", cam)], None


def h_setstate(r):
    idx, val = r.u8(), r.s16()
    return {"var": idx, "value": val}, "v%d = %d" % (idx, val), [], None


def h_call_func(r):
    return {"target": r.u16()}, "", [], None


def h_item_count(r):
    packed, dst = r.u16(), r.u8()
    cls, idx = packed & 0x1F, (packed >> 5) & 0x1F
    cname = ("inv", "wpn", "ammo")[cls] if cls < 3 else "cls%d" % cls
    return {"cls": cls, "idx": idx, "dst": dst}, \
        "%s[%d] -> v%d" % (cname, idx, dst), [], None


def h_tile_empty(r):
    packed, dst = r.u16(), r.u8()
    tx, ty = packed & 0x1F, (packed >> 5) & 0x1F
    return {"tx": tx, "ty": ty, "dst": dst}, \
        "tile(%d,%d) empty -> v%d" % (tx, ty, dst), [("tile", (tx, ty))], None


def h_weapon_equipped(r):
    d = r.u8() & 0x7F
    return {"dst": d}, "ce->weapon -> v%d" % d, [], None


def h_change_map(r):
    b, s = r.u8(), r.u16()
    txt = "map=%d dir=%d tile=%d%s" % (b & 0xF, (b >> 4) & 7, s & 0x3FF,
                                       " fade+stats" if b & 0x80 else "")
    return {"map": b & 0xF, "dir": (b >> 4) & 7, "tile": s & 0x3FF}, txt, \
        [("map", b & 0xF)], None


def h_camera_str(r):
    v, ms = r.u16(), r.u16()
    sid = v & 0x3FFF
    kind = "title" if v & 0x8000 else "subtitle"
    return {"str": sid, "title": bool(v & 0x8000), "ms": ms}, \
        "%s str=%d %dms" % (kind, sid, ms), [("string", sid)], None


def h_dialog(r):
    sid, packed = r.u8(), r.u8()
    style, typ = packed & 0xF, packed >> 4
    return {"str": sid, "style": style, "type": typ}, \
        "str=%d style=%d type=%d" % (sid, style, typ), [("string", sid)], None


def h_wait(r):
    ms = r.u8() * 100
    return {"ms": ms}, "%dms" % ms, [], None


def h_goto(r):
    v = r.u16()
    dx, dy = (v >> 5) & 0x1F, v & 0x1F
    face = (v >> 10) & 0xF
    fname = FACE_NAMES[face] if face < 8 else ("keep" if face == 15 else "?%d" % face)
    txt = "walk to (%d,%d) face=%s%s%s" % (
        dx, dy, fname, " animate" if v & 0x4000 else "",
        " advanceTurn" if v & 0x8000 else "")
    return {"dx": dx, "dy": dy, "face": face}, txt, [("tile", (dx, dy))], None


def h_entity_frame(r):
    sp, fr, t = r.u8(), r.u8(), r.u8() * 100
    return {"sprite": sp, "frame": fr, "ms": t}, \
        "sprite=%d frame=%d%s" % (sp, fr, " wait=%dms" % t if t else ""), \
        [("sprite", sp)], None


def h_adv_camerakey(r):
    n = r.u8()
    return {"count": n}, "keys=%d" % n, [], None


def h_damagemonster(r):
    sp, dmg = r.u8(), r.s8()
    return {"sprite": sp, "dmg": dmg}, "sprite=%d dmg=%d" % (sp, dmg), \
        [("sprite", sp)], None


def h_damageplayer(r):
    dmg, arm, dr = r.s8(), r.s8(), r.s8()
    return {"dmg": dmg, "armor": arm, "dir": dr}, \
        "hp=%d armor=%d dir=%d" % (dmg, arm, dr), [], None


def h_doorop(r):
    v = r.u16()
    sp, op = v & 0x3FF, v >> 10
    act = op & 3
    quiet = bool(op & 4)
    txt = "sprite=%d %s%s" % (sp, DOOR_ACT.get(act, "?%d" % act),
                              " (quiet)" if quiet else "")
    return {"sprite": sp, "act": act, "quiet": quiet, "op": op}, txt, \
        [("sprite", sp), ("door", (sp, DOOR_ACT.get(act, "?%d" % act)))], None


def h_monsterflagop(r):
    sp, b = r.u8(), r.u8()
    opname = ("ADD", "REMOVE", "SET")[(b >> 6) & 3]
    mask = 1 << (b & 0x3F)
    return {"sprite": sp, "op": opname, "mask": mask}, \
        "sprite=%d %s mask=0x%X" % (sp, opname, mask), [("sprite", sp)], None


def h_eventop(r):
    v = r.u16()
    idx, dis = v & 0x7FFF, bool(v & 0x8000)
    return {"event": idx, "disable": dis}, \
        "%s event[%d]" % ("disable" if dis else "enable", idx), \
        [("event", (idx, "disable" if dis else "enable"))], None


def h_hide(r):
    sp = r.u8()
    return {"sprite": sp}, "sprite=%d" % sp, [("sprite", sp)], None


def h_dropitem(r):
    v, d = r.u16(), r.u8()
    txt = "at (%d,%d) h=%d def=%d" % (v & 0x1F, (v >> 5) & 0x1F,
                                      (v >> 10) & 0x1F, d)
    return {"def": d}, txt, [("item", d), ("tile", (v & 0x1F, (v >> 5) & 0x1F))], None


def h_prevstate(r):
    idx = r.u8()
    return {"var": idx}, "--v%d" % idx, [], None


def h_nextstate(r):
    idx = r.u8()
    return {"var": idx}, "++v%d" % idx, [], None


def h_wakemonster(r):
    sp = r.u8()
    return {"sprite": sp}, "sprite=%d" % sp, [("sprite", sp)], None


def h_show_playerattack(r):
    w = r.u8()
    return {"weapon": w}, "weapon=%d" % w, [], None


def h_monster_particles(r):
    sp = r.u8()
    return {"sprite": sp}, "sprite=%d blood" % sp, [("sprite", sp)], None


def h_spawn_particles(r):
    packed, pos, dz = r.u8(), r.u16(), r.u8() - 48
    kind, color = (packed >> 3) & 0xF, packed & 7
    if packed & 0x80:
        px, py = (pos >> 11) & 0x1F, (pos >> 6) & 0x1F
        where = "tile(%d,%d)" % (px, py)
    else:
        where = "sprite=%d" % pos
    return {"kind": kind, "color": color}, \
        "kind=%d color=%d %s z=%+d" % (kind, color, where, dz), \
        ([] if packed & 0x80 else [("sprite", pos)]), None


def h_fadeop(r):
    v = r.u16()
    return {"out": bool(v & 0x8000), "ms": v & 0x7FFF}, \
        "%s %dms" % ("out" if v & 0x8000 else "in", v & 0x7FFF), [], None


def h_giveitem(r):
    d, q, mode = r.u8(), r.s8(), r.s8()
    return {"def": d, "qty": q, "mode": mode}, \
        "def=%d qty=%d mode=%d" % (d, q, mode), [("item", d)], None


def h_nameentity(r):
    sp, name = r.u8(), r.u8()
    return {"sprite": sp, "name": name}, "sprite=%d name=%d" % (sp, name), \
        [("sprite", sp)], None


def h_dropmonsteritem(r):
    v = r.u16()
    d = r.u8()
    relocate = bool(v & 0x8000)
    if relocate:
        d = (d << 8) | r.u8()
        v &= 0x7FFF
    qty = r.u8()
    txt = "%s sprite=%d def=%d qty=%d" % (
        "relocate-entity" if relocate else "drop", v, d, qty)
    return {"sprite": v, "def": d, "qty": qty, "relocate": relocate}, txt, \
        ([("sprite", d)] if relocate else [("item", d), ("sprite", v)]), None


def h_setdeathfunc(r):
    sp, ip = r.u8(), r.s16()
    return {"sprite": sp, "func": ip}, \
        "sprite=%d deathfunc=%s" % (sp, "none" if ip == -1 else "ip %d" % ip), \
        [("sprite", sp)], None


def h_playsound(r):
    sid, args = r.u8(), r.u8()
    rid = sid + 1000
    return {"id": rid}, "id=%d vol=%d pri=%d" % (rid, args >> 4, args & 0xF), \
        [("sound", rid)], None


def h_npcchat(r):
    v = r.u16()
    sp, param = v & 0x3FFF, (v >> 14) & 3
    return {"sprite": sp, "param": param}, \
        "sprite=%d chat=%d" % (sp, param), [("sprite", sp)], None


def h_lerpflat(r):
    b, s = r.u8(), r.u16()
    return {"b": b, "s": s}, "b=%d s=%d (ignored by VM)" % (b, s), [], None


def h_giveloot(r):
    cnt = r.u8()
    entries = [r.u16() for _ in range(cnt)]
    rendered = ", ".join(fmt_loot_entry(e) for e in entries)
    refs = [("string", e & 0xFFF) for e in entries if (e >> 12) & 0xF == 6]
    return {"count": cnt, "entries": entries}, "[%s]" % rendered, refs, None


def h_marktile(r):
    v = r.u16()
    fl = (v >> 10) & 0x3F
    tx, ty = (v >> 5) & 0x1F, v & 0x1F
    return {"tx": tx, "ty": ty, "flags": fl}, \
        "tile(%d,%d) flags=0x%X" % (tx, ty, fl), [("tile", (tx, ty))], None


def h_updatejournal(r):
    q, st = r.u8(), r.u8()
    return {"quest": q, "state": st}, "quest=%d state=%d" % (q, st), \
        [("quest", (q, st))], None


def h_player_add_stat(r):
    b = r.u8()
    delta = ((b & 0x1F) << 27) >> 27
    return {"stat": (b >> 5) & 7, "delta": delta}, \
        "stat%d %+d" % ((b >> 5) & 7, delta), [], None


def h_respawn_monster(r):
    sp, tx, ty = r.u16(), r.u8(), r.u8()
    return {"sprite": sp, "tx": tx, "ty": ty}, \
        "sprite=%d at (%d,%d)" % (sp, tx, ty), [("sprite", sp)], None


def h_screen_shake(r):
    v = r.u16()
    dur = ((v >> 14) & 3) + 1
    dx = ((v >> 7) & 0x7F)
    dy = (v & 0x7F)
    return {"dur_units": dur, "raw_dx": dx, "raw_dy": dy}, \
        "units=%d dx=%d dy=%d" % (dur, dx, dy), [], None


def h_speechbubble(r):
    tex, col = r.s16(), r.u8()
    return {"tex": tex, "color": col}, "tex=%d color=%d" % (tex, col), [], None


def h_aigoal(r):
    v, param = r.u16(), r.u8()
    sp, goal = v & 0xFFF, (v >> 12) & 0xF
    return {"sprite": sp, "goal": goal, "param": param}, \
        "sprite=%d goal=%d param=%d" % (sp, goal, param), [("sprite", sp)], None


def h_minigame(r):
    a, b, c, d = (r.s8() for _ in range(4))
    return {"type": a, "b": b, "c": c, "d": d}, \
        "type=%d args=(%d,%d,%d)" % (a, b, c, d), [], None


def h_playerattack(r):
    v = r.u16()
    sp, weapon = v & 0xFFF, (v >> 12) & 0xF
    return {"sprite": sp, "weapon": weapon}, \
        "weapon=%d -> sprite=%d" % (weapon, sp), [("sprite", sp)], None


def h_set_fog_color(r):
    argb = r.i32() & 0xFFFFFFFF
    return {"argb": argb}, "ARGB=%08X" % argb, [], None


def h_lerp_fog(r):
    v = r.i32() & 0xFFFFFFFF
    frm, to, ms = v & 0x7FF, (v >> 11) & 0x7FF, ((v >> 22) & 0xFF) * 100
    return {"from": frm, "to": to, "ms": ms}, \
        "%d -> %d %dms (packed %08X)" % (frm, to, ms, v), [], None


def h_lerpspriteoffset(r):
    sp, ms, v = r.u8(), r.u8() * 100, r.i32()
    dx, dy = (v >> 11) & 0x7FF, v & 0x7FF
    fl = (v >> 22) & 3
    dz = ((v >> 24) & 0xFF) - 48
    return {"sprite": sp, "dx": dx, "dy": dy, "flags": fl, "dz": dz, "ms": ms}, \
        "sprite=%d pixel=(%d,%d) z=%+d t=%dms flags=%d" % (sp, dx, dy, dz, ms, fl), \
        [("sprite", sp)], None


def h_disabled_weapons(r):
    m = r.s16()
    return {"mask": m}, "mask=%d" % m, [], None


def h_lerpscale(r):
    s1, ms, sb = r.u16(), r.u16(), r.u8()
    sp, fl = s1 >> 4, s1 & 0xF
    return {"sprite": sp, "flags": fl, "ms": ms, "scale": sb << 1}, \
        "sprite=%d scale=%d t=%dms flags=%s" % (sp, sb << 1, ms, ls_flag_names(fl)), \
        [("sprite", sp)], None


def h_debugprint(r):
    mode = r.u8()
    if mode == 0:
        chars = []
        while True:
            c = r.u8()
            if c == 0:
                break
            chars.append(chr(c))
        return {"mode": 0, "text": "".join(chars)}, 'mode0 "%s"' % "".join(chars), [], None
    if mode == 1:
        idx = r.u8()
        return {"mode": 1, "var": idx}, "mode1 v%d" % idx, [], None
    return {"mode": mode}, "mode=%d ?" % mode, [], "DEBUGPRINT unknown mode"


def h_goto_menu(r):
    m = r.u8()
    return {"menu": m}, "menu=%d" % m, [], None


def h_start_intercinematic(r):
    cam = r.u8() & 0x7F
    return {"camera": cam}, "camera=%d" % cam, [("camera", cam)], None


def h_turn_player(r):
    b = r.u8()
    face, anim = b & 7, (b >> 3) == 1
    fname = FACE_NAMES[face] if face < 8 else "?%d" % face
    return {"face": face, "animated": anim}, \
        "face=%s (%d deg)%s" % (fname, face * 45, " animated" if anim else ""), [], None


def h_status_effect(r):
    b = r.u8()
    if b & 0x80:
        return {"remove": b & 0x7F}, "remove effect %d" % (b & 0x7F), [], None
    mag = r.u8()
    return {"add": b, "mag": mag}, "add effect %d mag=%d" % (b, mag), [], None


def h_journal_tile(r):
    q, tx, ty = r.u8(), r.u8() & 0x1F, r.u8() & 0x1F
    return {"quest": q, "tx": tx, "ty": ty}, \
        "quest=%d tile(%d,%d)" % (q, tx, ty), [("tile", (tx, ty))], None


def h_make_corpse(r):
    sp, tx, ty = r.u16() & 0xFFF, r.u8(), r.u8()
    return {"sprite": sp, "tx": tx, "ty": ty}, \
        "sprite=%d dst=(%d,%d)" % (sp, tx, ty), [("sprite", sp)], None


def h_inventory_op(r):
    op = r.u8()
    names = {0: "strip-for-vios", 1: "strip-target-practice", 2: "restore"}
    return {"op": op}, names.get(op, "op=%d ?" % op), [], None


def h_lerpspriteparabola(r):
    v, ms = r.i32(), r.u16()
    sp = (v >> 22) & 0x3FF
    dx, dy = (v >> 17) & 0x1F, (v >> 12) & 0x1F
    h = ((v >> 4) & 0xFF) - 48
    fl = v & 0xF
    fields = {"sprite": sp, "dx": dx, "dy": dy, "h": h, "flags": fl, "ms": ms}
    txt = "sprite=%d dst=(%d,%d) h=%+d t=%dms flags=%s" % (
        sp, dx, dy, h, ms, ls_flag_names(fl))
    if r.op == 96:
        sb = r.u8()
        fields["scale"] = sb << 1
        txt += " scale=%d" % (sb << 1)
    return fields, txt, [("sprite", sp)], None


def h_fog_affects_skymap(r):
    b = r.s8()
    return {"b": b}, "b=%d" % b, [], None


def h_unmarktile(r):
    f, txt, refs, _ = h_marktile(r)
    return f, txt, refs, None


def h_assign_lootset(r):
    sp, cnt = r.u16() & 0xFFF, r.u8()
    entries = [r.u16() for _ in range(cnt)]
    return {"sprite": sp, "entries": entries}, \
        "sprite=%d [%s]" % (sp, ", ".join(fmt_loot_entry(e) for e in entries)), \
        [("sprite", sp)], None


def h_start_targetpractice(r):
    v = r.u16()
    return {"a": (v >> 8) & 0x1F, "b": (v >> 3) & 0x1F, "c": v & 7}, \
        "args=(%d,%d,%d)" % ((v >> 8) & 0x1F, (v >> 3) & 0x1F, v & 7), [], None


def h_anger_vios(r):
    b = r.s8()
    return {"b": b}, "b=%d" % b, [], None


def h_rect_automap(r):
    x0, y0, x1, y1 = r.u8(), r.u8(), r.u8(), r.u8()
    return {"rect": (x0, y0, x1, y1)}, "rect=(%d,%d)-(%d,%d)" % (x0, y0, x1, y1), [], None


def h_portal_event(r):
    b = r.u8()
    return {"state": b & 0xF, "prev": (b >> 4) & 0xF}, \
        "state=%d prev=%d" % (b & 0xF, (b >> 4) & 0xF), [], None


def h_pitch_control(r):
    v = r.u16()
    op = "remove" if (v >> 13) & 1 else "add"
    tx, ty, lvl = (v >> 8) & 0x1F, (v >> 3) & 0x1F, v & 7
    return {"op": op, "tx": tx, "ty": ty, "level": lvl}, \
        "%s tile(%d,%d) level=%d" % (op, tx, ty, lvl), [("tile", (tx, ty))], None


def h_entity_breathes(r):
    sp, mode = r.u8(), r.u8()
    return {"sprite": sp, "mode": mode}, \
        "sprite=%d breathe=%d" % (sp, mode), [("sprite", sp)], None


def h_destroy_player(r):
    b = r.u8()
    return {"b": b}, "b=%d" % b, [], None


def h_stopsound(r):
    rid = r.u8() + 1000
    return {"id": rid}, "id=%d" % rid, [("sound", rid)], None


class ParabolaHelper(Reader):
    """Reader carrying the current opcode so shared handlers can branch."""
    def __init__(self, bc, ip, op):
        super().__init__(bc, ip)
        self.op = op


HANDLERS = {
    0: h_eval, 1: h_jump, 3: h_message, 4: h_lerpsprite, 5: h_startcinematic,
    6: h_setstate, 7: h_call_func, 8: h_item_count, 9: h_tile_empty,
    10: h_weapon_equipped, 11: h_change_map, 12: h_camera_str, 13: h_dialog,
    14: h_wait, 15: h_goto, 17: h_entity_frame, 18: h_adv_camerakey,
    19: h_damagemonster, 20: h_damageplayer, 21: h_doorop, 22: h_monsterflagop,
    23: h_eventop, 24: h_hide, 25: h_dropitem, 26: h_prevstate,
    27: h_nextstate, 28: h_wakemonster, 29: h_show_playerattack,
    30: h_monster_particles, 31: h_spawn_particles, 32: h_fadeop,
    33: h_giveitem, 34: h_nameentity, 35: h_dropmonsteritem,
    36: h_setdeathfunc, 37: h_playsound, 38: h_npcchat, 40: h_lerpflat,
    41: h_giveloot, 42: h_marktile, 43: h_updatejournal,
    45: h_player_add_stat, 47: h_respawn_monster, 48: h_screen_shake,
    49: h_speechbubble, 51: h_aigoal, 53: h_minigame, 56: h_playerattack,
    57: h_set_fog_color, 58: h_lerp_fog, 59: h_lerpspriteoffset,
    60: h_disabled_weapons, 61: h_lerpscale, 66: h_debugprint,
    67: h_goto_menu, 68: h_start_intercinematic, 69: h_turn_player,
    70: h_status_effect, 71: h_journal_tile, 72: h_make_corpse,
    73: h_inventory_op, 75: h_lerpspriteparabola, 77: h_fog_affects_skymap,
    78: h_fog_affects_skymap, 79: h_fog_affects_skymap, 97: h_fog_affects_skymap,
    82: h_unmarktile, 83: h_assign_lootset, 84: h_start_targetpractice,
    86: h_anger_vios, 87: h_rect_automap, 88: h_rect_automap,
    89: h_portal_event, 90: h_pitch_control, 92: h_entity_breathes,
    93: h_destroy_player, 95: h_stopsound, 96: h_lerpspriteparabola,
}


class Disassembler:
    def __init__(self, sections, extra_funcs):
        self.sec = sections
        self.warnings = []
        self.insns = {}          # ip -> Insn (shared across all entries)
        self.owner = {}          # ip -> seed label that first reached it
        self.seeds = []          # [(label, ip)]
        self.extra_ips = set(extra_funcs)

    def build_seeds(self):
        sec = self.sec
        for i, v in enumerate(sec.static_funcs):
            if v != SCR_NOT_DEFINED:
                if v < len(sec.bytecode):
                    self.seeds.append(("static %s" % SCR_NAMES[i], v))
                else:
                    self.warnings.append(
                        "staticFuncs[%d]=%d out of range; ignored" % (i, v))
        for ip in sorted(self.extra_ips):
            if ip < len(sec.bytecode):
                self.seeds.append(("user", ip))
            else:
                self.warnings.append(
                    "--funcs IP %d out of range; ignored" % ip)
        for i, (w0, _w1) in enumerate(sec.tile_events):
            ip = (w0 >> 16) & 0xFFFF
            if ip < len(sec.bytecode):
                self.seeds.append(("event[%d]" % i, ip))
            else:
                self.warnings.append(
                    "event[%d] entry IP %d out of range" % (i, ip))

    def _decode_one(self, ip):
        bc = self.sec.bytecode
        op = bc[ip]
        if op not in OP_NAMES:
            self.warnings.append(
                "UNKNOWN opcode %d at IP %d — emitted .byte, path stopped "
                "(no desync guess)" % (op, ip))
            return Insn(ip, op, ".byte", 1, bytes([op]), "opcode %d (UNKNOWN)" % op,
                        [], [], {}, unknown=True)
        name = OP_NAMES[op]
        handler = HANDLERS.get(op)
        try:
            if handler is None:
                # zero-operand opcodes (RETURN/END/nops/etc.)
                fields, text, refs, note = {}, "", [], None
                size = 1
                raw = bytes(bc[ip:ip + 1])
                return Insn(ip, op, name, size, raw, text, self._successors(
                    op, ip, size, fields), refs, fields, note=note)
            r = ParabolaHelper(bc, ip, op)
            fields, text, refs, note = handler(r)
            size = r.p - ip
            raw = bytes(bc[ip:r.p])
            succ = self._successors(op, ip, size, fields)
            return Insn(ip, op, name, size, raw, text, succ, refs, fields, note=note)
        except Truncated:
            self.warnings.append(
                "TRUNCATED operands for %s at IP %d (blob ends mid-instruction); "
                "emitted .byte, path stopped" % (name, ip))
            return Insn(ip, op, ".byte", 1, bytes([op]),
                        "truncated %s (UNKNOWN tail)" % name, [], [], {},
                        unknown=True)

    def _successors(self, op, ip, size, fields):
        end = ip + size
        bc_len = len(self.sec.bytecode)
        succ = []

        def push(t):
            if t >= bc_len:
                self.warnings.append(
                    "%s at IP %d: target %d reaches past bytecode end" %
                    (OP_NAMES.get(op, "?"), ip, t))
            elif t not in succ:
                succ.append(t)

        if op == 0:                      # EVAL: fallthrough + conditional false-jump
            push(end)
            if fields.get("false_off"):
                push(end + fields["false_off"])
        elif op == 1:                    # JUMP (forward-only in legacy)
            push(end + fields["offset"])
        elif op == 7:                    # CALL_FUNC: callee + return address
            push(fields["target"])
            push(end)
        elif op in (2, 255):             # RETURN / END terminate the path
            pass
        else:
            push(end)
        return succ

    def run(self):
        self.build_seeds()
        seen_seed_ip = set()
        for label, ip in self.seeds:
            if ip in seen_seed_ip:
                continue
            seen_seed_ip.add(ip)
            work = deque([ip])
            while work:
                cur = work.popleft()
                if cur in self.insns:
                    continue
                ins = self._decode_one(cur)
                self.insns[cur] = ins
                self.owner.setdefault(cur, label)
                for t in ins.succ:
                    if t not in self.insns:
                        work.append(t)


# --- map file parsing ------------------------------------------------------

class MapSections:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.data = fh.read()
        self.warnings = []
        self.method = None
        self.off_static_funcs = self.off_tile_events = self.off_bytecode = None

    # -- header --
    def parse_header(self):
        d = self.data
        if len(d) < 42:
            sys.exit("error: file too small for map header (%d bytes)" % len(d))
        (self.version,) = struct.unpack_from("<B", d, 0)
        (self.compile_date,) = struct.unpack_from("<i", d, 1)
        (self.spawn_index,) = struct.unpack_from("<H", d, 5)
        self.spawn_dir = d[7]
        (self.num_nodes, self.data_size_polys, self.num_lines,
         self.num_normals, self.num_normal_sprites, self.num_z_sprites,
         self.num_tile_events, self.bytecode_size) = struct.unpack_from("<8H", d, 11)
        (self.total_maya_cameras,) = struct.unpack_from("<B", d, 27)
        (self.total_maya_camera_keys,) = struct.unpack_from("<H", d, 28)
        if self.version != 3:
            self.warnings.append("unexpected map version %d (expected 3)" % self.version)

    # -- method 1: marker-chain constraint walk (research 2026-08-25 §Method) --
    def locate_by_markers(self):
        d = self.data
        marks = []
        pos = d.find(MARKER)
        while pos != -1:
            marks.append(pos)
            pos = d.find(MARKER, pos + 1)
        mark_set = set(marks)
        n, b = self.num_tile_events, self.bytecode_size
        for i, m1 in enumerate(marks):
            m2 = m1 + 4 + 24               # staticFuncs: 12 x u16
            if m2 not in mark_set:
                continue
            m3 = m2 + 4 + n * 8            # tileEvents: N pairs of i32
            if m3 not in mark_set:
                continue
            m4 = m3 + 4 + b                # mapByteCode
            if m4 in mark_set:
                self.off_static_funcs = m1 + 4
                self.off_tile_events = m2 + 4
                self.off_bytecode = m3 + 4
                self.method = "marker-chain walk (sizes verified vs header)"
                return True
        return False

    # -- method 2: full forward parse mirroring LoadingManager.cpp:463-553 --
    def locate_by_forward_parse(self):
        d = self.data
        off = 42                            # header block
        try:
            off += 4                        # DEADBEEF
            media_count = struct.unpack_from("<H", d, off)[0]
            off += 2 + media_count * 2
            off += 4                        # DEADBEEF
            off += self.num_normals * 3 * 2  # normals
            off += 4
            off += self.num_nodes * 2       # nodeOffsets
            off += 4
            off += self.num_nodes           # nodeNormalIdxs
            off += 4
            off += self.num_nodes * 2 * 2   # childOffset1 + childOffset2 (no marker between, file-side)
            off += 4
            off += self.num_nodes * 4       # nodeBounds
            off += 4
            off += self.data_size_polys     # nodePolys
            off += 4
            off += (self.num_lines + 1) // 2 + self.num_lines * 4  # lineFlags/Xs/Ys
            off += 4
            off += 1024                     # heightMap
            off += 4
            nsprites = self.num_normal_sprites + max(0, self.num_z_sprites)
            off += nsprites * 3             # sprite X + Y coords + info-low bytes
            off += 4                        # (one marker; LoadingManager.cpp:486-512)
            off += nsprites * 2             # info high u16s
            off += 4
            off += max(0, self.num_z_sprites) * 2 + 8  # z Z-values | marker | z info bytes | marker
            self.off_static_funcs = off
            off += 24 + 4
            self.off_tile_events = off
            off += self.num_tile_events * 8 + 4
            self.off_bytecode = off
            self.method = "forward parse (LoadingManager walk)"
            return True
        except struct.error:
            return False

    def load(self):
        self.parse_header()
        if not self.locate_by_markers():
            if not self.locate_by_forward_parse():
                sys.exit("error: could not locate script sections in %s" % self.path)
        else:
            # cross-check against the forward parse; warn on disagreement
            sf, te, bc = self.off_static_funcs, self.off_tile_events, self.off_bytecode
            saved = (sf, te, bc)
            self.off_static_funcs = self.off_tile_events = self.off_bytecode = None
            if self.locate_by_forward_parse() and (self.off_static_funcs,
                                                   self.off_tile_events,
                                                   self.off_bytecode) != saved:
                self.warnings.append(
                    "marker-chain offsets disagree with forward parse: %r vs %r"
                    % (saved, (self.off_static_funcs, self.off_tile_events,
                               self.off_bytecode)))
            (self.off_static_funcs, self.off_tile_events,
             self.off_bytecode) = saved
            self.method = "marker-chain walk (sizes verified vs header)"
        d = self.data
        end_sf = self.off_static_funcs + 24
        end_te = self.off_tile_events + self.num_tile_events * 8
        end_bc = self.off_bytecode + self.bytecode_size
        if end_bc > len(d):
            sys.exit("error: bytecode block [%d,%d) exceeds file size %d" %
                     (self.off_bytecode, end_bc, len(d)))
        self.static_funcs = list(struct.unpack_from("<12H", d, self.off_static_funcs))
        self.tile_events = []
        for i in range(self.num_tile_events):
            w0, w1 = struct.unpack_from("<ii", d, self.off_tile_events + i * 8)
            self.tile_events.append((w0 & 0xFFFFFFFF, w1 & 0xFFFFFFFF))
        self.bytecode = d[self.off_bytecode:end_bc]


def parse_func_list(spec):
    out = []
    for tok in spec.split(","):
        tok = tok.strip()
        if tok:
            try:
                out.append(int(tok, 0))
            except ValueError:
                sys.exit("error: bad --funcs entry %r (expected integer IP)" % tok)
    return out


# --- rendering -------------------------------------------------------------

def label_for(ip, call_targets):
    return ("func_%d" if ip in call_targets else "loc_%d") % ip


def render_listing(dis):
    sec = dis.sec
    out = []
    w = out.append
    w("=" * 78)
    w("Doom II RPG location script disassembly: %s" % sec.path)
    w("=" * 78)
    w("header: version=%d compileDate=%d spawn=(%d,%d) numTileEvents=%d "
      "mapByteCodeSize=%d" % (sec.version, sec.compile_date, sec.spawn_index,
                              sec.spawn_dir, sec.num_tile_events,
                              sec.bytecode_size))
    w("sections: staticFuncs@%d tileEvents@%d (0x%X) bytecode@%d..%d [%s]" % (
        sec.off_static_funcs, sec.off_tile_events, sec.off_tile_events,
        sec.off_bytecode, sec.off_bytecode + sec.bytecode_size, sec.method))
    w("staticFuncs: %s" % ", ".join(
        "[%d]=%d%s" % (i, v, "" if v != SCR_NOT_DEFINED else " (not defined)")
        for i, v in enumerate(sec.static_funcs)))
    w("")
    w("--- entries (%d) ---" % len(dis.seeds))
    for i, (w0, w1) in enumerate(sec.tile_events):
        ip = (w0 >> 16) & 0xFFFF
        tile = w0 & 0x3FF
        w("EVT %-4d tile %2d,%2d (idx %3d) entry IP %-5d trigger: %s" % (
            i, tile % 32, tile // 32, tile, ip, trigger_desc(w1)))
    w("")

    # labels: any referenced target or seed start
    inbound = {}
    call_targets = set()
    for ins in dis.insns.values():
        for t in ins.succ:
            inbound.setdefault(t, []).append(ins.ip)
            if ins.op == 7 and t == ins.fields.get("target"):
                call_targets.add(t)
    for _label, ip in dis.seeds:
        inbound.setdefault(ip, [])

    w("--- disassembly ---")
    seed_starts = {}
    for label, ip in dis.seeds:
        seed_starts.setdefault(ip, []).append(label)
    for ip in sorted(dis.insns):
        ins = dis.insns[ip]
        tags = seed_starts.get(ip)
        if tags:
            w("; ---- entry: %s ----" % ", ".join(tags))
        if ip in inbound and (tags or ip in call_targets or
                              any(src != ip for src in inbound[ip])):
            w("%s:" % label_for(ip, call_targets))
        raw_hex = " ".join("%02X" % b for b in ins.raw)
        tgt_txt = ""
        if ins.op == 1:
            tgt_txt = (" -> %s" % label_for(ins.succ[0], call_targets)
                       ) if ins.succ else " ; target past blob end"
        elif ins.op == 0:
            tgts = [t for t in ins.succ if t != ip + ins.size]
            if tgts:
                tgt_txt = " ; iffalse -> %s" % label_for(tgts[0], call_targets)
        elif ins.op == 7:
            tgt_txt = " func_%d" % ins.fields["target"]
        line = "%5d: %-10s %-34s%s" % (ip, ins.name, ins.text, tgt_txt)
        if ins.unknown:
            line += "  ; RAW: %s" % raw_hex
        if ins.note:
            line += "  ; %s" % ins.note
        w(line)
    w("")

    # unreachable bytes (never claimed by any decoded instruction)
    covered = bytearray(len(sec.bytecode))
    for ins in dis.insns.values():
        for j in range(ins.ip, min(ins.ip + ins.size, len(covered))):
            covered[j] = 1
    runs = []
    j = 0
    n = len(covered)
    while j < n:
        if not covered[j]:
            k = j
            while k < n and not covered[k]:
                k += 1
            runs.append((j, k))
            j = k
        else:
            j += 1
    if runs:
        w("--- unreachable / data bytes ---")
        for a, b in runs:
            chunk = sec.bytecode[a:b]
            w("%5d-%5d: .byte %s%s" % (
                a, b - 1, " ".join("%02X" % x for x in chunk[:24]),
                " ..." if b - a > 24 else ""))
        w("")
    return out


def render_xref(dis):
    sec = dis.sec
    refs = {}
    for ins in dis.insns.values():
        owner = dis.owner.get(ins.ip, "?")
        for kind, key in ins.refs:
            refs.setdefault(kind, {}).setdefault(key, []).append((ins.ip, owner))

    def fmt_owner(owner):
        return owner

    out = []
    w = out.append
    w("--- cross-reference summary ---")
    order = [
        ("door", "doors (sprite/action)"),
        ("sprite", "sprites"),
        ("string", "strings"),
        ("event", "events"),
        ("item", "item defs"),
        ("quest", "quests"),
        ("sound", "sounds"),
        ("camera", "cameras"),
        ("tile", "tiles"),
        ("map", "map changes"),
    ]
    for kind, title in order:
        bucket = refs.get(kind)
        if not bucket:
            continue
        w("%s:" % title)
        for key in sorted(bucket, key=lambda k: (str(type(k)), str(k))):
            sites = bucket[key]
            sites.sort()
            site_txt = ", ".join(
                "%s@%d" % (fmt_owner(o), ip) for ip, o in
                dict.fromkeys(sites))  # dedupe, keep order
            w("  %-22s <- %s" % (key, site_txt))
        w("")
    return out


# --- map00 anchor verification ---------------------------------------------

class VerifyRun:
    def __init__(self):
        self.fails = 0

    def check(self, ok, msg):
        print("  %s  %s" % ("PASS" if ok else "FAIL", msg))
        if not ok:
            self.fails += 1


def run_verify(sec, dis):
    v = VerifyRun()
    print("verify: map00 anchors (docs/original-code/tile-events-vm.md section 5,"
          " docs/research/2026-08-25 sections 2-3)")

    ins = dis.insns

    def has(ip):
        return ip in ins and not ins[ip].unknown

    v.check(sec.num_tile_events == 167 and sec.bytecode_size == 10015,
            "header counts numTileEvents=167 mapByteCodeSize=10015 (got %d/%d)"
            % (sec.num_tile_events, sec.bytecode_size))
    v.check(sec.off_bytecode == 62019,
            "bytecode blob at file offset 62019 (got %s)" % sec.off_bytecode)
    v.check(sec.off_tile_events == 0xED07,
            "tileEvents block at file offset 0xED07 (got %s)" % hex(sec.off_tile_events))
    sf = sec.static_funcs
    v.check(sf[0] == 0 and sf[6] == 253 and
            all(sf[i] == SCR_NOT_DEFINED for i in range(12) if i not in (0, 6)),
            "staticFuncs [0]=0 INIT_MAP [6]=253 PER_TURN rest undefined")

    # Anchor A: imp MAKE_CORPSE cluster @1091-1127 (research section 2)
    v.check(has(1091) and ins[1091].op == 0 and
            ins[1091].text.replace(" ", "") == "[v26==0]",
            "1091 EVAL [v26 == 0]")
    v.check(has(1091) and 1091 + ins[1091].size + ins[1091].fields["false_off"] == 1111,
            "1091 EVAL false-target 1111")
    for ip, spr, tx, ty in ((1098, 41, 14, 3), (1103, 42, 14, 4),
                            (1111, 41, 4, 21), (1116, 42, 8, 18),
                            (1122, 121, 27, 23)):
        f = ins[ip].fields if has(ip) else {}
        v.check(has(ip) and ins[ip].op == 72 and f.get("sprite") == spr and
                f.get("tx") == tx and f.get("ty") == ty,
                "%d MAKE_CORPSE sprite=%d dst=(%d,%d)" % (ip, spr, tx, ty))
    v.check(has(1108) and ins[1108].op == 1 and
            1108 + ins[1108].size + ins[1108].fields["offset"] == 1121,
            "1108 JUMP -> 1121")
    v.check(has(1121) and ins[1121].op == 2, "1121 RETURN")
    v.check(has(1127) and ins[1127].op == 2, "1127 RETURN")

    # Anchor B: blue-door player-with-key trigger @4161 (tile-events-vm section 5.1)
    f = ins[4161].fields if has(4161) else {}
    v.check(has(4161) and ins[4161].op == 8 and f.get("cls") == 0 and
            f.get("idx") == 20 and f.get("dst") == 27,
            "4161 ITEM_COUNT inventory[20] -> var27")
    v.check(has(4165) and ins[4165].op == 0 and
            ins[4165].text.replace(" ", "") == "[v27!=0]" and
            4165 + ins[4165].size + ins[4165].fields["false_off"] == 4190,
            "4165 EVAL [v27 != 0] iffalse -> 4190")
    f = ins[4172].fields if has(4172) else {}
    v.check(has(4172) and ins[4172].op == 23 and f.get("event") == 60 and
            f.get("disable"), "4172 EVENTOP disable event[60]")
    f = ins[4175].fields if has(4175) else {}
    v.check(has(4175) and ins[4175].op == 33 and f.get("def") == 111,
            "4175 GIVEITEM def=111 (blue keycard)")
    f = ins[4179].fields if has(4179) else {}
    v.check(has(4179) and ins[4179].op == 21 and f.get("sprite") == 22 and
            f.get("act") == 3 and not f.get("quiet"),
            "4179 DOOROP sprite=22 UNLOCK (interactive)")
    f = ins[4182].fields if has(4182) else {}
    v.check(has(4182) and ins[4182].op == 14 and f.get("ms") == 300,
            "4182 WAIT 300ms")
    f = ins[4184].fields if has(4184) else {}
    v.check(has(4184) and ins[4184].op == 21 and f.get("sprite") == 22 and
            f.get("act") == 0, "4184 DOOROP sprite=22 OPEN")
    v.check(has(4187) and ins[4187].op == 1 and
            4187 + ins[4187].size + ins[4187].fields["offset"] == 4222 and
            has(4222) and ins[4222].op == 2,
            "4187 JUMP -> 4222 RETURN (doc quotes 4220; bytes land on 4222)")
    f = ins[4219].fields if has(4219) else {}
    v.check(has(4219) and ins[4219].op == 3 and f.get("str") == 127 and
            f.get("style3"), '4219 MESSAGE str=127 style=important ("locked")')

    # Anchor C: EVT 617 head @2993 — retire sibling triggers
    for ip, ev in ((2993, 54), (2996, 41), (2999, 51)):
        f = ins[ip].fields if has(ip) else {}
        v.check(has(ip) and ins[ip].op == 23 and f.get("event") == ev and
                f.get("disable"),
                "%d EVENTOP disable event[%d]" % (ip, ev))

    # Static-func bodies documented in section 5.3
    v.check(has(0) and ins[0].op == 0 and
            ins[0].text.replace(" ", "") == "[v17==0]",
            "0 (INIT_MAP) EVAL [v17 == 0] new-game gate")
    v.check(has(253) and ins[253].op == 0 and
            ins[253].text.replace(" ", "") == "[v28==1]",
            "253 (PER_TURN) EVAL [v28 == 1]")
    f = ins[260].fields if has(260) else {}
    v.check(has(260) and ins[260].op == 8 and f.get("cls") == 0 and
            f.get("idx") == 20 and f.get("dst") == 27,
            "260 (PER_TURN) ITEM_COUNT inventory[20] -> var27")

    print("verify: %d failure(s)" % v.fails)
    return v.fails == 0


# --- main ------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(
        description="CFG-based disassembler for Doom II RPG mapXX.bin scripts.",
        epilog="example: python3 tools/disasm_map_scripts.py tmp_map00.bin "
               "-o /tmp/map00_disasm.txt --verify")
    ap.add_argument("map", help="path to mapXX.bin")
    ap.add_argument("-o", "--output", help="write listing to file (default stdout)")
    ap.add_argument("--funcs", default="", help="extra entry IPs, comma separated")
    ap.add_argument("--ipa", default=None,
                    help="game archive (optional). String-id resolution is not "
                         "implemented; raw ids are printed either way.")
    ap.add_argument("--verify", action="store_true",
                    help="run the built-in map00 anchor suite after decoding "
                         "(exit 1 on failure)")
    args = ap.parse_args(argv)

    if args.ipa:
        print("note: --ipa accepted but string tables are not parsed; "
              "string references are shown as raw ids.")

    sec = MapSections(args.map)
    sec.load()
    for wn in sec.warnings:
        print("WARNING: %s" % wn)

    dis = Disassembler(sec, parse_func_list(args.funcs))
    dis.run()

    out = render_listing(dis)
    out += render_xref(dis)
    if dis.warnings:
        out.append("--- warnings (%d) ---" % len(dis.warnings))
        for wn in dis.warnings:
            out.append("WARNING: %s" % wn)
        out.append("")

    text = "\n".join(out) + "\n"
    if args.output:
        with open(args.output, "w") as fh:
            fh.write(text)
        print("listing written to %s" % args.output)
    else:
        sys.stdout.write(text)

    if args.verify:
        ok = run_verify(sec, dis)
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
