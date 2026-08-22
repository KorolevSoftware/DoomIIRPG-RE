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

// Trace content masks.
static constexpr int CONTENTS_ANY = -1;
static constexpr int CONTENTS_PICKUP = 64;
static constexpr int CONTENTS_INTERACTIVE = 1068;
static constexpr int CONTENTS_PLAYERSOLID = 13501;
static constexpr int CONTENTS_MONSTERSOLID = 15535;
static constexpr int CONTENTS_WEAPONSOLID = 13997;
static constexpr int CONTENTS_VIEWSOLID = 5293;
static constexpr int CONTENTS_MONSTERWPSOLID = 5295;
static constexpr int CONTENTS_WORLD = 1;
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

// Door subtypes.
static constexpr int DOOR_LOCKED = 1;
static constexpr int DOOR_UNLOCKED = 2;

// Door tile range (271-281).
static constexpr int TILENUM_FIRST_DOOR = 271;
static constexpr int TILENUM_LAST_DOOR = 281;

// Bosses.
static constexpr int FIRSTBOSS = 12;
static constexpr int LASTBOSS = 16;

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