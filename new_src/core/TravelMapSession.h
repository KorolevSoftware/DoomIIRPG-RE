#ifndef NEW_CORE_TRAVELMAPSESSION_H
#define NEW_CORE_TRAVELMAPSESSION_H

#include <cstdint>

#include "core/GameStates.h"
#include "ui/TravelMapModel.h"

namespace newcore {

class Tables;
class UiAssets;

// ST_TRAVELMAP logic (docs/original-code/travel-map.md): port of
// TravelMapManager minus the drawing, which goes through TravelMapModel.
// The dead star-field page (§7) is not ported.
class TravelMapSession {
public:
	struct Env {
		const int64_t* upTimeMs = nullptr;
		const UiAssets* art = nullptr;    // image sizes
		const Tables* tables = nullptr;   // levelNames
	};
	void init(const Env& env);
	// TravelMapManager::init (src/TravelMapManager.cpp:23-121) minus var 15,
	// which LoadingManager::enterTravelMap writes.
	void begin(int loadId, int lastId);
	// Advances the phase machine (§4) and fills the model. Called once per
	// rendered frame, like drawTravelMap (src/TravelMapManager.cpp:146-256).
	void buildModel(TravelMapModel& m);
	// handleInput (§5). True = the screen is finished (finishAndLoadLevel).
	bool handleAction(Action a);

	// TravelMapManager::yCoordOfSpaceShip (src/TravelMapManager.cpp:333-337):
	// integer parabola, truncating left to right.
	static constexpr int yCoordOfSpaceShip(int n) {
		const int k = (n * 11) / 15;
		return ((-851 * (k * k) / 110880 + 6115 * k / 22176 + 40) * 15) / 11;
	}

private:
	bool samePlanet() const;                            // newLevelSamePlanet (:258-263)
	void closeup(TravelMapModel& m, int level, bool twoStep) const;   // :265-280
	bool dottedLine(TravelMapModel& m, int time) const;               // :282-301
	bool marsToMoon(TravelMapModel& m, int n) const;                  // :309-331
	bool moonToEarth(TravelMapModel& m, int n, bool toMoon) const;    // :339-368
	bool earthToHell(TravelMapModel& m, int n, bool toEarth) const;   // :370-399
	void locatorBoxAndName(TravelMapModel& m, bool magnifier, int level) const;   // :401-430
	bool locatorLines(TravelMapModel& m, int n, bool same, bool twoStep) const;   // :523-585
	int levelName(int level) const;

	Env env_;
	int sv_[9] = {};                      // canvas->stateVars usage per §4
	int load_ = 1, last_ = 0;
	int totalPast_ = 0;                   // totalTMTimeInPastAnimations
	int crossTX_ = 0, crossTY_ = 0;       // target crosshair (§2.2)
	bool twoStepEarth_ = false;           // local `b` of init
};

} // namespace newcore

#endif // NEW_CORE_TRAVELMAPSESSION_H
