#ifndef __LOADING_MANAGER_H__
#define __LOADING_MANAGER_H__

class Graphics;

class LoadingManager {
public:
    LoadingManager();

    void loadRuntimeData();
    void freeRuntimeData();
    void loadState(int loadType, short n, short n2);
    void saveState(int saveType, short n, short n2);
    void loadMap(int loadMapID, bool b, bool tm_NewGame);
    void loadMiniGameImages();
    bool loadMedia();
    void unloadMedia();
    int getRecentLoadType();

private:
    void registerMapMedia(int mediaID);
    void finalizeMapMedia();
    bool loadMapData(int mapNameID);
};

#endif
