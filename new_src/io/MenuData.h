#ifndef NEW_IO_MENUDATA_H
#define NEW_IO_MENUDATA_H

#include <cstdint>
#include <vector>

namespace newcore {

// Legacy Menus::menus ids (src/Menus.h:210-311); only the in-game subtree.
enum MenuId : int {
	kMenuInGame        = 29,
	kMenuStatus        = 30, kMenuPlayer = 31, kMenuLevel = 32, kMenuGrades = 33,
	kMenuOptions       = 35,
	kMenuHelp          = 37,
	kMenuHelpGeneral   = 38, kMenuHelpMove = 39, kMenuHelpAttack = 40,
	kMenuHelpSniper    = 41, kMenuHelpArmor = 43, kMenuHelpEffects = 44,
	kMenuHelpItems     = 45, kMenuHelpHacker = 59, kMenuHelpMatrixSkip = 60,
	kMenuHelpPowerUp   = 61,
	kMenuExit          = 42,
	kMenuQuestLog      = 46,
	kMenuLoad          = 49, kMenuLoadNoSave = 50,
	kMenuRestartLvl    = 52, kMenuSaveQuit = 53,
	kMenuSpecialExit   = 58,
	kMenuControls      = 62,
	kMenuItems         = 72, kMenuItemsWeapons = 73, kMenuItemsDrinks = 75,
};

// Menus::MENUTYPE_* (src/Menus.h:7-15)
enum MenuType : int {
	kMenuTypeList = 1, kMenuTypeHelp = 5, kMenuTypeVCenter = 6,
	kMenuTypeNotebook = 7,
};

// Menus::ITEM_* (src/Menus.h:16-34); only the bits the in-game tree uses.
enum MenuItemFlag : int {
	kItemNormal = 0x0000, kItemNoSelect = 0x0001, kItemNoDehyphenate = 0x0002, kItemDisabled = 0x0004,
	kItemAlignCenter = 0x0008, kItemShowDetails = 0x0020, kItemDivider = 0x0040,
	kItemChecked = 0x0400, kItemHidden = 0x8000,
};

// Menus::ACTION_* (src/Menus.h:135-167); only what the in-game tree uses.
enum MenuAction : int {
	kActionNone = 0, kActionGoto = 1, kActionBack = 2, kActionLoad = 3,
	kActionSave = 4, kActionBackToMain = 5, kActionChangeState = 9,
	kActionRestartLevel = 12, kActionSaveQuit = 13, kActionShowDetails = 16,
	kActionUseItemWeapon = 18, kActionConfirmUse = 24, kActionSaveExit = 25,
	kActionReturnToPlayer = 33,
};

// One menu row. The code-built screens (QUESTLOG 46, LOAD 49, RESTARTLVL 52,
// SAVEQUIT 53, every HELP leaf; ADR 0013) fill the same struct by hand, so the
// view cannot tell a parsed row from a generated one.
struct MenuItemDef {
	int labelId = 0;    // (type<<10)|index, type 3; kEmptyTextId == none
	int flags   = 0;
	int action  = 0;
	int param   = 0;
	int helpId  = 0;    // kEmptyTextId == none
};

// EMPTY_TEXT = STRINGID(3, 0) (src/MenuSystem.h:38), what paint compares
// against (src/MenuSystem.cpp:930).
constexpr int kEmptyTextId = 3072;

// Parsed copy of menus.bin (ADR 0013). Value semantics, movable.
class MenuData {
public:
	bool load(const std::vector<uint8_t>& data);

	// Row type, or -1 when the id has no row in the file.
	int type(int menuId) const;
	// Item span for a menu id; empty when absent (logs once per id).
	const MenuItemDef* items(int menuId, int& countOut) const;

	int rowCount() const { return (int)rows_.size(); }
	int itemIntCount() const { return itemIntCount_; }   // 426 in the shipped file
	int itemDefCount() const { return (int)items_.size(); }
	int bytesConsumed() const { return bytesConsumed_; }

	// Boot-time format regression check against the shipped file (ADR 0013:
	// 72 rows, 426 item ints, 1996 bytes, root id 29 type 1 with 11 known
	// items, ids 46/49/52/53 absent). Logs every mismatch to stderr.
	bool goldenCheck() const;

private:
	struct Row { int id, type, first, count; };

	const Row* findRow(int menuId) const;

	std::vector<Row> rows_;
	std::vector<MenuItemDef> items_;
	int itemIntCount_ = 0;
	int bytesConsumed_ = 0;
	mutable std::vector<int> warnedIds_;
};

} // namespace newcore

#endif // NEW_IO_MENUDATA_H
