#ifndef NEW_DOMAIN_GAME_PLAYERSTATE_H
#define NEW_DOMAIN_GAME_PLAYERSTATE_H

namespace newcore {

class DataReader;
class DataWriter;
class Player;

// Player file payload (FULLPLAYER / BRIEFPLAYER, ADR 0037): port of
// Game::savePlayerState / loadPlayerState and Player::saveState / loadState.
namespace PlayerState {

// Game::savePlayerState header (src/Game.cpp:2155-2171).
struct Header {
	int mapId = 0;
	int viewX = 0, viewY = 0, viewAngle = 0, viewPitch = 0;
	int prevX = 0, prevY = 0, saveX = 0, saveY = 0, saveZ = 0;
	int saveAngle = 0, savePitch = 0;
};

// Header + Player::saveState body (src/Player.cpp:1351-1420).
void save(const Player& p, const Header& h, DataWriter& w);
// False = truncated / trailing bytes / out-of-range field; p and h untouched then.
bool load(DataReader& r, Player& p, Header& h);
#ifndef NDEBUG
void selfCheck();
#endif

} // namespace PlayerState

} // namespace newcore

#endif // NEW_DOMAIN_GAME_PLAYERSTATE_H
