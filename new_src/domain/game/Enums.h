#ifndef NEW_DOMAIN_ENUMS_H
#define NEW_DOMAIN_ENUMS_H

// Game constants ported from the legacy src/Enums.h (subset needed by the
// entity/combat/door systems).
namespace newcore {
namespace Enums {

// Entity types.
static constexpr int ET_WORLD = 0;
static constexpr int ET_PLAYER = 1;
static constexpr int ET_MONSTER = 2;
static constexpr int ET_NPC = 3;
static constexpr int ET_PLAYERCLIP = 4;
static constexpr int ET_DOOR = 5;
static constexpr int ET_ITEM = 6;
static constexpr int ET_DECOR = 7;
static constexpr int ET_ENV_DAMAGE = 8;
static constexpr int ET_CORPSE = 9;
static constexpr int ET_ATTACK_INTERACTIVE = 10;
static constexpr int ET_MONSTERBLOCK_ITEM = 11;
static constexpr int ET_SPRITEWALL = 12;
static constexpr int ET_NONOBSTRUCTING_SPRITEWALL = 13;
static constexpr int ET_DECOR_NOCLIP = 14;
static constexpr int ET_MAX = 15;

} // namespace Enums

// Trace content masks, composed from per-eType bits instead of the bare legacy
// numbers (ADR 0011 §5). Decode table: docs/original-code/player-collision.md
// §2.2; legacy values at src/Enums.h:26-40. Each composed mask is pinned with a
// static_assert on that legacy value — the numbers go straight into a trace, so
// an edit that changes one must fail to compile.
namespace Contents {

constexpr int bit(int eType) { return 1 << eType; }

constexpr int WORLD = bit(Enums::ET_WORLD);
constexpr int PLAYER = bit(Enums::ET_PLAYER);
constexpr int MONSTER = bit(Enums::ET_MONSTER);
constexpr int NPC = bit(Enums::ET_NPC);
constexpr int PLAYERCLIP = bit(Enums::ET_PLAYERCLIP);
constexpr int DOOR = bit(Enums::ET_DOOR);
constexpr int ITEM = bit(Enums::ET_ITEM);
constexpr int DECOR = bit(Enums::ET_DECOR);
constexpr int ENV_DAMAGE = bit(Enums::ET_ENV_DAMAGE);
constexpr int CORPSE = bit(Enums::ET_CORPSE);
constexpr int ATTACK_INTERACTIVE = bit(Enums::ET_ATTACK_INTERACTIVE);
constexpr int MONSTERBLOCK_ITEM = bit(Enums::ET_MONSTERBLOCK_ITEM);
constexpr int SPRITEWALL = bit(Enums::ET_SPRITEWALL);
constexpr int NONOBSTRUCTING_SPRITEWALL = bit(Enums::ET_NONOBSTRUCTING_SPRITEWALL);
constexpr int DECOR_NOCLIP = bit(Enums::ET_DECOR_NOCLIP);

// Player move mask (src/MovementController.cpp:326) — NONOBSTRUCTING_SPRITEWALL
// does block moves.
constexpr int PLAYERSOLID = WORLD | MONSTER | NPC | PLAYERCLIP | DOOR | DECOR |
                            ATTACK_INTERACTIVE | SPRITEWALL | NONOBSTRUCTING_SPRITEWALL;
// Fire ray (src/PlayingInputHandler.cpp:200) = playersolid - PLAYERCLIP + CORPSE.
constexpr int WEAPONSOLID = (PLAYERSOLID & ~PLAYERCLIP) | CORPSE;
// The facing / health-bar probe (src/MovementController.cpp:38).
constexpr int FACING_PROBE = WORLD | MONSTER | NPC | DOOR | ITEM | DECOR |
                             ATTACK_INTERACTIVE | SPRITEWALL | DECOR_NOCLIP;
// Monster move mask (src/Entity.cpp:1078).
constexpr int MONSTERSOLID = WORLD | PLAYER | MONSTER | NPC | DOOR | DECOR |
                             ATTACK_INTERACTIVE | MONSTERBLOCK_ITEM | SPRITEWALL |
                             NONOBSTRUCTING_SPRITEWALL;
// Splash / line-of-sight solidity (src/Combat.cpp:1070).
constexpr int SPLASH_SOLID = WORLD | DOOR | SPRITEWALL;

// Per-weapon modifiers of the fire ray (src/PlayingInputHandler.cpp:203-215).
constexpr int HOLY_WATER_EXTRA = ENV_DAMAGE | DECOR_NOCLIP;   // was | 0x4100
constexpr int MELEE_EXTRA      = PLAYERCLIP;                  // was | 0x10

static_assert(PLAYERSOLID  == 13501, "src/Enums.h CONTENTS_PLAYERSOLID");
static_assert(WEAPONSOLID  == 13997, "src/Enums.h CONTENTS_WEAPONSOLID");
static_assert(FACING_PROBE == 21741, "src/MovementController.cpp:38");
static_assert(MONSTERSOLID == 15535, "src/Enums.h CONTENTS_MONSTERSOLID");
static_assert(SPLASH_SOLID == 4129,  "src/Enums.h CONTENTS_SPLASH_SOLID");
static_assert(HOLY_WATER_EXTRA == 0x4100 && MELEE_EXTRA == 0x10,
	"src/PlayingInputHandler.cpp:203-215");

} // namespace Contents

namespace Enums {

// Legacy CONTENTS_* names kept as aliases of the composed values so call sites
// are not forced to churn; new code uses Contents:: directly.
static constexpr int CONTENTS_ANY = -1;
static constexpr int CONTENTS_PICKUP = 64;
static constexpr int CONTENTS_INTERACTIVE = 1068;
static constexpr int CONTENTS_PLAYERSOLID = Contents::PLAYERSOLID;
static constexpr int CONTENTS_MONSTERSOLID = Contents::MONSTERSOLID;
static constexpr int CONTENTS_WEAPONSOLID = Contents::WEAPONSOLID;
static constexpr int CONTENTS_VIEWSOLID = 5293;
static constexpr int CONTENTS_MONSTERWPSOLID = 5295;
static constexpr int CONTENTS_WORLD = Contents::WORLD;
static constexpr int CONTENTS_SPRITEWALL = 12288;

// CombatEntity stat slots.
static constexpr int STAT_HEALTH = 0;
static constexpr int STAT_MAX_HEALTH = 1;
static constexpr int STAT_ARMOR = 2;
static constexpr int STAT_DEFENSE = 3;
static constexpr int STAT_STRENGTH = 4;
static constexpr int STAT_ACCURACY = 5;
static constexpr int STAT_AGILITY = 6;
static constexpr int STAT_IQ = 7;
static constexpr int STAT_MAX = 8;

// ET_ITEM sub-types: the eSubType the entity defs are looked up with
// (src/LootingSystem.cpp:185-198, src/MenuSystem.cpp:1941,:1950,:1997).
static constexpr int ITEM_CLASS_INVENTORY = 0;
static constexpr int ITEM_CLASS_WEAPON = 1;
static constexpr int ITEM_CLASS_AMMO = 2;

// Player inventory slots (src/Enums.h:196-232), the subset the item screens
// walk. The *_MAX names are exclusive bounds, exactly as the legacy loops use
// them (`for (n = INV_HEALTH_MIN; n < INV_HEALTH_MAX; ++n)`).
static constexpr int INV_DRINK_MIN = 0;
static constexpr int INV_DRINK_MAX = 11;
static constexpr int INV_ARMOR_MIN = 11;
static constexpr int INV_ARMOR_MAX = 13;
static constexpr int INV_HEALTH_MIN = 16;
static constexpr int INV_HEALTH_MAX = 18;
static constexpr int INV_OTHER_HOLY_WATER = 22;
static constexpr int INV_ONE_UAC_CREDIT = 24;

// Ammo types, the index into Player::ammo (src/Enums.h:103-116).
static constexpr int AMMO_NONE = 0;
static constexpr int AMMO_HOLY_WATER = 3;
static constexpr int AMMO_SOUL_CUBE = 6;
static constexpr int AMMO_ITEM = 8;
static constexpr int AMMO_MAX_SOULS = 5;

// Weapon ids / bit positions in Player::weapons (src/Enums.h:134-150).
static constexpr int WP_HOLY_WATER_PISTOL = 2;
static constexpr int WP_ITEM = 14;
static constexpr int WP_PLAYERMAX = 15;

// Door subtypes.
static constexpr int DOOR_LOCKED = 1;
static constexpr int DOOR_UNLOCKED = 2;

// Door tile range (271-281).
static constexpr int TILENUM_FIRST_DOOR = 271;
static constexpr int TILENUM_LAST_DOOR = 281;

// Shootable practice-target decor (src/Enums.h:763; tested as 0x95 against the
// sprite tile number at src/PlayingInputHandler.cpp:348).
static constexpr int TILENUM_PRACTICE_TARGET = 149;

// Bosses.
static constexpr int FIRSTBOSS = 12;
static constexpr int LASTBOSS = 16;

// Character art tiles (src/Enums.h:708-719): stacked leg/torso/head sheets.
static constexpr int TILENUM_FIRST_NPC = 65;
static constexpr int TILENUM_LAST_NPC = 80;
static constexpr int TILENUM_NPC_RILEY_OCONNOR = 66;
static constexpr int TILENUM_NPC_SARGE = 72;
static constexpr int TILENUM_SHADOW = 232;             // src/Enums.h:830

// Monster art tiles (src/Enums.h:665-707) — classification-side subsets.
static constexpr int TILENUM_MONSTER_LOST_SOUL  = 29;  // ..31  (floater, src/Render.cpp:3007-3009)
static constexpr int TILENUM_MONSTER_LOST_SOUL3 = 31;
static constexpr int TILENUM_MONSTER_CACODEMON  = 41;  // ..43  (floater, src/Render.cpp:3003-3005)
static constexpr int TILENUM_MONSTER_CACODEMON3 = 43;
static constexpr int TILENUM_MONSTER_SENTINEL   = 44;  // ..46  (floater, src/Render.cpp:2979-2981)
static constexpr int TILENUM_MONSTER_SENTINEL3  = 46;
static constexpr int TILENUM_MONSTER_ARACHNOTRON = 53; // special boss, src/Render.cpp:3027-3029
static constexpr int TILENUM_BOSS_PINKY          = 56;
static constexpr int TILENUM_BOSS_MASTERMIND     = 57;
static constexpr int TILENUM_BOSS_VIOS           = 58; // ..62
static constexpr int TILENUM_BOSS_VIOS5          = 62;

// Sprite flags.
static constexpr int SPRITE_FLAG_HIDDEN = 0x10000;
static constexpr int SPRITE_FLAG_FLIP_HORIZONTAL = 0x20000;
static constexpr int SPRITE_FLAG_FLIP_VERTICAL = 0x40000;
static constexpr int SPRITE_FLAG_AUTO_ANIMATE = 0x80000;
static constexpr int SPRITE_FLAG_TWO_SIDED = 0x100000;
static constexpr int SPRITE_FLAG_AUTOMAP_VISIBLE = 0x200000;
static constexpr int SPRITE_FLAG_NOENTITY = 0x200000;
static constexpr int SPRITE_FLAG_TILE = 0x400000;
static constexpr int SPRITE_FLAG_SOLIDSIDE = 0x800000;
static constexpr int SPRITE_FLAG_NORTH = 0x1000000;
static constexpr int SPRITE_FLAG_SOUTH = 0x2000000;
static constexpr int SPRITE_FLAG_EAST = 0x4000000;
static constexpr int SPRITE_FLAG_WEST = 0x8000000;
static constexpr int SPRITE_FLAG_DECAL = 0x10000000;
static constexpr int SPRITE_FLAG_FLAT = 0x20000000;
static constexpr int SPRITE_FLAG_DOORLERP = 0x80000000;

// Monster animation modes/frames.
static constexpr int MANIM_MASK = 240;
static constexpr int MANIM_IDLE = 0;
static constexpr int MANIM_IDLE_BACK = 16;
static constexpr int MANIM_WALK_FRONT = 32;
static constexpr int MANIM_WALK_BACK = 48;
static constexpr int MANIM_ATTACK1 = 64;
static constexpr int MANIM_ATTACK2 = 80;
static constexpr int MANIM_PAIN = 96;
static constexpr int MANIM_DEAD = 112;
static constexpr int MANIM_SLAP = 128;
static constexpr int MANIM_DODGE = 144;
static constexpr int MANIM_NPC_TALK = 160;
static constexpr int MANIM_NPC_BACK_ACTION = 176;
static constexpr int MFRAME_MASK = 15;

// tileEvents VM opcodes (src/Enums.h:399-497, verbatim ids).
static constexpr int EV_EVAL = 0;
static constexpr int EV_JUMP = 1;
static constexpr int EV_RETURN = 2;
static constexpr int LAST_INTERNAL_CMD = 2;
static constexpr int EV_END = -1; // byte value 255 in mapByteCode
static constexpr int EV_FIRST_COMMAND = 3;
static constexpr int EV_MESSAGE = 3;
static constexpr int EV_LERPSPRITE = 4;
static constexpr int EV_STARTCINEMATIC = 5;
static constexpr int EV_SETSTATE = 6;
static constexpr int EV_CALL_FUNC = 7;
static constexpr int EV_ITEM_COUNT = 8;
static constexpr int EV_TILE_EMPTY = 9;
static constexpr int EV_WEAPON_EQUIPPED = 10;
static constexpr int EV_CHANGE_MAP = 11;
static constexpr int EV_CAMERA_STR = 12;
static constexpr int EV_DIALOG = 13;
static constexpr int EV_WAIT = 14;
static constexpr int EV_GOTO = 15;
static constexpr int EV_ABORT_MOVE = 16;
static constexpr int EV_ENTITY_FRAME = 17;
static constexpr int EV_ADV_CAMERAKEY = 18;
static constexpr int EV_DAMAGEMONSTER = 19;
static constexpr int EV_DAMAGEPLAYER = 20;
static constexpr int EV_DOOROP = 21;
static constexpr int EV_MONSTERFLAGOP = 22;
static constexpr int EV_EVENTOP = 23;
static constexpr int EV_HIDE = 24;
static constexpr int EV_DROPITEM = 25;
static constexpr int EV_PREVSTATE = 26;
static constexpr int EV_NEXTSTATE = 27;
static constexpr int EV_WAKEMONSTER = 28;
static constexpr int EV_SHOW_PLAYERATTACK = 29;
static constexpr int EV_MONSTER_PARTICLES = 30;
static constexpr int EV_SPAWN_PARTICLES = 31;
static constexpr int EV_FADEOP = 32;
static constexpr int EV_GIVEITEM = 33;
static constexpr int EV_NAMEENTITY = 34;
static constexpr int EV_DROPMONSTERITEM = 35;
static constexpr int EV_SETDEATHFUNC = 36;
static constexpr int EV_PLAYSOUND = 37;
static constexpr int EV_NPCCHAT = 38;
static constexpr int EV_STOCKSTATION = 39;
static constexpr int EV_LERPFLAT = 40;
static constexpr int EV_GIVELOOT = 41;
static constexpr int EV_MARKTILE = 42;
static constexpr int EV_UPDATEJOURNAL = 43;
static constexpr int EV_BRIBE_ENTITY = 44;
static constexpr int EV_PLAYER_ADD_STAT = 45;
static constexpr int EV_PLAYER_ADD_RECIPE = 46;
static constexpr int EV_RESPAWN_MONSTER = 47;
static constexpr int EV_SCREEN_SHAKE = 48;
static constexpr int EV_SPEECHBUBBLE = 49;
static constexpr int EV_AWARDSECRET = 50;
static constexpr int EV_AIGOAL = 51;
static constexpr int EV_ADVANCETURN = 52;
static constexpr int EV_MINIGAME = 53;
static constexpr int EV_ENDMINIGAME = 54;
static constexpr int EV_ENDROUND = 55;
static constexpr int EV_PLAYERATTACK = 56;
static constexpr int EV_SET_FOG_COLOR = 57;
static constexpr int EV_LERP_FOG = 58;
static constexpr int EV_LERPSPRITEOFFSET = 59;
static constexpr int EV_DISABLED_WEAPONS = 60;
static constexpr int EV_LERPSCALE = 61;
static constexpr int EV_GIVEAWARD = 62;
static constexpr int EV_STARTMIXING = 65;
static constexpr int EV_DEBUGPRINT = 66;
static constexpr int EV_GOTO_MENU = 67;
static constexpr int EV_START_INTERCINEMATIC = 68;
static constexpr int EV_TURN_PLAYER = 69;
static constexpr int EV_STATUS_EFFECT = 70;
static constexpr int EV_JOURNAL_TILE = 71;
static constexpr int EV_MAKE_CORPSE = 72;
static constexpr int EV_INVENTORY_OP = 73;
static constexpr int EV_END_GAME = 74;
static constexpr int EV_LERPSPRITEPARABOLA = 75;
static constexpr int EV_TOGGLE_OVERLAY = 76;
static constexpr int EV_FOG_AFFECTS_SKYMAP = 77;
static constexpr int EV_ENABLE_HELP = 78;
static constexpr int EV_SET_MM_RENDER_HACK = 79;
static constexpr int EV_START_ARMORREPAIR = 80;
static constexpr int EV_FORCE_BOT_RETURN = 81;
static constexpr int EV_UNMARKTILE = 82;
static constexpr int EV_ASSIGN_LOOTSET = 83;
static constexpr int EV_START_TARGETPRACTICE = 84;
static constexpr int EV_GIVE_AUTOMAP = 85;
static constexpr int EV_ANGER_VIOS = 86;
static constexpr int EV_UNHIDE_AUTOMAP = 87;
static constexpr int EV_HIDE_AUTOMAP = 88;
static constexpr int EV_PORTAL_EVENT = 89;
static constexpr int EV_PITCH_CONTROL = 90;
static constexpr int EV_USED_CHAINSAW = 91;
static constexpr int EV_ENTITY_BREATHES = 92;
static constexpr int EV_DESTROY_PLAYER = 93;
static constexpr int EV_START_TREADMILL = 94;
static constexpr int EV_STOPSOUND = 95;
static constexpr int EV_LERPSPRITEPARABOLA_SCALE = 96;
static constexpr int EV_SET_CALDEX_RENDER_HACK = 97;

// Built-in static script slots (src/Enums.h:498-511).
static constexpr int SCR_INIT_MAP = 0;
static constexpr int SCR_END_GAME = 1;
static constexpr int SCR_BOSS_75 = 2;
static constexpr int SCR_BOSS_50 = 3;
static constexpr int SCR_BOSS_25 = 4;
static constexpr int SCR_BOSS_DEAD = 5;
static constexpr int SCR_PER_TURN = 6;
static constexpr int SCR_ATTACK_NPC = 7;
static constexpr int SCR_MONSTER_DEATH = 8;
static constexpr int SCR_MONSTER_ACTIVATE = 9;
static constexpr int SCR_CHICKEN_KICKED = 10;
static constexpr int SCR_ITEM_PICKUP = 11;
static constexpr int MAX_BUILT_IN_CMD = 12;
static constexpr int SCR_NOT_DEFINED = 65535;

// EV_EVAL term encoding (src/Enums.h:512-522).
static constexpr int EVAL_VARFLAG = 128;
static constexpr int EVAL_VARMASK = 127;
static constexpr int EVAL_CONSTFLAG = 64;
static constexpr int EVAL_CONSTMASK = 63;
static constexpr int EVAL_AND = 0;
static constexpr int EVAL_OR = 1;
static constexpr int EVAL_LTE = 2;
static constexpr int EVAL_LT = 3;
static constexpr int EVAL_EQ = 4;
static constexpr int EVAL_NEQ = 5;
static constexpr int EVAL_NOT = 6;

// Trigger-mask constants for tileEvents word1 (src/Enums.h:523-547).
static constexpr int EVFL_EXEC_ENTER = 1;
static constexpr int EVFL_EXEC_EXIT = 2;
static constexpr int EVFL_EXEC_TRIGGER = 4;
static constexpr int EVFL_EXEC_FACE = 8;
static constexpr int EVFL_EXEC_MASK = 15;
static constexpr int EVFL_MOD_SHIFT = 4;
static constexpr int EVFL_MOD_EAST = 16;
static constexpr int EVFL_MOD_NORTHEAST = 32;
static constexpr int EVFL_MOD_NORTH = 64;
static constexpr int EVFL_MOD_NORTHWEST = 128;
static constexpr int EVFL_MOD_WEST = 256;
static constexpr int EVFL_MOD_SOUTHWEST = 512;
static constexpr int EVFL_MOD_SOUTH = 1024;
static constexpr int EVFL_MOD_SOUTHEAST = 2048;
static constexpr int EVFL_MOD_DIR_MASK = 4080;
static constexpr int EVFL_MOD_MELEE = 4096;
static constexpr int EVFL_MOD_RANGED = 8192;
static constexpr int EVFL_MOD_EXPLOSION = 16384;
static constexpr int EVFL_MOD_ATTACK_MASK = 28672;
static constexpr int EVFL_FLAG_SHIFT = 16;
static constexpr int EVFL_FLAG_BLOCKINPUT = 65536;
static constexpr int EVFL_FLAG_EXIT_GOTO = 131072;
static constexpr int EVFL_FLAG_SKIP_TURN = 262144;
static constexpr int EVFL_DISABLE_SHIFT = 19;
static constexpr int EVFL_FLAG_DISABLE = 524288;

// EV_LERPSPRITE packed-field flags (src/Enums.h:285-290).
static constexpr int SCRIPT_LS_FLAG_ASYNC = 1;
static constexpr int SCRIPT_LS_FLAG_BLOCK = 2;
static constexpr int SCRIPT_LS_NO_TIME = 4;
static constexpr int SCRIPT_LS_DEFAULT_Z = 8;
static constexpr int SCRIPT_LS_FLAG_ASYNC_BLOCK = 3;

// Monster flags.
static constexpr int MFLAG_NONE = 0;
static constexpr int MFLAG_ABILITY = 0x1;
static constexpr int MFLAG_TRIGGERONACTIVATE = 0x2;
static constexpr int MFLAG_NOKILL = 0x4;
static constexpr int MFLAG_NOACTIVATE = 0x8;
static constexpr int MFLAG_NORESPAWN = 0x10;
static constexpr int MFLAG_NOTHINK = 0x20;
static constexpr int MFLAG_NORAISE = 0x40;
static constexpr int MFLAG_NOTRACK = 0x80;
static constexpr int MFLAG_WEAPON_ALT = 0x100;
static constexpr int MFLAG_SCALED = 0x200;
static constexpr int MFLAG_ATTACKING = 0x400;
static constexpr int MFLAG_LOOTED = 0x800;
static constexpr int MFLAG_KNOCKBACK = 0x1000;
static constexpr int MFLAG_NPC_CHAT = 0x2000;
static constexpr int MFLAG_LERP_SHADOW = 0x4000;

} // namespace Enums
} // namespace newcore

#endif // NEW_DOMAIN_ENUMS_H