#!/usr/bin/env python3
# Read-only scanner used by docs/research/2026-08-24-loot-inventory.md.
# Disassembles tmp_map00.bin bytecode (via tools/map_to_obj.py parser) and lists
# loot-relevant opcodes (GIVEITEM / ASSIGN_LOOTSET / MAKE_CORPSE / GIVELOOT).
# Operand sizes derived from each case in src/ScriptThread.cpp:243-2038.
import sys, gzip, struct
sys.path.insert(0, '/Volumes/Goida/Project/DoomIIRPG-RE/tools')
import map_to_obj as M

data = open('/Volumes/Goida/Project/DoomIIRPG-RE/tmp_map00.bin','rb').read()
md = M.parse_map(data)
bc = md.bytecode
print(f"bytecode size {len(bc)}  tileEvents {len(md.tile_events)}")

# entities.bin defs (src/EntityDef.cpp:31-44): u16 count then 8 bytes per def.
ent = open('/Volumes/Goida/Project/DoomIIRPG-RE/tmp_entities.bin','rb').read()
try:
    ent = gzip.decompress(ent)
except OSError:
    pass
n = struct.unpack_from('<H', ent, 0)[0]
defs = []
off = 2
for i in range(n):
    ti, et, est, parm, name, lname, desc = struct.unpack_from('<hBBBBBB', ent, off)
    defs.append((ti, et, est, parm))
    off += 8
print(f"entity defs: {n}")
print("WEAPON defs (eType=6 eSubType=1):")
for ti, et, est, parm in defs:
    if et == 6 and est == 1:
        print(f"  tileIndex={ti} parm(WP_)={parm}")
print("KEYCARD/inventory defs of interest:")
for ti, et, est, parm in defs:
    if et == 6 and est == 0 and parm in (16,17,18,19,20,21,22,23,24,25):
        print(f"  tileIndex={ti} parm(INV_)={parm}")

# opcode table: op -> operand spec chars; B=u8 b=s8 S=u16BE s=s16BE I=i32BE
FIX = {
1:'S',2:'',3:'S',5:'b',6:'BS',7:'S',8:'SB',9:'SB',10:'B',11:'BS',12:'SS',13:'BB',
14:'B',15:'S',16:'',17:'BBB',18:'B',19:'Bb',20:'bbb',21:'S',22:'BB',23:'S',24:'B',
25:'SB',26:'B',27:'B',28:'B',29:'B',30:'B',31:'BSB',32:'S',33:'BBb',34:'BB',36:'Bs',
37:'BB',38:'S',39:'',40:'BS',42:'S',43:'BB',44:'',45:'B',46:'',47:'SBB',48:'S',
49:'sB',50:'',51:'SB',52:'',53:'BBBB',54:'',55:'',56:'S',57:'I',58:'I',59:'BBI',
60:'s',61:'SSB',62:'',65:'',67:'B',68:'B',69:'B',71:'BBB',72:'SBB',73:'B',74:'',
75:'IS',76:'',77:'b',78:'b',79:'b',80:'',81:'',82:'S',84:'S',85:'',86:'b',
87:'BBBB',88:'BBBB',89:'B',90:'S',91:'',92:'BB',93:'B',94:'',95:'B',96:'ISB',
97:'b',255:'',
}
NAMES = {1:'JUMP',2:'RETURN',3:'MESSAGE',33:'GIVEITEM',41:'GIVELOOT',72:'MAKE_CORPSE',
         83:'ASSIGN_LOOTSET',24:'HIDE',8:'ITEM_COUNT',21:'DOOROP',47:'RESPAWN_MONSTER',
         25:'DROPITEM',35:'DROPMONSTERITEM',255:'END'}

def u8(a): return bc[a]
def s8(a): return struct.unpack_from('>b', bc, a)[0]
def u16(a): return struct.unpack_from('>H', bc, a)[0]
def s16(a): return struct.unpack_from('>h', bc, a)[0]
def i32(a): return struct.unpack_from('>i', bc, a)[0]

lines = []
ip = 0
size = len(bc)
while ip < size:
    op = bc[ip]
    start = ip
    ip += 1
    if op == 0:  # EV_EVAL, src/ScriptThread.cpp:244-312
        cnt = u8(ip); ip += 1
        terms = []
        for _ in range(cnt):
            t = u8(ip); ip += 1
            if t & 0x40:
                nb = u8(ip); ip += 1
                v = (((t & 0x3F) << 8 | nb) << 18) >> 18
                terms.append(f"c{v}")
            elif t & 0x80:
                terms.append(f"v{t&0x7F}")
            else:
                terms.append(f"op{t}")
        off_ = u8(ip); ip += 1
        lines.append((start, f"EVAL [{' '.join(terms)}] iff->+{off_}"))
        continue
    if op == 4:  # EV_LERPSPRITE, src/ScriptThread.cpp:344-352
        a = u8(ip)|u8(ip+1)<<8|u8(ip+2)<<16; ip += 3
        flags = a & 0xF
        extra = ''
        if not flags & 8:
            extra += f" z={u8(ip)-48}"; ip += 1
        if not flags & 4:
            extra += f" t={u8(ip)*100}"; ip += 1
        lines.append((start, f"LERPSPRITE sp={(a>>14)&0xFF} dst={(a>>9)&0x1F},{(a>>4)&0x1F} fl={flags}{extra}"))
        continue
    if op == 35:  # EV_DROPMONSTERITEM, src/ScriptThread.cpp:1023-1041
        s = u16(ip); ip += 2
        b1 = u8(ip); ip += 1
        if s & 0x8000:
            b1 = b1<<8 | u8(ip); ip += 1
        b2 = u8(ip); ip += 1
        lines.append((start, f"DROPMONSTERITEM sprite={s&0x7FFF} def={b1} qty={b2}"))
        continue
    if op == 41:  # EV_GIVELOOT, composeLootDialog src/ScriptThread.cpp:2135-2145
        cnt = u8(ip); ip += 1
        ents = []
        for _ in range(cnt):
            v = u16(ip); ip += 2
            cls = v >> 12 & 0xF
            if cls == 6: ents.append(f"text{v&0xFFF}")
            elif cls == 5: ents.append(f"quest{v&0xFFF}")
            else: ents.append(f"g(cls{cls},idx{(v&0xFC0)>>6},x{v&0x3F})")
        lines.append((start, f"GIVELOOT n={cnt} [{'; '.join(ents)}]"))
        continue
    if op == 66:  # EV_DEBUGPRINT, src/ScriptThread.cpp:1515-1524
        mode = u8(ip); ip += 1
        if mode == 0:
            s = ''
            while True:
                c = u8(ip); ip += 1
                if c == 0: break
                s += chr(c)
            lines.append((start, f"DEBUGPRINT '{s}'"))
        else:
            lines.append((start, f"DEBUGPRINT var{u8(ip)}")); ip += 1
        continue
    if op == 70:  # EV_STATUS_EFFECT, src/ScriptThread.cpp:1576-1600
        b_ = u8(ip); ip += 1
        if b_ & 0x80:
            lines.append((start, f"STATUS_EFFECT rm {b_&0x7F}"))
        else:
            m = u8(ip); ip += 1
            lines.append((start, f"STATUS_EFFECT add id={b_} mag={m}"))
        continue
    if op == 83:  # EV_ASSIGN_LOOTSET, src/ScriptThread.cpp:1782-1804
        s = u16(ip); ip += 2
        cnt = u8(ip); ip += 1
        ents = []
        for _ in range(cnt):
            v = u16(ip); ip += 2
            cls = v >> 12 & 0xF
            if cls == 6: ents.append(f"named str{v&0xFFF}")
            else: ents.append(f"cls{cls} idx{(v&0xFC0)>>6} x{v&0x3F}")
        lines.append((start, f"ASSIGN_LOOTSET entSprite={s&0xFFF} n={cnt} [{'; '.join(ents)}]"))
        continue
    spec = FIX.get(op)
    if spec is None:
        lines.append((start, f"??? op={op} DESYNC"))
        break
    vals = []
    for c in spec:
        if c == 'B': vals.append(str(u8(ip))); ip += 1
        elif c == 'b': vals.append(str(s8(ip))); ip += 1
        elif c == 'S': vals.append(str(u16(ip))); ip += 2
        elif c == 's': vals.append(str(s16(ip))); ip += 2
        elif c == 'I': vals.append(str(i32(ip))); ip += 4
    nm = NAMES.get(op, f"OP{op}")
    if op == 33:
        mode, a1, a2 = int(vals[0]), int(vals[1]), int(vals[2])
        if mode == 0:
            lines.append((start, f"GIVEITEM mode0 touchSprite={(vals[2] and 0)}{(int(vals[1])<<8)|int(vals[2])}"))
        else:
            d = next(((ti,et,est,p) for ti,et,est,p in defs if ti==a1), None)
            lines.append((start, f"GIVEITEM def={a1}(t{d[0]},eType{d[1]},sub{d[2]},parm{d[3]}) qty={a2}" if d else f"GIVEITEM def={a1}? qty={a2}"))
    else:
        lines.append((start, f"{nm} {','.join(vals)}"))

with open('/Volumes/Goida/Project/DoomIIRPG-RE/docs/research/assets/map00_disasm.txt','w') as f:
    for a, t in lines:
        f.write(f"{a}: {t}\n")
print("disasm lines:", len(lines))
for a, t in lines:
    if any(t.startswith(k) for k in ('GIVEITEM','ASSIGN_LOOTSET','MAKE_CORPSE','GIVELOOT','???')):
        print(a, t)
