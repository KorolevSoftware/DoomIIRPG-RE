#ifndef __ZOOM_CONTROLLER_H__
#define __ZOOM_CONTROLLER_H__

class ZoomController
{
public:
	void initZoom();
	void zoomOut();
	bool handleZoomEvents(int key, int action);
	bool handleZoomEvents(int key, int action, bool force);
};

#endif
