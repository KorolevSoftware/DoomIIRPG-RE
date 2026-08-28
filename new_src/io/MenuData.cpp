#include "io/MenuData.h"

#include <cstdio>

#include "io/DataReader.h"
#include "io/Localization.h"

namespace newcore {

bool MenuData::load(const std::vector<uint8_t>& data) {
	rows_.clear();
	items_.clear();
	warnedIds_.clear();
	itemIntCount_ = 0;
	bytesConsumed_ = 0;

	if (data.size() < 4) return false;

	DataReader reader(data);
	const int rowCount = reader.readShort();
	const int itemIntCount = reader.readShort();
	if (rowCount < 0 || itemIntCount < 0) return false;
	if (reader.remaining() < (size_t)(rowCount + itemIntCount) * 4) return false;

	// Row words come first, item words after them (ADR 0013 step 1).
	std::vector<int32_t> rowWords((size_t)rowCount);
	for (int32_t& w : rowWords) w = reader.readInt();

	items_.reserve((size_t)itemIntCount / 2);
	for (int i = 0; i + 1 < itemIntCount; i += 2) {
		// Item unpack, verbatim from MenuSystem::loadMenuItems
		// (src/MenuSystem.cpp:4022-4030).
		const uint32_t n7 = reader.readUInt();
		const uint32_t n8 = reader.readUInt();
		// Label and help ids are STRINGID(FILE_MENUSTRINGS = kTextIngame2, index).
		const int kLabelType = kTextIngame2 << Localization::kStringIdShift;
		MenuItemDef def;
		def.labelId = kLabelType | (int)((n7 >> 16) & 0xFFFF);
		def.flags = (int)(n7 & 0xFFFF);
		def.action = (int)((n8 >> 8) & 0xFF);
		def.param = (int)(n8 & 0xFF);
		def.helpId = kLabelType | (int)((n8 >> 16) & 0xFFFF);
		items_.push_back(def);
	}
	// A trailing odd item word is not part of any pair; consume it so the
	// byte count stays exact (the shipped file has an even count).
	if (itemIntCount % 2 != 0) reader.readInt();

	itemIntCount_ = itemIntCount;
	bytesConsumed_ = (int)reader.position();

	rows_.reserve((size_t)rowCount);
	int prevEnd = 0;
	for (int j = 0; j < rowCount; ++j) {
		const uint32_t w = (uint32_t)rowWords[(size_t)j];
		Row row;
		row.id = (int)(w & 0xFF);
		row.type = (int)((w >> 24) & 0xFF);
		const int end = (int)((w & 0xFFFF00) >> 8);   // in item ints
		row.first = prevEnd / 2;
		row.count = (end - prevEnd) / 2;
		prevEnd = end;
		// Rows 36..71 of the shipped file are zero padding (id 0, end 0), which
		// makes count negative once a real row precedes them. Such a row
		// describes no items.
		if (row.count < 0 || row.first < 0 ||
			(size_t)row.first + (size_t)row.count > items_.size()) {
			row.count = 0;
			row.first = 0;
		}
		rows_.push_back(row);
	}

	return true;
}

const MenuData::Row* MenuData::findRow(int menuId) const {
	// First match wins, like the loadMenuItems scan (src/MenuSystem.cpp:4009).
	for (const Row& row : rows_) {
		if (row.id == menuId) return &row;
	}
	return nullptr;
}

int MenuData::type(int menuId) const {
	const Row* row = findRow(menuId);
	return row ? row->type : -1;
}

const MenuItemDef* MenuData::items(int menuId, int& countOut) const {
	const Row* row = findRow(menuId);
	if (!row || row->count == 0) {
		countOut = 0;
		// The original calls Error(29) and dies (src/MenuSystem.cpp:4034); a
		// partial rewrite logs once and shows an empty screen instead.
		bool warned = false;
		for (int id : warnedIds_) {
			if (id == menuId) { warned = true; break; }
		}
		if (!warned) {
			warnedIds_.push_back(menuId);
			std::fprintf(stderr, "menus.bin: no items for menu id %d\n", menuId);
		}
		return nullptr;
	}
	countOut = row->count;
	return items_.data() + row->first;
}

bool MenuData::goldenCheck() const {
	// Decoded from the shipped archive (ADR 0013 "Consequences").
	static const int kExpectedRows = 72;
	static const int kExpectedItemInts = 426;
	static const int kExpectedBytes = 1996;
	static const int kRootActions[11] = { 1, 1, 4, 1, 9, 1, 1, 1, 1, 1, 1 };
	static const int kRootParams[11] = { 72, 46, 0, 49, 6, 30, 37, 35, 52, 53, 42 };
	static const int kAbsentIds[4] = { kMenuQuestLog, kMenuLoad, kMenuRestartLvl, kMenuSaveQuit };

	bool ok = true;
	if (rowCount() != kExpectedRows) {
		std::fprintf(stderr, "menus.bin GOLDEN FAIL: rows=%d expected %d\n",
			rowCount(), kExpectedRows);
		ok = false;
	}
	if (itemIntCount() != kExpectedItemInts) {
		std::fprintf(stderr, "menus.bin GOLDEN FAIL: item ints=%d expected %d\n",
			itemIntCount(), kExpectedItemInts);
		ok = false;
	}
	if (bytesConsumed() != kExpectedBytes) {
		std::fprintf(stderr, "menus.bin GOLDEN FAIL: bytes consumed=%d expected %d\n",
			bytesConsumed(), kExpectedBytes);
		ok = false;
	}
	if (type(kMenuInGame) != kMenuTypeList) {
		std::fprintf(stderr, "menus.bin GOLDEN FAIL: MENU_INGAME type=%d expected %d\n",
			type(kMenuInGame), kMenuTypeList);
		ok = false;
	}

	int rootCount = 0;
	const MenuItemDef* root = items(kMenuInGame, rootCount);
	if (!root || rootCount != 11) {
		std::fprintf(stderr, "menus.bin GOLDEN FAIL: MENU_INGAME items=%d expected 11\n",
			rootCount);
		ok = false;
	} else {
		for (int i = 0; i < 11; ++i) {
			if (root[i].action != kRootActions[i] || root[i].param != kRootParams[i]) {
				std::fprintf(stderr,
					"menus.bin GOLDEN FAIL: root item %d action=%d param=%d expected %d/%d\n",
					i, root[i].action, root[i].param, kRootActions[i], kRootParams[i]);
				ok = false;
			}
		}
	}

	// These screens are built in code, so a row appearing for them means the
	// format decode drifted (ADR 0013).
	for (int id : kAbsentIds) {
		if (findRow(id) != nullptr) {
			std::fprintf(stderr, "menus.bin GOLDEN FAIL: menu id %d should be absent\n", id);
			ok = false;
		}
	}
	return ok;
}

} // namespace newcore
