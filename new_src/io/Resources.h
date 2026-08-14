#ifndef NEW_IO_RESOURCES_H
#define NEW_IO_RESOURCES_H

namespace newcore {

// Static names of game data files inside the archive (Packages/ prefix is
// applied by AppContext::readResource).
struct Resources {
	static constexpr const char* kLogo = "logo.bmp";
	static constexpr const char* kLogo2 = "logo2.bmp";
	static constexpr const char* kStringsIndex = "strings.idx";
	static constexpr const char* kTables = "tables.bin";
	static constexpr const char* kStringsArray[3] = { "strings00.bin", "strings01.bin", "strings02.bin" };
	static constexpr const char* kEntities = "entities.bin";
	static constexpr const char* kMenus = "menus.bin";
	static constexpr const char* kNewMappings = "newMappings.bin";
	static constexpr const char* kMapFiles[10] = {
		"map00.bin", "map01.bin", "map02.bin", "map03.bin", "map04.bin",
		"map05.bin", "map06.bin", "map07.bin", "map08.bin", "map09.bin" };
	static constexpr const char* kNewPalettes = "newPalettes.bin";
	static constexpr const char* kNewTexels[39] = {
		"newTexels000.bin", "newTexels001.bin", "newTexels002.bin", "newTexels003.bin",
		"newTexels004.bin", "newTexels005.bin", "newTexels006.bin", "newTexels007.bin",
		"newTexels008.bin", "newTexels009.bin", "newTexels010.bin", "newTexels011.bin",
		"newTexels012.bin", "newTexels013.bin", "newTexels014.bin", "newTexels015.bin",
		"newTexels016.bin", "newTexels017.bin", "newTexels018.bin", "newTexels019.bin",
		"newTexels020.bin", "newTexels021.bin", "newTexels022.bin", "newTexels023.bin",
		"newTexels024.bin", "newTexels025.bin", "newTexels026.bin", "newTexels027.bin",
		"newTexels028.bin", "newTexels029.bin", "newTexels030.bin", "newTexels031.bin",
		"newTexels032.bin", "newTexels033.bin", "newTexels034.bin", "newTexels035.bin",
		"newTexels036.bin", "newTexels037.bin", "newTexels038.bin" };
};

} // namespace newcore

#endif // NEW_IO_RESOURCES_H