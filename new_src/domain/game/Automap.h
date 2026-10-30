#ifndef NEW_DOMAIN_GAME_AUTOMAP_H
#define NEW_DOMAIN_GAME_AUTOMAP_H

#include <cstdint>

namespace newcore {

class EntityDb;
class MapData;
class Player;
struct AutomapModel;
struct ViewVisibility;

// Automap seen data on MapData (ADR 0036, docs/architecture/automap.md).
namespace Automap {

// Applies one rendered frame's marks. gate = legacy game->updateAutomap
// (Playing incl. combat, or Automap; src/Canvas.cpp:802,811,830,934).
void markSeen(MapData& map, const ViewVisibility& vis, bool gate);
// uncoverAutomap (src/AutomapController.cpp:35-63). Caller guarantees the gate.
void uncover(MapData& map, int destX, int destY);
// Game::givemap (src/Game.cpp:1016-1036).
void giveMap(MapData& map, int x, int y, int w, int h);

struct Blink { int64_t nextToggleMs = 0; bool on = false; };  // automapBlinkTime/State
// drawAutomap's geometry (src/AutomapController.cpp:65-324) into m.prims and the
// cursor fields; soft-key labels are left to the caller.
void buildModel(AutomapModel& m, const MapData& map, const EntityDb& db,
                const Player& p, int64_t timeMs, Blink& blink);

} // namespace Automap

} // namespace newcore

#endif // NEW_DOMAIN_GAME_AUTOMAP_H
