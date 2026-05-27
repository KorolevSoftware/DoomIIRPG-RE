#ifndef __MOVEMENT_CONTROLLER_H__
#define __MOVEMENT_CONTROLLER_H__

class MovementController {
public:
    MovementController();

    void setAnimFrames(int animFrames);
    void checkFacingEntity();
    void finishMovement();
    int flagForWeapon(int i);
    int flagForFacingDir(int i);
    void startRotation(bool b);
    void finishRotation(bool b);
    bool attemptMove(int n, int n2);
    bool pitchIsControlled(int n, int n2, int n3);
    void updateView();
};

#endif
