#include "core/AppContext.h"
#include "io/ZipArchive.h"
#include "platform/FileSystem.h"
#include "io/Tables.h"
#include "io/EntityDefs.h"
#include "io/Localization.h"
#include "io/Resources.h"
#include "io/Media.h"
#include "io/BmpImageLoader.h"
#include "domain/world/MapParser.h"
#include "platform/InputSystem.h"
#include "render/RenderBackend.h"
#include "render/gl/Texture.h"
#include "render/Graphics2D.h"
#include "render/gl/GlCommon.h"
#include "render/Camera3D.h"
#include "render/World3D.h"
#include "platform/Window.h"
#include "text/Font.h"
#include "text/Text.h"
#include "ui/Hud.h"
#include <SDL.h>
#include <fstream>

#include <cstdio>
#include <cstdlib>
#include <climits>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
	using namespace newcore;

	// Allow overriding the data archive via argv[1].
	const char* archiveName = (argc > 1) ? argv[1] : "Doom 2 RPG.ipa";

	AppContext& app = AppContext::instance();
	if (!app.initialize(archiveName)) {
		std::fprintf(stderr, "Startup failed.\n");
		return 1;
	}

	std::fprintf(stdout, "Data archive opened: %d entries\n", app.archive().entryCount());

	// --- Verify data loaders (Phase 0) ---
	std::vector<uint8_t> raw;

	Tables tables;
	if (app.readResource(Resources::kTables, raw)) {
		if (tables.load(raw)) {
			std::fprintf(stdout, "tables.bin OK: attacks=%zu wpinfo=%zu weapons=%zu stats=%zu masks=%zu keys=%zu osc=%zu levelnames=%zu\n",
				tables.monsterAttacks.size(), tables.weaponInfo.size(), tables.weaponData.size(),
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

	if (app.readResource(Resources::kEntities, raw)) {
		EntityDefs defs;
		if (defs.load(raw)) {
			std::fprintf(stdout, "entities.bin OK: %d defs\n", defs.count());
		} else {
			std::fprintf(stderr, "entities.bin FAILED to parse\n");
		}
	} else {
		std::fprintf(stderr, "entities.bin not found\n");
	}

	if (app.readResource(Resources::kStringsIndex, raw)) {
		Localization loc;
		if (loc.loadIndex(raw)) {
			for (int i = 0; i < 3; ++i) {
				std::vector<uint8_t> chunk;
				if (app.readResource(Resources::kStringsArray[i], chunk)) {
					loc.loadChunk(i, chunk);
				}
			}
			loc.loadTextType(0, kTextMain);
			loc.loadTextType(0, kTextIngame);
			std::fprintf(stdout, "strings.idx OK: '%s' | '%s'\n",
				loc.get(kTextMain, 0).c_str(), loc.get(kTextIngame, 0).c_str());
		} else {
			std::fprintf(stderr, "strings.idx FAILED\n");
		}
	} else {
		std::fprintf(stderr, "strings.idx not found\n");
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
	}

	Hud hud;
	hud.startup();

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

	// Camera placed at the map spawn (matches legacy Game::setSpawnPosition).
	// viewX = n*64+32 world units (tile grid), then render shifts by <<4+8.
	int spawnTile = g_map.spawnIndex & 0x1F;
	int spawnRow = g_map.spawnIndex >> 5;
	int camX = (spawnTile * 64 + 32) << 4;
	int camY = (spawnRow * 64 + 32) << 4;
	Camera3D camera;
	camera.setSinTable(tables.sinTable.data());
	{
		int hx = (spawnTile * 64 + 32) & 0x7FF;
		int hy = (spawnRow * 64 + 32) & 0x7FF;
		int h = (g_map.heightMap[(hy >> 6) * 32 + (hx >> 6)] << 3) + 36;
		int camZ = h << 4;
		int viewFov = 290;
		int viewAspect = (viewFov << 14) / ((480 << 14) / 320);
		int camYaw = (g_map.spawnDir << 7) & 0x3FF;
		camera.setView(camX + 8, camY + 8, camZ + 8, camYaw, 0, 0, viewFov, viewAspect);
	}

	// Render loop with a placeholder canvas draw (Phase 1 test).
		{
			using Clock = std::chrono::steady_clock;
			auto next = Clock::now();
			int frames = 0;
			double elapsed = 0;
			int camYaw = camera.viewYaw();
		bool running = true;
		app.input().setEventCallback([&running](const SDL_Event& e) {
			if (e.type == SDL_QUIT) running = false;
			if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
		});
		hud.showCenterMessage("Press ESC to quit", 0xAA000000, 2000);
		hud.setDemoMonster(100, 100);
		hud.setBubbleText("Hello Marine!", 0xFF002864, 3000);
		int weaponSelectTimer = 0;
		int touchCycle = 0;
		int touchTimer = 0;
		while (running) {
			app.input().poll(app.window());
			hud.update(15);
			// Cycle the demo weapon-select screen every 3 seconds.
			weaponSelectTimer += 15;
			if (weaponSelectTimer >= 3000) {
				weaponSelectTimer = 0;
				hud.setWeaponSelect(!hud.weaponSelect());
				touchCycle = 0;
				touchTimer = 0;
			}
			// Simulate a finger hovering over a weapon button in the select screen.
			if (hud.weaponSelect()) {
				touchTimer += 15;
				if (touchTimer >= 500) {
					touchTimer = 0;
					touchCycle = (touchCycle + 1) % 5;
				}
				hud.setTouchedWeapon(touchCycle);
			} else {
				hud.setTouchedWeapon(-1);
			}

// Simulate arrow-press highlighting (up/down/left/right in a loop).
		static int arrowTimer = 0;
		static int arrowCycle = 0;
		arrowTimer += 15;
		if (arrowTimer >= 500) {
			arrowTimer = 0;
			arrowCycle = (arrowCycle + 1) % 4;
		}
		hud.setArrowPressed(arrowCycle + 1);

		// Simulate taking damage periodically (red vignette + attack arrow).
		static int damageTimer = 0;
		static int damageDirCycle = 0;
		damageTimer += 15;
		if (damageTimer >= 4000) {
			damageTimer = 0;
			hud.setDamageDemo(damageDirCycle);
			damageDirCycle = (damageDirCycle + 1) % 8;
		}

		// Show an important message (red banner) once.
		static bool importantShown = false;
		if (!importantShown) {
			importantShown = true;
			hud.showImportantMessage("Important: objective updated!");
		}

		// Camera controls: WASD/arrows to move and turn (temporary exploration
		// camera; a real movement/script system comes later).
		{
			const uint8_t* keys = SDL_GetKeyboardState(nullptr);
			int turn = 0;
			if (keys[SDL_SCANCODE_LEFT]) turn += 8;
			if (keys[SDL_SCANCODE_RIGHT]) turn -= 8;
			int move = 0, strafe = 0;
			if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) move += 24;
			if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) move -= 24;
			if (keys[SDL_SCANCODE_D]) strafe -= 20;
			if (keys[SDL_SCANCODE_A]) strafe += 20;

			int yaw = (camYaw + turn) & 0x3FF;
			const int* st = tables.sinTable.data();
			int viewSin = st[yaw & 0x3FF];
			int viewCos = st[(yaw + 256) & 0x3FF];

			// Forward = +cos*K, -sin*K (mirror of legacy backward nudge).
			int dx = (viewCos * move - viewSin * strafe) >> 16;
			int dy = (-viewSin * move - viewCos * strafe) >> 16;
			camX += dx;
			camY += dy;
			camYaw = yaw;
			camera.setView(camX, camY, camera.viewZ(), yaw, 0, 0, 290,
				(290 << 14) / ((480 << 14) / 320));
		}

			RenderBackend& renderer = app.renderer();
			renderer.beginFrame(app.window());
			Graphics2D& g = renderer.g2d();
			// 3D perspective view of the decoded world (legacy GL-path port).
			if (world.initialized() && g_map.numNodes > 0) {
				world.drawSky(camera);
				world.drawBSP(g_map, g_media, camera);
			} else {
				g.fillRect(0, 0, 480, 320, 32, 32, 64);
			}
			// Cockpit, minimap and HUD are hidden for now so the raw 3D scene
			// can be inspected (re-enable when the world renderer is verified).
			constexpr bool kShowHud = false;
			if (kShowHud) {
				if (hud.imgCockpitOverlay().valid()) {
					hud.drawOverlay(g, 0, 42, 480);
				}
				if (!g_map.polygons.empty()) {
					// Compute world bounds (x,y) over all polygon verts.
					int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
					for (const auto& p : g_map.polygons) {
						for (const auto& v : p.verts) {
							if (v.x < minX) minX = v.x;
							if (v.x > maxX) maxX = v.x;
							if (v.y < minY) minY = v.y;
							if (v.y > maxY) maxY = v.y;
						}
					}
					int spanX = maxX - minX, spanY = maxY - minY;
					if (spanX > 0 && spanY > 0) {
						const int mapW = 400, mapH = 260;
						float sx = (float)mapW / spanX;
						float sy = (float)mapH / spanY;
						float scale = sx < sy ? sx : sy;
						int drawW = (int)(spanX * scale);
						int drawH = (int)(spanY * scale);
						const int mapX = (480 - drawW) / 2;
						const int mapY = (320 - drawH) / 2;
						g.fillRect(mapX - 4, mapY - 4, (int)(spanX * scale) + 8, (int)(spanY * scale) + 8, 0, 0, 0, 180);
						for (const auto& p : g_map.polygons) {
							uint8_t cr = 80, cg = 160, cb = 255;
							for (size_t i = 0; i < p.verts.size(); ++i) {
								const auto& v0 = p.verts[i];
								const auto& v1 = p.verts[(i + 1) % p.verts.size()];
								int x0 = mapX + (int)((v0.x - minX) * scale);
								int y0 = mapY + (int)((v0.y - minY) * scale);
								int x1 = mapX + (int)((v1.x - minX) * scale);
								int y1 = mapY + (int)((v1.y - minY) * scale);
								g.drawLine(x0, y0, x1, y1, cr, cg, cb, 200);
							}
						}
					}
				}
				if (font.valid()) {
					hud.draw(g, font, 480, 320);
				}
			}

			renderer.endFrame(app.window());

			// Throttle to ~66 fps.
			next += std::chrono::milliseconds(15);
			auto now = Clock::now();
			if (now < next) std::this_thread::sleep_until(next);
			else next = now;

			++frames;
			elapsed += std::chrono::duration<double>(Clock::now() - now).count();
			if (elapsed >= 1.0) {
				std::fprintf(stdout, "FPS: %d\n", frames);
				std::fflush(stdout);
				frames = 0;
				elapsed = 0;
			}
			if (frames > 6000) running = false;
		}
	}

	app.shutdown();
	return 0;
}
