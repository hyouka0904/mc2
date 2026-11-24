// SPDX-License-Identifier: GPL-3.0-only
#include "savelayout.h"

#include "mclib.h"
#include "objmgr.h"
#include "gameobj.h"
#include "mover.h"
#include "mech.h"
#include "gvehicl.h"
#include "terrobj.h"
#include "bldng.h"
#include "turret.h"
#include "gate.h"
#include "artlry.h"
#include "carnage.h"
#include "weaponbolt.h"
#include "team.h"
#include "comndr.h"
#include "warrior.h"

uint32_t SaveLayoutHash()
{
    // Order matters only in that it must not change; add new structures at
    // the end.
    const uint32_t sizes[] = {
        sizeof(ObjectManagerData), sizeof(GameObjectData), sizeof(MoverData),
        sizeof(MechData), sizeof(GroundVehicleData), sizeof(TerrainObjectData),
        sizeof(BuildingData), sizeof(TurretData), sizeof(GateData),
        sizeof(ArtilleryData), sizeof(CarnageData), sizeof(WeaponBoltData),
        sizeof(TeamData), sizeof(TeamStaticData), sizeof(CommanderData),
        sizeof(StaticCommanderData), sizeof(MechWarriorData),
        sizeof(StaticMechWarriorData),
    };
    uint32_t h = 2166136261u;
    for (uint32_t s : sizes) {
        for (int i = 0; i < 4; i++) {
            h ^= (s >> (8 * i)) & 0xff;
            h *= 16777619u;
        }
    }
    return h;
}

bool SaveLayoutIsLegacy(uint32_t hash)
{
    // The layouts of 0.1.0 (the last release without the fingerprint) on
    // each platform CI builds. Never edit these: they describe old files.
    static const uint32_t legacy[] = {
        0x98d5de0eu, // Linux x86_64
    };
    for (uint32_t l : legacy)
        if (l == hash)
            return true;
    return false;
}
