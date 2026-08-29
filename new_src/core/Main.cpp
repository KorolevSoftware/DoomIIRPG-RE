#include "core/AppContext.h"
#include "core/GameContext.h"

#include "domain/game/DialogSystem.h"
#include "domain/game/Game.h"
#include "domain/game/Player.h"
#include "domain/game/ScriptVM.h"
#include "domain/world/MapParser.h"
#include "io/BmpImageLoader.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Media.h"
#include "io/MenuData.h"
#include "io/Resources.h"
#include "io/Tables.h"
#include "io/ZipArchive.h"
#include "render/RenderBackend.h"
#include "render/World3D.h"
#include "text/Font.h"
#include "text/Text.h"
#include "ui/Hud.h"
#include "ui/Ui.h"
#include "ui/UiAssets.h"
#include "ui/UiState.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>

// Composition root (spec 2026-08-23-phase5-skeleton §2): eager data/media
// init with loader smoke logs, subsystem construction, then everything is
// handed to the GameContext state machine driven by AppContext::run().
// Spawn/camera placement live inside the machine (Loading tick / render),
// input arrives as queued actions via GameLoop.
int main(int argc, char* argv[]) {
	using namespace newcore;

	// Combat rolls use std::rand like the RE port (src/App.cpp:506-512).
	std::srand((unsigned)std::time(nullptr));

	// Allow overriding the data archive via argv[1].
	const char* archiveName = (argc > 1) ? argv[1] : "Doom 2 RPG.ipa";

	AppContext& app = AppContext::instance();
	if (!app.initialize(archiveName)) {
		std::fprintf(stderr, "Startup failed.\n");
		return 1;
	}

	std::fprintf(stdout, "Data archive opened: %d entries\n", app.archive().entryCount());

	std::vector<uint8_t> raw;

	Tables tables;
	if (app.readResource(Resources::kTables, raw)) {
		if (tables.load(raw)) {
			// wpinfo/weapons are now row counts, not byte counts.
			std::fprintf(stdout, "tables.bin OK: attacks=%zu wpinfoRows=%zu weaponRows=%zu stats=%zu masks=%zu keys=%zu osc=%zu levelnames=%zu\n",
				tables.monsterAttacks.size(), tables.weaponPoses.size(), tables.weaponDefs.size(),
				tables.monsterStats.size(), tables.combatMasks.size(), tables.keysNumeric.size(),
				tables.oscCycle.size(), tables.levelNames.size());
			std::fprintf(stdout, "  monsterColors=%zu sin=%zu drinks=%zu weakness=%zu movieFx=%zu sounds=%zu\n",
				tables.monsterColors.size(), tables.sinTable.size(), tables.energyDrinkData.size(),
				tables.monsterWeakness.size(), tables.movieEffects.size(), tables.monsterSounds.size());
			std::fprintf(stdout, "  skyPalA=%zu skyTexA=%zu skyPalB=%zu skyTexB=%zu\n",
				tables.skyPaletteA.size(), tables.skyTexelA.size(),
				tables.skyPaletteB.size(), tables.skyTexelB.size());
		} else {
			std::fprintf(stderr, "tables.bin FAILED to parse\n");
		}
	} else {
		std::fprintf(stderr, "tables.bin not found\n");
	}

	EntityDefs g_entityDefs;
	if (app.readResource(Resources::kEntities, raw)) {
		if (g_entityDefs.load(raw)) {
			std::fprintf(stdout, "entities.bin OK: %d defs\n", g_entityDefs.count());
		} else {
			std::fprintf(stderr, "entities.bin FAILED to parse\n");
		}
	} else {
		std::fprintf(stderr, "entities.bin not found\n");
	}

	// Function scope: the VM and GameContext read strings after boot.
	Localization loc;
	if (app.readResource(Resources::kStringsIndex, raw)) {
		if (loc.loadIndex(raw)) {
			for (int i = 0; i < 3; ++i) {
				std::vector<uint8_t> chunk;
				if (app.readResource(Resources::kStringsArray[i], chunk)) {
					loc.loadChunk(i, chunk);
				}
			}
			loc.loadTextType(0, kTextMain);
			loc.loadTextType(0, kTextIngame);
			// Menu labels/help fields live in FILE_MENUSTRINGS (kTextIngame2),
			// HELP page bodies in FILE_FILESTRINGS (kTextHelp)
			// (src/MenuStrings.h:20-23; spec 2026-08-28-menu §3).
			loc.loadTextType(0, kTextIngame2);
			loc.loadTextType(0, kTextHelp);
			// Script EV_MESSAGE strings live in the per-map text type:
			// loadMapStringID = kTextMap + (mapNameID - 1), loaded at map load
			// (src/LoadingManager.cpp:310-311); boot is always map00 -> kTextMap.
			const int currentMapId = 0;
			loc.loadTextType(0, kTextMap + currentMapId);
			std::fprintf(stdout, "strings.idx OK: '%s' | '%s' | '%s'\n",
				loc.get(kTextMain, 0).c_str(), loc.get(kTextIngame, 0).c_str(),
				loc.get(kTextMap, 0).c_str());
		} else {
			std::fprintf(stderr, "strings.idx FAILED\n");
		}
	} else {
		std::fprintf(stderr, "strings.idx not found\n");
	}

	// In-game menu tree (ADR 0013): the row/item data is parsed, not
	// hardcoded. Lives here next to Tables so it outlives the GameContext.
	MenuData menus;
	if (app.readResource(Resources::kMenus, raw)) {
		if (menus.load(raw)) {
			const bool golden = menus.goldenCheck();
			int rootCount = 0;
			const MenuItemDef* root = menus.items(kMenuInGame, rootCount);
			std::fprintf(stdout, "menus.bin %s: rows=%d items=%d bytes=%d/%zu; MENU_INGAME type=%d items=%d\n",
				golden ? "OK" : "GOLDEN MISMATCH", menus.rowCount(), menus.itemIntCount(),
				menus.bytesConsumed(), raw.size(), menus.type(kMenuInGame), rootCount);
			for (int i = 0; i < rootCount; ++i) {
				Text label;
				label.append(loc.get(loc.typeOf(root[i].labelId), loc.indexOf(root[i].labelId)));
				label.dehyphenate();
				std::fprintf(stdout, "  root[%2d] action=%2d target=%2d flags=0x%04X help=%d '%s'\n",
					i, root[i].action, root[i].param, root[i].flags,
					loc.indexOf(root[i].helpId), label.c_str());
			}
		} else {
			std::fprintf(stderr, "menus.bin FAILED to parse\n");
		}
	} else {
		std::fprintf(stderr, "menus.bin not found\n");
	}

	// --- Verify map00 media loading ---
	MediaLoader g_media;
	{
		MediaMappings maps;
		if (app.readResource("newMappings.bin", raw) && maps.load(raw)) {
			std::fprintf(stdout, "newMappings.bin OK: mappings=%zu dims=%zu bounds=%zu\n",
				maps.mappings.size(), maps.dimensions.size(), maps.bounds.size());

			// Read map00.bin media ids (header: version byte + int32 + ushort + byte +
			// byte + byte + byte + ushort + ushort + ushort + ushort + ushort + short +
			// short + short + byte + ushort + 6 shorts + 0xDEADBEEF marker).
			std::vector<uint8_t> mapData;
			if (app.readResource("map00.bin", mapData)) {
				size_t off = 42;
				if (mapData.size() >= off + 4 &&
					(uint8_t)mapData[off] == 0xEF && (uint8_t)mapData[off+1] == 0xBE &&
					(uint8_t)mapData[off+2] == 0xAD && (uint8_t)mapData[off+3] == 0xDE) {
					off += 4;
					uint16_t mediaCount = mapData[off] | (mapData[off+1] << 8);
					off += 2;
					std::vector<uint16_t> mediaIds(mediaCount);
					for (uint16_t& id : mediaIds)
						id = mapData[off] | (mapData[off+1] << 8), off += 2;

					if (g_media.loadMappings(raw)) {
						g_media.registerMedia(mediaIds);
						g_media.finalize([&app](const std::string& name) {
							std::vector<uint8_t> d;
							app.readResource(name, d);
							return d;
						});
						std::fprintf(stdout, "map00 media OK: %u media ids -> %d palettes, %d texels\n",
							mediaCount, g_media.paletteCount(), g_media.texelCount());
					}
				}
			}
		} else {
			std::fprintf(stderr, "newMappings.bin FAILED\n");
		}
	}

	// --- Parse map00.bin geometry ---
	MapData g_map;
	{
		std::vector<uint8_t> mapData;
		if (app.readResource("map00.bin", mapData)) {
			if (MapParser::parse(mapData, g_map)) {
				std::fprintf(stdout, "map00.bin parse OK: version=%d spawn=%d nodes=%d lines=%d normals=%d polys=%d sprites=%d/%d tileEvents=%d bytecode=%d mayaCams=%d\n",
					g_map.version, g_map.spawnIndex, g_map.numNodes, g_map.numLines, g_map.numNormals,
					g_map.dataSizePolys, g_map.numNormalSprites, g_map.numZSprites,
					g_map.numTileEvents, g_map.mapByteCodeSize, g_map.totalMayaCameras);
				std::fprintf(stdout, "  media=%d heightMap=1024 flags(events)=%d mayaKeys=%d mayaTweens=%d decodedPolys=%zu\n",
					g_map.mediaCount, g_map.numTileEvents, g_map.totalMayaCameraKeys, g_map.totalMayaTweens,
					g_map.polygons.size());
			} else {
				std::fprintf(stderr, "map00.bin FAILED to parse\n");
			}
		} else {
			std::fprintf(stderr, "map00.bin not found\n");
		}
	}

	// --- Verify BMP loading ---
	std::vector<uint8_t> bmp;
	Font font;
	if (app.readResource("Font.bmp", bmp)) {
		BmpImageLoader loader;
		ImagePtr img = loader.load(bmp, true);
		if (img) {
			std::fprintf(stdout, "Font.bmp OK: %dx%d bpp=%d pal=%zu\n",
				img->width(), img->height(), img->depth(), img->palette().size());
			font.upload(img->indices(), img->width(), img->height(), img->palette());
		} else {
			std::fprintf(stderr, "Font.bmp FAILED\n");
		}
	} else {
		std::fprintf(stderr, "Font.bmp not found\n");
	}

	// DEBUG: distribution of texture ids across decoded polygons.
	{
		std::map<int, int> texHist;
		for (const auto& p : g_map.polygons) texHist[p.textureId]++;
		fprintf(stderr, "DEBUG textureId histogram: %zu unique\n", texHist.size());
		fflush(stderr);
	}

	// UI layer (spec 2026-08-27-ui-layer §2, §8): UiAssets owns every sheet
	// and outlives every view; the reader functor keeps ui/ free of
	// core/AppContext.h. UiState holds the only retained UI state, Ui is the
	// per-frame façade, handed to GameContext which drives the views (GROUP 4:
	// the HUD bottom bar).
	UiAssets uiAssets;
	uiAssets.load([&app](const char* name, std::vector<uint8_t>& out) {
		return app.readResource(name, out);
	});

	UiState uiState;
	Ui ui;
	ui.init({ &app.renderer().g2d(), &font, &uiAssets, &uiState });

	Hud hud;
	hud.setAssets(&uiAssets);

	// 3D world renderer: builds GPU textures for the map's media and draws
	// the decoded polygons with a perspective camera (legacy GL-path port).
	World3D world;
	world.initialize();
	world.uploadMapTextures(g_map, g_media);

	// Sky: legacy chooses palette/texel by skyIndex = ((mapNameID-1)/5%2)*2.
	// map00 -> skyIndex 0 -> tables A (palette table 16, texel table 17).
	if (!tables.skyTexelA.empty() && !tables.skyPaletteA.empty()) {
		world.uploadSky(tables.skyTexelA, tables.skyPaletteA);
	} else if (!tables.skyTexelB.empty() && !tables.skyPaletteB.empty()) {
		world.uploadSky(tables.skyTexelB, tables.skyPaletteB);
	}
	// Fog: disabled for now. Legacy takes fog values from the map/save
	// (world.setFog(ARGB, fogMin, fogRange); alpha==0 disables). Re-enable
	// with map-provided values when the loading pipeline supplies them.
	// world.setFog(0xFF1A2A1A, 500, 900);

	// Phase 4: player + world game state (doors, items). Spawn placement
	// happens in the Loading tick (legacy Game::spawnPlayer port).
	Player player;
	player.reset();

	Game game;
	// Entity load happens in Loading phase 1 (GameContext::tickLoading,
	// src/LoadingManager.cpp:658) — do not duplicate it here.

	// Game-state machine + tileEvents VM + dialogs: non-owning wiring; the
	// context is declared after every system it references so it dies first
	// (spec §2).
	ScriptVM vm;
	GameContext ctx;
	DialogSystem dialogs;
	vm.init({                  // Env: map, defs, game, player, loc, hud, ctx, dialogs, gameTime
		&g_map, &g_entityDefs, &game, &player, &loc, &hud, &ctx, &dialogs, &ctx.gameTime });
	game.setVM(&vm);
	game.combat.init({         // Env: game, player, hud, loc, tables, map, gameTime (spec §2.2)
		&game, &player, &hud, &loc, &tables, &g_map, &ctx.gameTime });
	game.setXPSystems(&player, &loc, &hud);   // kill-XP state/presentation bridges
	ctx.init({                 // Init: map, defs, tables, loc, font, media, game, player, vm, hud, world, dialogs, ui, menus
		&g_map, &g_entityDefs, &tables, &loc, &font, &g_media,
		&game, &player, &vm, &hud, &world, &dialogs, &ui, &menus });
	dialogs.init({             // Env: ctx, vm, game, loc, hud, font, tables
		&ctx, &vm, &game, &loc, &hud, &font, &tables });

	// Revived frame path: AppContext::run -> GameLoop::run drives the
	// fixed-step ticks, queued-input actions and per-frame rendering.
	app.setGameContext(&ctx);
	app.run();

	app.shutdown();
	return 0;
}
