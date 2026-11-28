// SPDX-License-Identifier: GPL-3.0-only
#include "netlayout.h"

#include "multplyr_wire.h"

uint32_t NetLayoutHash()
{
    // Order matters only in that it must not change; add new structures at
    // the end.
    const uint32_t sizes[] = {
        sizeof(MC2Session),
        sizeof(MC2SessionData),
        sizeof(TeamInfo),
        sizeof(MC2PlayerSlot),
        sizeof(MC2Player),
        sizeof(MissionSettings),
        sizeof(CompressedMech),
        sizeof(NetWorldEvent),
        sizeof(NetBeacon),
        sizeof(NetMoverUpdate),
        sizeof(NetMoverEntry),
        sizeof(NetMoverEventsHeader),
        sizeof(NetMoverEvents),
        sizeof(NetTurretUpdate),
        sizeof(NetTurretEntry),
        sizeof(NetTurretFire),
        sizeof(NetChunkList),
        sizeof(NetPause),
        sizeof(MCMSG_Generic),
        sizeof(MCMSG_PlayerCID),
        sizeof(MCMSG_PlayerUpdate),
        sizeof(MCMSG_MissionSettingsUpdate),
        sizeof(MCMSG_MissionSetup),
        sizeof(MCMSG_PlayerInfo),
        sizeof(MCMSG_Chat),
        sizeof(MCMSG_PlayerCheckIn),
        sizeof(MCMSG_PlayerSetup),
        sizeof(MCMSG_PlayerInsignia),
        sizeof(MCMSG_StartMission),
        sizeof(MCMSG_EndMission),
        sizeof(MCMSG_LeaveSession),
        sizeof(MCMSG_PlayerOrder),
        sizeof(MCMSG_HoldPosition),
        sizeof(MCMSG_PlayerMoverGroup),
        sizeof(MCMSG_PlayerArtillery),
        sizeof(MCMSG_MoverUpdate),
        sizeof(MCMSG_TurretUpdate),
        sizeof(MCMSG_MoverWeaponFireUpdate),
        sizeof(MCMSG_TurretWeaponFireUpdate),
        sizeof(MCMSG_MoverCriticalUpdate),
        sizeof(MCMSG_WeaponHitUpdate),
        sizeof(MCMSG_WorldUpdate),
        sizeof(MCMSG_Reinforcement),
        sizeof(MCMSG_NewServer),
        sizeof(MCMSG_FileReport),
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

const char* NetLayoutStamp()
{
    return MC2_NET_PROTOCOL_STAMP;
}
