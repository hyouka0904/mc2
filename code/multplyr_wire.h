// SPDX-License-Identifier: GPL-3.0-only
// LAN multiplayer wire formats that are not MCMSG_* classes: the discovery
// beacon and the in-mission sync records. Fixed-size fields, packed, little
// endian on every supported platform.
//
// Changing any structure here or in multplyr.h changes the wire format:
// bump MC2_NET_PROTOCOL_STAMP so older builds refuse to join instead of
// desyncing. unit_netlayout (code/netlayout.cpp) fails until you do.
#pragma once

#include "multplyr.h"

#include <cstddef>

#define MC2_NET_PROTOCOL_STAMP	"MC2NET-4"		// bump when the wire format changes

#pragma pack(1)
// Discovery answer payload (see GosNetSession::setDiscoveryPayload).
typedef struct _NetBeacon {
	char			magic[4];				// "MC2S"
	char			protocol[15];
	char			name[MAXLEN_SESSION_NAME];
	char			map[MAXLEN_MAP_NAME];
	unsigned char	maxPlayers;
	unsigned char	numPlayers;
	unsigned char	locked;
	unsigned char	inProgress;
	unsigned int	dataHash;
	unsigned int	sessionId;				// random per hosted session
	// Added in 0.2; hosts before that send the beacon without it.
	char			gameVersion[24];
} NetBeacon;
#define NET_BEACON_MIN_SIZE		offsetof(NetBeacon, gameVersion)

//***************************************************************************
// In-mission messages (host <-> clients)
//***************************************************************************

// MCMSG_MOVER_UPDATE: host -> all. Header, then numEntries NetMoverEntry.
typedef struct _NetMoverUpdate {
	unsigned char	type;
	unsigned short	updateId;
	unsigned char	numEntries;
	float			missionTime;
	int				teamScore[MAX_TEAMS];
	int				playerScore[MAX_MC_PLAYERS];
	short			kills[MAX_MC_PLAYERS];
	short			losses[MAX_MC_PLAYERS];
	int				resourcePoints[MAX_MC_PLAYERS];
	unsigned char	leftSession;		// bit per commander
} NetMoverUpdate;

typedef struct _NetMoverEntry {
	unsigned char	index;				// net roster index
	unsigned char	flags;				// 1 = status chunk, 2 = move chunk
	unsigned int	status;
	unsigned int	move;
} NetMoverEntry;

// MCMSG_MOVER_WEAPONFIRE_UPDATE: host -> all. Per mover: index, counts,
// then fire chunks (u32), critical hit chunks (u8), radio chunks (u8).
typedef struct _NetMoverEventsHeader {
	unsigned char	type;
	unsigned char	numMovers;
} NetMoverEventsHeader;

typedef struct _NetMoverEvents {
	unsigned char	index;
	unsigned char	numFire;
	unsigned char	numCrit;
	unsigned char	numRadio;
} NetMoverEvents;

// MCMSG_TURRET_UPDATE: host -> all. Turret targets (part ids, 0 = none).
typedef struct _NetTurretUpdate {
	unsigned char	type;
	unsigned char	numEntries;
} NetTurretUpdate;

typedef struct _NetTurretEntry {
	unsigned char	index;
	int				targetPartId;
} NetTurretEntry;

// MCMSG_TURRET_WEAPONFIRE_UPDATE: host -> all. Per turret: index, count,
// fire chunks.
typedef struct _NetTurretFire {
	unsigned char	index;
	unsigned char	numFire;
} NetTurretFire;

// MCMSG_WEAPONHIT_UPDATE / MCMSG_WORLD_UPDATE: host -> all.
typedef struct _NetChunkList {
	unsigned char	type;
	unsigned short	count;
} NetChunkList;

typedef struct _NetPause {
	unsigned char	type;			// MCMSG_PAUSE
	unsigned char	paused;
	char			commanderID;	// who asked
	unsigned char	refused;		// host: no pauses left
} NetPause;
#pragma pack()
