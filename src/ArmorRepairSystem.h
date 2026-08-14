#ifndef __ARMOR_REPAIR_SYSTEM_H__
#define __ARMOR_REPAIR_SYSTEM_H__

class ScriptThread;
class Entity;

class ArmorRepairSystem {
public:
    ArmorRepairSystem();

    bool startArmorRepair(ScriptThread* armorRepairThread);
    void endArmorRepair();
    void turnEntityIntoWaterSpout(Entity* entity);
};

#endif
