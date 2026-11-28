//***************************************************************************
//
//	multplyr_sync.cpp - In-mission part of the MultiPlayer class: mover and
//	turret rosters, world / weapon-hit chunks, player orders and the host's
//	state updates to the clients. Sessions and the lobby are in multplyr.cpp.
//
//---------------------------------------------------------------------------//
// Copyright (C) Microsoft Corporation. All rights reserved.                 //
//===========================================================================//

#ifndef MCLIB_H
#include"mclib.h"
#endif

#ifndef LINUX_BUILD
#include"crtdbg.h"
#endif

#ifndef MULTPLYR_H
#include"multplyr.h"
#endif

#ifndef TEAM_H
#include"team.h"
#endif

#ifndef MOVER_H
#include"mover.h"
#endif

#ifndef TURRET_H
#include"turret.h"
#endif

#ifndef MISSION_H
#include"mission.h"
#endif

#ifndef MECH_H
#include"mech.h"
#endif

#ifndef GAMEOBJ_H
#include"gameobj.h"
#endif

#ifndef OBJMGR_H
#include"objmgr.h"
#endif

#ifndef LOGISTICSDATA_H
#include"logisticsdata.h"
#endif

#ifndef BLDNG_H
#include"bldng.h"
#endif

#ifndef TERROBJ_H
#include"terrobj.h"
#endif

#ifndef MOVE_H
#include"move.h"
#endif

#ifndef GROUP_H
#include"group.h"
#endif

#ifndef COMNDR_H
#include"comndr.h"
#endif

#ifndef CARNAGE_H
#include"carnage.h"
#endif

#ifndef ARTLRY_H
#include"artlry.h"
#endif

#ifndef CONTROLGUI_H
#include"controlgui.h"
#endif

#ifndef LOGISTICS_H
#include"logistics.h"
#endif

#include"missionbegin.h"

#ifndef PREFS_H
#include"prefs.h"
#endif

#ifndef MISSIONGUI_H
#include"missiongui.h"
#endif

#ifndef GAMELOG_H
#include"gamelog.h"
#endif


#include"mpparameterscreen.h"

//sebi: commented include
//#ifndef VERSION_H
//#include"version.h"
//#endif

#include "../res/resource.h"

#include "net/gos_net.h"
#include "gamesound.h"

#include <chrono>

extern UserHeapPtr systemHeap;
extern MoverPtr BringInReinforcement (long vehicleID, long rosterIndex, long commanderID, Stuff::Vector3D pos, bool exists);
extern bool FromMP;
extern long CurMultiplayCode;
extern long CurMultiplayParam;
extern float scenarioTime;

static float hostMissionTime = 0.0f;

static double SyncNow (void) {
	using namespace std::chrono;
	return(duration<double>(steady_clock::now().time_since_epoch()).count());
}

#define HOST_NETPLAYER			((NETPLAYER)1)
#define	MAX_SYNC_MSG			(64 * 1024)

//***************************************************************************
// Wire formats of the in-mission messages (host <-> clients). Fixed-size
// fields, packed, little endian on every supported platform.
//***************************************************************************

#pragma pack(1)
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
#pragma pack()

//***************************************************************************
// WORLD CHUNK class (kept for the call sites; the data travels as
// NetWorldEvent records, see MultiPlayer::addWorldChunk)
//***************************************************************************

void* WorldChunk::operator new (size_t ourSize) {

	void* result = systemHeap->Malloc(ourSize);
	return(result);
}

void WorldChunk::operator delete (void* us) {

	systemHeap->Free(us);
}

void WorldChunk::buildMine (long worldCellR, long worldCellC, long teamId, long mineState, long explosionType) {

	init();
	type = WORLDCHUNK_MINE;
	tileRC[0] = (short)worldCellR;
	tileRC[1] = (short)worldCellC;
	param1 = teamId;
	param2 = (mineState & 0xFF) | ((explosionType & 0xFF) << 8);
}

void WorldChunk::buildTerrainFire (GameObjectPtr object, long seconds) {

	init();
	type = WORLDCHUNK_FIRE;
	objectId = object ? object->getPartId() : 0;
	param1 = seconds;
}

void WorldChunk::buildArtillery (long commanderId, long artilleryType, Stuff::Vector3D location, long seconds) {

	init();
	type = WORLDCHUNK_ARTILLERY_COMMANDER0;
	param1 = commanderId;
	param2 = artilleryType | (seconds << 8);
	objectBlockOrTrainNumber = (long)location.x;
	objectVertexOrCarNumber = (long)location.y;
}

void WorldChunk::buildMissionScriptMessage (long messageCode, long messageParam) {

	init();
	type = WORLDCHUNK_MISSION_SCRIPT_MESSAGE;
	param1 = messageCode;
	param2 = messageParam;
}

void WorldChunk::buildPilotKillStat (long moverIndex, long vehicleClass) {

	init();
	type = WORLDCHUNK_PILOT_KILL_STAT;
	param1 = moverIndex;
	param2 = vehicleClass;
}

void WorldChunk::buildScore (long commanderID, long score) {

	init();
	type = WORLDCHUNK_SCORE;
	param1 = commanderID;
	param2 = score;
}

void WorldChunk::buildKillLoss (long killerCID, long loserCID) {

	init();
	type = WORLDCHUNK_KILL_LOSS;
	param1 = killerCID;
	param2 = loserCID;
}

void WorldChunk::buildCaptureBuilding (BuildingPtr building, long newCommanderID) {

	init();
	type = WORLDCHUNK_CAPTURE_BUILDING;
	objectId = building ? building->getPartId() : 0;
	param1 = newCommanderID;
}

void WorldChunk::buildEndMission (void) {

	init();
	type = WORLDCHUNK_END_MISSION;
}

void WorldChunk::pack (void) {
}

void WorldChunk::unpack (void) {
}

bool WorldChunk::equalTo (WorldChunkPtr chunk) {

	return((type == chunk->type) && (tileRC[0] == chunk->tileRC[0]) && (tileRC[1] == chunk->tileRC[1])
		&& (objectId == chunk->objectId) && (param1 == chunk->param1) && (param2 == chunk->param2));
}

//***************************************************************************
// MULTIPLAYER class: rosters
//***************************************************************************

void MultiPlayer::addToLocalMovers (MoverPtr mover) {

	for (long i = 0; i < numLocalMovers; i++)
		if (localMovers[i] == mover)
			return;
	if (numLocalMovers < MAX_LOCAL_MOVERS)
		localMovers[numLocalMovers++] = mover;
}

//---------------------------------------------------------------------------

void MultiPlayer::removeFromLocalMovers (MoverPtr mover) {

	for (long i = 0; i < numLocalMovers; i++)
		if (localMovers[i] == mover) {
			localMovers[i] = localMovers[--numLocalMovers];
			localMovers[numLocalMovers] = NULL;
			return;
		}
}

//---------------------------------------------------------------------------
// Every machine loads the same mission and adds movers in the same order
// (mission parts, then each player's force, then reinforcements in the
// order the host announces them), so the first free slot gives the same
// net roster index everywhere.

void MultiPlayer::addToMoverRoster (MoverPtr mover) {

	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++)
		if (moverRoster[i] == mover)
			return;
	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++)
		if (!moverRoster[i]) {
			moverRoster[i] = mover;
			mover->setNetRosterIndex(i);
			lastMoveChunkSent[i] = 0xFFFFFFFF;
			lastStatusChunkSent[i] = 0xFFFFFFFF;
			if (i >= numMovers)
				numMovers = i + 1;
			return;
		}
	STOP(("MultiPlayer.addToMoverRoster: too many movers"));
}

//---------------------------------------------------------------------------

void MultiPlayer::removeFromMoverRoster (MoverPtr mover) {

	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++)
		if (moverRoster[i] == mover)
			moverRoster[i] = NULL;
}

//---------------------------------------------------------------------------

void MultiPlayer::addToPlayerMoverRoster (long playerCommanderID, MoverPtr mover) {

	if ((playerCommanderID < 0) || (playerCommanderID >= MAX_MC_PLAYERS))
		return;
	for (long i = 0; i < MAX_LOCAL_MOVERS; i++)
		if (playerMoverRoster[playerCommanderID][i] == mover)
			return;
	for (long i = 0; i < MAX_LOCAL_MOVERS; i++)
		if (!playerMoverRoster[playerCommanderID][i]) {
			playerMoverRoster[playerCommanderID][i] = mover;
			return;
		}
}

//---------------------------------------------------------------------------

void MultiPlayer::removeFromPlayerMoverRoster (MoverPtr mover) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		for (long j = 0; j < MAX_LOCAL_MOVERS; j++)
			if (playerMoverRoster[i][j] == mover)
				playerMoverRoster[i][j] = NULL;
}

//---------------------------------------------------------------------------

void MultiPlayer::addToTurretRoster (TurretPtr turret) {

	for (long i = 0; i < numTurrets; i++)
		if (turretRoster[i] == turret)
			return;
	if (numTurrets >= MAX_MULTIPLAYER_TURRETS)
		return;
	turretRoster[numTurrets] = turret;
	turret->setNetRosterIndex(numTurrets);
	lastTurretTargetSent[numTurrets] = 0;
	numTurrets++;
}

//---------------------------------------------------------------------------
// Called from Mission::init on every machine once the map is loaded.

void MultiPlayer::initSpecialBuildings (char commandersToLoad[8][3]) {

	numTurrets = 0;
	for (long i = 0; i < MAX_MULTIPLAYER_TURRETS; i++)
		turretRoster[i] = NULL;
	for (long i = 0; i < ObjectManager->getNumTurrets(); i++) {
		TurretPtr turret = ObjectManager->getTurret(i);
		if (turret)
			addToTurretRoster(turret);
	}
	numWorldEvents = 0;
	numWeaponHitChunks = 0;
	endMission = false;
	netPaused = false;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		pausesUsed[i] = 0;
	allUnitsDestroyed[0] = false;
	for (long i = 0; i < MAX_TEAMS; i++)
		teamScore[i] = 0;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		playerInfo[i].score = 0;
		playerInfo[i].kills = 0;
		playerInfo[i].losses = 0;
		playerInfo[i].winner = false;
	}
	winningTeam = -1;
	lastFullUpdateTime = 0.0;
	nextMissionStatusCheck = 0.0;
}

//***************************************************************************
// World events
//***************************************************************************

long MultiPlayer::addWorldChunk (WorldChunkPtr chunk) {

	if (!chunk || !isServer())
		return(0);
	if (numWorldEvents >= MAX_NET_WORLD_EVENTS)
		updateWorldChunks();
	NetWorldEvent& ev = worldEvents[numWorldEvents++];
	memset(&ev, 0, sizeof(ev));
	ev.type = (unsigned char)chunk->type;
	ev.a = chunk->tileRC[0];
	ev.b = chunk->tileRC[1];
	ev.c = (int)chunk->objectId;
	ev.d = (int)chunk->param1;
	ev.x = (float)chunk->param2;
	ev.y = 0.0f;
	if (chunk->type == WORLDCHUNK_ARTILLERY_COMMANDER0) {
		ev.c = (int)chunk->param2;	// type | seconds << 8
		ev.x = (float)chunk->objectBlockOrTrainNumber;
		ev.y = (float)chunk->objectVertexOrCarNumber;
	}
	if ((chunk->type >= 0) && (chunk->type < NUM_WORLDCHUNK_TYPES))
		worldChunkTally[chunk->type]++;
	return(numWorldEvents);
}

//---------------------------------------------------------------------------

long MultiPlayer::addMissionScriptMessageChunk (long code, long param) {

	WorldChunk chunk;
	chunk.buildMissionScriptMessage(code, param);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::addArtilleryChunk (long commanderId, long artilleryType, Stuff::Vector3D location, long seconds)
{
	WorldChunk chunk;
	chunk.buildArtillery(commanderId, artilleryType, location, seconds);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::addMineChunk (long tileR, long tileC, long teamId, long mineState, long explosionType) {

	WorldChunk chunk;
	chunk.buildMine(tileR, tileC, teamId, mineState, explosionType);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::addLightOnFireChunk (GameObjectPtr object, long seconds) {

	WorldChunk chunk;
	chunk.buildTerrainFire(object, seconds);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::addPilotKillStat (MoverPtr mover, long vehicleClass) {

	if (!mover)
		return(0);
	WorldChunk chunk;
	chunk.buildPilotKillStat(mover->getNetRosterIndex(), vehicleClass);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::addScoreChunk (long commanderID, long score) {

	if ((commanderID >= 0) && (commanderID < MAX_MC_PLAYERS))
		playerInfo[commanderID].score = score;
	return(0); // scores travel in every mover update
}

//---------------------------------------------------------------------------

long MultiPlayer::addKillLossChunk (long killerCID, long loserCID) {

	// The host keeps the tallies; they travel in every mover update.
	if ((killerCID >= 0) && (killerCID < MAX_MC_PLAYERS))
		playerInfo[killerCID].kills++;
	if ((loserCID >= 0) && (loserCID < MAX_MC_PLAYERS))
		playerInfo[loserCID].losses++;
	return(0);
}

//---------------------------------------------------------------------------

long MultiPlayer::addEndMissionChunk (void) {

	return(0);
}

//---------------------------------------------------------------------------

long MultiPlayer::addCaptureBuildingChunk (BuildingPtr building, long prevCommanderID, long newCommanderID) {

	WorldChunk chunk;
	chunk.buildCaptureBuilding(building, newCommanderID);
	return(addWorldChunk(&chunk));
}

//---------------------------------------------------------------------------

long MultiPlayer::grabWorldChunks (unsigned long* packedChunkBuffer) {

	return(0);
}

//---------------------------------------------------------------------------
// Send the pending world events (host).

long MultiPlayer::updateWorldChunks (void) {

	if (!numWorldEvents)
		return(0);
	static unsigned char buffer[sizeof(NetChunkList) + MAX_NET_WORLD_EVENTS * sizeof(NetWorldEvent)];
	NetChunkList* hdr = (NetChunkList*)buffer;
	hdr->type = MCMSG_WORLD_UPDATE;
	hdr->count = (unsigned short)numWorldEvents;
	memcpy(buffer + sizeof(NetChunkList), worldEvents, numWorldEvents * sizeof(NetWorldEvent));
	sendMessage(0, buffer, (int)(sizeof(NetChunkList) + numWorldEvents * sizeof(NetWorldEvent)), true);
	long n = numWorldEvents;
	numWorldEvents = 0;
	return(n);
}

//---------------------------------------------------------------------------

static void ApplyWorldEvent (MultiPlayer* mp, const NetWorldEvent& ev) {

	switch (ev.type) {
		case WORLDCHUNK_MINE: {
			long r = ev.a;
			long c = ev.b;
			long mineState = ((long)ev.x) & 0xFF;
			if (!GameMap->inBounds(r, c))
				break;
			GameMap->setMine(r, c, mineState);
			if (mineState == 2) {
				Stuff::Vector3D minePosition;
				land->cellToWorld(r, c, minePosition);
				ObjectManager->createExplosion(MINE_EXPLOSION_ID, NULL, minePosition, 0.0f, 0.0f);
			}
			break;
		}
		case WORLDCHUNK_FIRE: {
			GameObjectPtr obj = ObjectManager->findByPartId(ev.c);
			if (obj && obj->isTerrainObject())
				((TerrainObjectPtr)obj)->lightOnFire((float)ev.d);
			break;
		}
		case WORLDCHUNK_ARTILLERY_COMMANDER0: {
			Stuff::Vector3D loc;
			loc.x = ev.x;
			loc.y = ev.y;
			loc.z = land->getTerrainElevation(loc);
			CallArtillery(ev.d, ev.c & 0xFF, loc, (ev.c >> 8) & 0xFFFF, false);
			break;
		}
		case WORLDCHUNK_MISSION_SCRIPT_MESSAGE:
			CurMultiplayCode = ev.d;
			CurMultiplayParam = (long)ev.x;
			break;
		case WORLDCHUNK_PILOT_KILL_STAT:
			break;
		case WORLDCHUNK_CAPTURE_BUILDING: {
			GameObjectPtr obj = ObjectManager->findByPartId(ev.c);
			long cid = ev.d;
			if (obj && (cid >= 0) && (cid < MAX_MC_PLAYERS) && Commander::commanders[cid]) {
				obj->setCommanderId(cid);
				obj->setTeamId(Commander::commanders[cid]->getTeam()->getId(), false);
				if ((obj->getObjectType()->getObjTypeNum() == GENERIC_HQ_BUILDING_OBJNUM)
					&& (mp->missionSettings.missionType == MISSION_TYPE_CAPTURE_BASE))
					obj->setFlag(OBJECT_FLAG_CAPTURABLE, false);
			}
			break;
		}
		default:
			break;
	}
}

//***************************************************************************
// Weapon hits
//***************************************************************************

long MultiPlayer::addWeaponHitChunk (WeaponHitChunkPtr chunk) {

	if (!chunk || !isServer())
		return(0);
	if (numWeaponHitChunks >= MAX_WEAPONHIT_CHUNKS)
		updateWeaponHitChunks();
	weaponHitChunks[numWeaponHitChunks++] = (unsigned int)chunk->data;
	return(numWeaponHitChunks);
}

//---------------------------------------------------------------------------

long MultiPlayer::addWeaponHitChunk (GameObjectPtr target, WeaponShotInfoPtr shotInfo, bool isRefit) {

	if (!target || !shotInfo || !isServer())
		return(0);
	if (target->isMover() && (((MoverPtr)target)->getNetRosterIndex() < 0))
		return(0);
	WeaponShotInfo shot = *shotInfo;
	// the chunk carries the damage in quarter points
	shot.damage = (float)((unsigned long)(shot.damage * 4.0f)) * 0.25f;
	if (shot.damage > 255.0f)
		shot.damage = 255.0f;
	WeaponHitChunk chunk;
	chunk.init();
	chunk.build(target, &shot, isRefit);
	if ((chunk.targetType == 0) && !target->isMover())
		return(0); // not a target type the chunk knows
	chunk.pack();
	return(addWeaponHitChunk(&chunk));
}

//---------------------------------------------------------------------------

void MultiPlayer::grabWeaponHitChunks (unsigned long* packedChunkBuffer, long numChunks) {

	for (long i = 0; i < numChunks && i < numWeaponHitChunks; i++)
		packedChunkBuffer[i] = weaponHitChunks[i];
}

//---------------------------------------------------------------------------

long MultiPlayer::updateWeaponHitChunks (void) {

	if (!numWeaponHitChunks)
		return(0);
	static unsigned char buffer[sizeof(NetChunkList) + MAX_WEAPONHIT_CHUNKS * 4];
	NetChunkList* hdr = (NetChunkList*)buffer;
	hdr->type = MCMSG_WEAPONHIT_UPDATE;
	hdr->count = (unsigned short)numWeaponHitChunks;
	unsigned int* out = (unsigned int*)(buffer + sizeof(NetChunkList));
	for (long i = 0; i < numWeaponHitChunks; i++) {
		unsigned int v = (unsigned int)weaponHitChunks[i];
		memcpy(&out[i], &v, 4);
	}
	sendMessage(0, buffer, (int)(sizeof(NetChunkList) + numWeaponHitChunks * 4), true);
	long n = numWeaponHitChunks;
	numWeaponHitChunks = 0;
	return(n);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleWeaponHitUpdate (NETPLAYER sender, MCMSG_WeaponHitUpdate* msg) {

	const NetChunkList* hdr = (const NetChunkList*)msg;
	if (lastMessageSize < (long)sizeof(NetChunkList))
		return;
	long count = hdr->count;
	if ((long)(sizeof(NetChunkList) + count * 4) > lastMessageSize)
		return;
	const unsigned char* data = (const unsigned char*)msg + sizeof(NetChunkList);
	for (long i = 0; i < count; i++) {
		unsigned int v;
		memcpy(&v, data + i * 4, 4);
		WeaponHitChunk chunk;
		chunk.init();
		chunk.data = v;
		chunk.unpack();
		if (!chunk.valid(0))
			continue;
		WeaponShotInfo shotInfo;
		shotInfo.init(0, chunk.cause, chunk.damage, chunk.hitLocation, 0.0f);
		static const float entryQuadTable[4] = {0.0f, 180.0f, -90.0f, 90.0f};
		if ((chunk.entryAngle >= 0) && (chunk.entryAngle < 4))
			shotInfo.entryAngle = entryQuadTable[(int)chunk.entryAngle];
		GameObjectPtr target = NULL;
		if (chunk.targetType == WEAPONHITCHUNK_TARGET_MOVER)
			target = moverRoster[chunk.targetId];
		else
			target = ObjectManager->findByPartId(chunk.targetId);
		if (!target)
			continue;
		FromMP = true;
		if (chunk.refit && target->isMover()) {
			float pointsUsed = 0.0f;
			((MoverPtr)target)->refit(chunk.damage, pointsUsed);
		}
		else
			target->handleWeaponHit(&shotInfo, false);
		FromMP = false;
	}
}

//***************************************************************************
// Player orders (client -> host)
//***************************************************************************

void MultiPlayer::sendPlayerOrder (TacticalOrderPtr tacOrder,
								   bool needsSelection,
								   long numMovers,
								   MoverPtr* moverList,
								   long numGroups,
								   MoverGroupPtr* groupList,
   								   bool queuedOrder) {

	if (!tacOrder || !moverList)
		return;
	tacOrder->pack(NULL, NULL);
	for (long i = 0; i < numMovers; i++) {
		MoverPtr mover = moverList[i];
		if (!mover || (mover->getNetRosterIndex() < 0))
			continue;
		MCMSG_PlayerOrder msg;
		msg.commanderID = (char)commanderID;
		msg.flags = queuedOrder ? 1 : 0;
		if (!queuedOrder && (ControlGui::instance && ControlGui::instance->getMines()))
			msg.flags |= 2;
		msg.moverIndex = (unsigned char)mover->getNetRosterIndex();
		Stuff::Vector3D wayPoint = tacOrder->getWayPoint(0);
		msg.location[0] = wayPoint.x;
		msg.location[1] = wayPoint.y;
		msg.tacOrderChunk[0] = (unsigned int)tacOrder->data[0];
		msg.tacOrderChunk[1] = (unsigned int)tacOrder->data[1];
		sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
	}
}

//---------------------------------------------------------------------------

static bool SenderOwnsMover (MultiPlayer* mp, NETPLAYER sender, MoverPtr mover) {

	long cid = mp->findPlayer(sender);
	return(mover && (cid > -1) && (mover->getCommanderId() == cid));
}

void MultiPlayer::handlePlayerOrder (NETPLAYER sender, MCMSG_PlayerOrder* msg) {

	if (!isServer() || (msg->moverIndex >= MAX_MULTIPLAYER_MOVERS))
		return;
	MoverPtr mover = moverRoster[msg->moverIndex];
	if (!SenderOwnsMover(this, sender, mover) || !mover->getPilot())
		return; // players may only command their own units
	TacticalOrder order;
	order.init();
	order.data[0] = msg->tacOrderChunk[0];
	order.data[1] = msg->tacOrderChunk[1];
	order.unpack();
	if (order.moveParams.wayPath.numPoints > 0) {
		// the chunk only has the cell; use the exact point
		Stuff::Vector3D wayPoint;
		wayPoint.x = msg->location[0];
		wayPoint.y = msg->location[1];
		wayPoint.z = land->getTerrainElevation(wayPoint);
		order.setWayPoint(0, wayPoint);
	}
	order.pack(NULL, NULL);
	if (msg->flags & 1) {
		mover->getPilot()->setExecutingQueue(FALSE);
		mover->getPilot()->addQueuedTacOrder(order);
	}
	else if (msg->flags & 2) {
		mover->getPilot()->addQueuedTacOrder(order);
		mover->getPilot()->setExecutingQueue(TRUE);
	}
	else
		mover->handleTacticalOrder(order);
}

//---------------------------------------------------------------------------

void MultiPlayer::sendHoldPosition (void) {

	MCMSG_HoldPosition msg;
	msg.commanderID = (char)commanderID;
	msg.fireFromCurrentPos = (ControlGui::instance && ControlGui::instance->getFireFromCurrentPos()) ? 1 : 0;
	for (long i = 0; i < Team::home->getRosterSize() && msg.numMovers < MAX_LOCAL_MOVERS; i++) {
		MoverPtr mover = Team::home->getMover(i);
		if (mover && mover->isSelected() && (mover->getCommander()->getId() == Commander::home->getId())
			&& (mover->getNetRosterIndex() > -1)) {
			msg.moverIndex[msg.numMovers] = (unsigned char)mover->getNetRosterIndex();
			msg.attackRange[msg.numMovers] = (char)mover->attackRange;
			msg.numMovers++;
		}
	}
	if (msg.numMovers)
		sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleHoldPosition (NETPLAYER sender, MCMSG_HoldPosition* msg) {

	if (!isServer())
		return;
	for (long i = 0; i < msg->numMovers && i < MAX_LOCAL_MOVERS; i++) {
		if (msg->moverIndex[i] >= MAX_MULTIPLAYER_MOVERS)
			continue;
		MoverPtr mover = moverRoster[msg->moverIndex[i]];
		if (!SenderOwnsMover(this, sender, mover) || !mover->getPilot())
			continue;
		mover->attackRange = msg->attackRange[i];
		TacticalOrderPtr order = mover->getPilot()->getCurTacOrder();
		if (order->code != TACTICAL_ORDER_NONE) {
			order->attackParams.range = (FireRangeType)mover->attackRange;
			if ((mover->attackRange == FIRERANGE_CURRENT) || msg->fireFromCurrentPos)
				order->attackParams.pursue = false;
			else
				order->attackParams.pursue = true;
			order->pack(NULL, NULL);
			mover->handleTacticalOrder(*order);
		}
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerMoverGroup (long groupId,
										long numMovers,
										MoverPtr* moverList,
										long point) {
	// Mover groups are a local selection convenience; orders are sent per
	// mover, so nothing needs to travel here.
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerMoverGroup (NETPLAYER sender, MCMSG_PlayerMoverGroup* msg) {
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerArtillery (long strikeType, Stuff::Vector3D location, long seconds) {

	MCMSG_PlayerArtillery msg;
	msg.location[0] = location.x;
	msg.location[1] = location.y;
	msg.chunk = (unsigned int)((strikeType & 0xFF) | ((seconds & 0xFFFF) << 8));
	sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerArtillery (NETPLAYER sender, MCMSG_PlayerArtillery* msg) {

	if (!isServer())
		return;
	long cid = findPlayer(sender);
	if (cid < 0)
		return;
	Stuff::Vector3D loc;
	loc.x = msg->location[0];
	loc.y = msg->location[1];
	loc.z = land->getTerrainElevation(loc);
	CallArtillery(cid, msg->chunk & 0xFF, loc, (msg->chunk >> 8) & 0xFFFF, false);
}

//***************************************************************************
// Reinforcements and resource points
//***************************************************************************

void MultiPlayer::sendReinforcement (long vehicleID, long rosterIndex, const char pilotName[16], long commanderID, Stuff::Vector3D pos, unsigned char stage) {

	MCMSG_Reinforcement msg;
	msg.stage = stage;
	msg.rosterIndex = (unsigned char)rosterIndex;
	msg.vehicleID = (int)vehicleID;
	memset(msg.pilotName, 0, sizeof(msg.pilotName));
	if (pilotName)
		strncpy(msg.pilotName, pilotName, sizeof(msg.pilotName) - 1);
	msg.commanderID = (char)commanderID;
	msg.location[0] = pos.x;
	msg.location[1] = pos.y;
	if (isServer())
		handleReinforcement(HOST_NETPLAYER, &msg);
	else
		sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------
// stage 0: call a support VTOL; 2: it landed, bring in the vehicle;
// 3: call a recovery Karnov; 5: recovered; 6: resource point change;
// 7: drop a mover from its force. Clients ask, the host checks and applies,
// then tells everybody (stage + 100 = already validated by the host).

void MultiPlayer::handleReinforcement (NETPLAYER sender, MCMSG_Reinforcement* msg) {

	bool fromHost = !iAmHost || (sender == HOST_NETPLAYER);
	unsigned char stage = msg->stage;
	if (stage >= 100) {
		if (iAmHost)
			return; // only the host sends validated stages
		stage -= 100;
	}
	else if (iAmHost) {
		long senderCID = (sender == HOST_NETPLAYER) ? msg->commanderID : findPlayer(sender);
		if ((senderCID < 0) || ((sender != HOST_NETPLAYER) && (msg->commanderID != senderCID)
			&& (stage != 6)))
			return;
		if ((sender != HOST_NETPLAYER) && (stage == 6) && (msg->commanderID != senderCID))
			return;
		if (stage == 6) {
			// resource points live on the host and go out in mover updates
			long cid = msg->commanderID;
			if ((cid >= 0) && (cid < MAX_MC_PLAYERS)) {
				playerInfo[cid].resourcePoints += msg->vehicleID;
				if (msg->vehicleID > 0)
					playerInfo[cid].resourcePointsGained += msg->vehicleID;
			}
			return;
		}
		MCMSG_Reinforcement out = *msg;
		out.stage = (unsigned char)(stage + 100);
		sendMessage(0, &out, sizeof(out), true);
	}
	else if (!fromHost)
		return;

	long cid = msg->commanderID;
	if ((cid < 0) || (cid >= MAX_MC_PLAYERS))
		return;
	Stuff::Vector3D pos;
	pos.x = msg->location[0];
	pos.y = msg->location[1];
	pos.z = land->getTerrainElevation(pos);
	MissionInterfaceManagerPtr iface = MissionInterfaceManager::instance();
	switch (stage) {
		case 0:
			reinforcements[cid][0] = -1;
			if (iface)
				iface->beginVtol(msg->vehicleID, cid, &pos);
			break;
		case 2:
			BringInReinforcement(msg->vehicleID, 255, cid, pos, true);
			break;
		case 3:
			if (msg->rosterIndex < MAX_MULTIPLAYER_MOVERS) {
				reinforcements[cid][1] = msg->rosterIndex;
				strncpy(reinforcementPilot[cid], msg->pilotName, sizeof(reinforcementPilot[cid]) - 1);
				if (iface)
					iface->beginVtol(msg->vehicleID, cid, &pos, moverRoster[msg->rosterIndex]);
			}
			break;
		case 6:
			break;
		default:
			break;
	}
}

//***************************************************************************
// Mission start / end
//***************************************************************************

void MultiPlayer::sendStartMission (void) {

	MCMSG_StartMission msg;
	sendMessage(0, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleStartMission (NETPLAYER sender) {

	startMission = true;
}

//---------------------------------------------------------------------------

void MultiPlayer::sendEndMission (long result) {

	if (!isServer())
		return;
	MCMSG_EndMission msg;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		msg.teamScore[i] = teamScore[i];
		msg.playerScore[i] = playerInfo[i].score;
	}
	// result: winning team, or -1 for nobody
	msg.result = (int)winningTeam;
	updateWeaponHitChunks();
	updateWorldChunks();
	sendMessage(0, &msg, sizeof(msg), true);
	if (net)
		net->flush();
}

//---------------------------------------------------------------------------

void MultiPlayer::handleEndMission (NETPLAYER sender, MCMSG_EndMission* msg) {

	if (iAmHost)
		return;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		teamScore[i] = msg->teamScore[i];
		playerInfo[i].score = msg->playerScore[i];
	}
	winningTeam = msg->result;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		playerInfo[i].winner = (playerInfo[i].commanderID > -1) && (winningTeam > -1)
			&& (playerInfo[i].team == winningTeam);
	endMission = true;
}

//---------------------------------------------------------------------------
// Host: the mission ends when only one team has units left, a team met its
// objectives, or time ran out (best team score wins). Clients wait for the
// host's END_MISSION message.

bool MultiPlayer::calcMissionStatus (void) {

	if (!iAmHost) {
		// the host decides; if it is gone the mission is over (and lost)
		if (hostLeft && !endMission) {
			endMission = true;
			for (long i = 0; i < MAX_MC_PLAYERS; i++)
				playerInfo[i].winner = false;
		}
		return(endMission);
	}
	if (endMission)
		return(true);
	if (!startMission || (scenarioTime < 5.0f))
		return(false);
	double now = SyncNow();
	if (now < nextMissionStatusCheck)
		return(false);
	nextMissionStatusCheck = now + 0.5;

	// units alive per team (player forces only)
	long alive[MAX_TEAMS];
	bool teamPlaying[MAX_TEAMS];
	for (long t = 0; t < MAX_TEAMS; t++) {
		alive[t] = 0;
		teamPlaying[t] = false;
	}
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player
			&& (playerInfo[i].team >= 0) && (playerInfo[i].team < MAX_TEAMS))
			teamPlaying[(int)playerInfo[i].team] = true;
	for (long i = 0; i < ObjectManager->getNumMovers(); i++) {
		MoverPtr mover = ObjectManager->getMover(i);
		if (!mover || !mover->getExists() || mover->isDestroyed() || mover->isDisabled())
			continue;
		long cid = mover->getCommanderId();
		if ((cid < 0) || (cid >= MAX_MC_PLAYERS) || (playerInfo[cid].commanderID < 0))
			continue;
		long team = playerInfo[cid].team;
		if ((team >= 0) && (team < MAX_TEAMS))
			alive[team]++;
	}
	long teamsLeft = 0;
	long lastTeam = -1;
	for (long t = 0; t < MAX_TEAMS; t++)
		if (teamPlaying[t] && alive[t]) {
			teamsLeft++;
			lastTeam = t;
		}

	bool over = false;
	long winner = -1;
	// objectives (capture base, king of the hill, ...)
	for (long t = 0; t < MAX_TEAMS && !over; t++)
		if (teamPlaying[t] && Team::teams[t] && (Team::teams[t]->objectives.Count() > 0)
			&& (Team::teams[t]->numPrimaryObjectives > 0)
			&& (Team::teams[t]->objectives.Status() == OS_SUCCESSFUL)) {
			over = true;
			winner = t;
		}
	if (!over && (teamsLeft <= 1)) {
		over = true;
		winner = lastTeam;
	}
	if (!over && (missionSettings.timeLimit > 0.0f) && (scenarioTime > missionSettings.timeLimit)) {
		over = true;
		long best = -1;
		for (long t = 0; t < MAX_TEAMS; t++)
			if (teamPlaying[t] && ((best < 0) || (teamScore[t] > teamScore[best])
				|| ((teamScore[t] == teamScore[best]) && (alive[t] > alive[best]))))
				best = t;
		winner = best;
	}
	if (!over)
		return(false);

	winningTeam = winner;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		playerInfo[i].winner = (playerInfo[i].commanderID > -1) && (winner > -1) && (playerInfo[i].team == winner);
	endMission = true;
	sendEndMission(winner);
	return(true);
}

//***************************************************************************
// Host -> clients state updates
//***************************************************************************

void MultiPlayer::buildMoverRosterRLE (void) {
}

//---------------------------------------------------------------------------

void MultiPlayer::sendMoverUpdate (void) {

	static unsigned char buffer[sizeof(NetMoverUpdate) + MAX_MULTIPLAYER_MOVERS * sizeof(NetMoverEntry)];
	NetMoverUpdate* hdr = (NetMoverUpdate*)buffer;
	memset(hdr, 0, sizeof(NetMoverUpdate));
	hdr->type = MCMSG_MOVER_UPDATE;
	hdr->updateId = ++moverUpdateId;
	hdr->missionTime = scenarioTime;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		hdr->teamScore[i] = teamScore[i];
		hdr->playerScore[i] = playerInfo[i].score;
		hdr->kills[i] = (short)playerInfo[i].kills;
		hdr->losses[i] = (short)playerInfo[i].losses;
		hdr->resourcePoints[i] = playerInfo[i].resourcePoints;
		if (playerInfo[i].leftSession)
			hdr->leftSession |= (unsigned char)(1 << i);
	}

	double now = SyncNow();
	bool full = (now - lastFullUpdateTime) > 2.0;
	if (full)
		lastFullUpdateTime = now;

	NetMoverEntry* entries = (NetMoverEntry*)(buffer + sizeof(NetMoverUpdate));
	long n = 0;
	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
		MoverPtr mover = moverRoster[i];
		if (!mover)
			continue;
		mover->buildStatusChunk();
		mover->buildMoveChunk();
		unsigned int status = (unsigned int)mover->getStatusChunk()->data;
		unsigned int move = (unsigned int)mover->getMoveChunk()->data;
		unsigned char flags = 0;
		if (full || (status != lastStatusChunkSent[i]))
			flags |= 1;
		if ((full || (move != lastMoveChunkSent[i])) && !mover->isDestroyed())
			flags |= 2;
		if (!flags)
			continue;
		NetMoverEntry e;
		e.index = (unsigned char)i;
		e.flags = flags;
		e.status = status;
		e.move = move;
		memcpy(&entries[n++], &e, sizeof(e));
		lastStatusChunkSent[i] = status;
		lastMoveChunkSent[i] = move;
	}
	hdr->numEntries = (unsigned char)n;
	sendMessage(0, buffer, (int)(sizeof(NetMoverUpdate) + n * sizeof(NetMoverEntry)), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleMoverUpdate (NETPLAYER sender, MCMSG_MoverUpdate* msg) {

	if (lastMessageSize < (long)sizeof(NetMoverUpdate))
		return;
	NetMoverUpdate hdr;
	memcpy(&hdr, msg, sizeof(hdr));
	if ((long)(sizeof(NetMoverUpdate) + hdr.numEntries * sizeof(NetMoverEntry)) > lastMessageSize)
		return;
	hostMissionTime = hdr.missionTime;
	if (commanderID >= 0 && commanderID < MAX_MC_PLAYERS)
		LogisticsData::instance->syncResourcePoints(hdr.resourcePoints[commanderID]);
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		teamScore[i] = hdr.teamScore[i];
		playerInfo[i].score = hdr.playerScore[i];
		playerInfo[i].kills = hdr.kills[i];
		playerInfo[i].losses = hdr.losses[i];
		playerInfo[i].resourcePoints = hdr.resourcePoints[i];
		if (hdr.leftSession & (1 << i))
			playerInfo[i].leftSession = true;
	}
	const unsigned char* data = (const unsigned char*)msg + sizeof(NetMoverUpdate);
	for (long i = 0; i < hdr.numEntries; i++) {
		NetMoverEntry e;
		memcpy(&e, data + i * sizeof(NetMoverEntry), sizeof(e));
		if (e.index >= MAX_MULTIPLAYER_MOVERS)
			continue;
		MoverPtr mover = moverRoster[e.index];
		if (!mover)
			continue;
		if (e.flags & 1)
			mover->handleStatusChunk(0, e.status);
		if ((e.flags & 2) && !mover->isDestroyed() && mover->getPilot())
			mover->handleMoveChunk(e.move);
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::sendTurretUpdate (void) {

	static unsigned char buffer[sizeof(NetTurretUpdate) + MAX_MULTIPLAYER_TURRETS * sizeof(NetTurretEntry)];
	NetTurretUpdate* hdr = (NetTurretUpdate*)buffer;
	hdr->type = MCMSG_TURRET_UPDATE;
	long n = 0;
	unsigned char* out = buffer + sizeof(NetTurretUpdate);
	for (long i = 0; i < numTurrets; i++) {
		TurretPtr turret = turretRoster[i];
		if (!turret)
			continue;
		GameObjectPtr target = turret->targetWID ? ObjectManager->getByWatchID(turret->targetWID) : NULL;
		long partId = target ? target->getPartId() : 0;
		if (partId == lastTurretTargetSent[i])
			continue;
		lastTurretTargetSent[i] = partId;
		NetTurretEntry e;
		e.index = (unsigned char)i;
		e.targetPartId = (int)partId;
		memcpy(out + n * sizeof(NetTurretEntry), &e, sizeof(e));
		n++;
	}
	if (!n)
		return;
	hdr->numEntries = (unsigned char)n;
	sendMessage(0, buffer, (int)(sizeof(NetTurretUpdate) + n * sizeof(NetTurretEntry)), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleTurretUpdate (NETPLAYER sender, MCMSG_TurretUpdate* msg) {

	if (lastMessageSize < (long)sizeof(NetTurretUpdate))
		return;
	const NetTurretUpdate* hdr = (const NetTurretUpdate*)msg;
	long n = hdr->numEntries;
	if ((long)(sizeof(NetTurretUpdate) + n * sizeof(NetTurretEntry)) > lastMessageSize)
		return;
	const unsigned char* data = (const unsigned char*)msg + sizeof(NetTurretUpdate);
	for (long i = 0; i < n; i++) {
		NetTurretEntry e;
		memcpy(&e, data + i * sizeof(NetTurretEntry), sizeof(e));
		if (e.index >= numTurrets || !turretRoster[e.index])
			continue;
		GameObjectPtr target = e.targetPartId ? ObjectManager->findByPartId(e.targetPartId) : NULL;
		turretRoster[e.index]->targetWID = target ? target->getWatchID() : 0;
	}
}

//---------------------------------------------------------------------------
// Weapon fire, critical hits and radio messages of the host's movers.

void MultiPlayer::sendMoverWeaponFireUpdate (void) {

	static unsigned char buffer[MAX_SYNC_MSG];
	NetMoverEventsHeader* hdr = (NetMoverEventsHeader*)buffer;
	hdr->type = MCMSG_MOVER_WEAPONFIRE_UPDATE;
	hdr->numMovers = 0;
	long size = sizeof(NetMoverEventsHeader);
	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
		MoverPtr mover = moverRoster[i];
		if (!mover)
			continue;
		long nFire = mover->getNumWeaponFireChunks(CHUNK_SEND);
		long nCrit = mover->getNumCriticalHitChunks(CHUNK_SEND);
		long nRadio = mover->getNumRadioChunks(CHUNK_SEND);
		if (!nFire && !nCrit && !nRadio)
			continue;
		if (nFire > 255) nFire = 255;
		if (nCrit > 255) nCrit = 255;
		long need = (long)sizeof(NetMoverEvents) + nFire * 4 + nCrit + nRadio;
		if (size + need > MAX_SYNC_MSG) {
			// flush what we have and start a new message
			sendMessage(0, buffer, (int)size, true);
			hdr->numMovers = 0;
			size = sizeof(NetMoverEventsHeader);
		}
		NetMoverEvents ev;
		ev.index = (unsigned char)i;
		ev.numFire = (unsigned char)nFire;
		ev.numCrit = (unsigned char)nCrit;
		ev.numRadio = (unsigned char)nRadio;
		memcpy(buffer + size, &ev, sizeof(ev));
		size += sizeof(ev);
		unsigned long fire[MAX_WEAPONFIRE_CHUNKS];
		long got = mover->grabWeaponFireChunks(CHUNK_SEND, fire, nFire);
		for (long j = 0; j < got; j++) {
			unsigned int v = (unsigned int)fire[j];
			memcpy(buffer + size + j * 4, &v, 4);
		}
		for (long j = got; j < nFire; j++)
			memset(buffer + size + j * 4, 0, 4);
		size += nFire * 4;
		unsigned char crit[MAX_CRITICALHIT_CHUNKS];
		mover->grabCriticalHitChunks(CHUNK_SEND, crit);
		mover->clearCriticalHitChunks(CHUNK_SEND);
		memcpy(buffer + size, crit, nCrit);
		size += nCrit;
		unsigned char radio[MAX_RADIO_CHUNKS];
		mover->grabRadioChunks(CHUNK_SEND, radio);
		mover->clearRadioChunks(CHUNK_SEND);
		memcpy(buffer + size, radio, nRadio);
		size += nRadio;
		hdr->numMovers++;
	}
	if (hdr->numMovers)
		sendMessage(0, buffer, (int)size, true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleMoverWeaponFireUpdate (NETPLAYER sender, MCMSG_MoverWeaponFireUpdate* msg) {

	const unsigned char* data = (const unsigned char*)msg;
	long size = lastMessageSize;
	if (size < (long)sizeof(NetMoverEventsHeader))
		return;
	long count = ((const NetMoverEventsHeader*)data)->numMovers;
	long pos = sizeof(NetMoverEventsHeader);
	for (long i = 0; i < count; i++) {
		if (pos + (long)sizeof(NetMoverEvents) > size)
			return;
		NetMoverEvents ev;
		memcpy(&ev, data + pos, sizeof(ev));
		pos += sizeof(ev);
		long need = ev.numFire * 4 + ev.numCrit + ev.numRadio;
		if (pos + need > size)
			return;
		MoverPtr mover = (ev.index < MAX_MULTIPLAYER_MOVERS) ? moverRoster[ev.index] : NULL;
		if (mover) {
			long room = MAX_WEAPONFIRE_CHUNKS - 1 - mover->getNumWeaponFireChunks(CHUNK_RECEIVE);
			unsigned long fire[MAX_WEAPONFIRE_CHUNKS];
			long nFire = 0;
			for (long j = 0; j < ev.numFire && nFire < room; j++) {
				unsigned int v;
				memcpy(&v, data + pos + j * 4, 4);
				fire[nFire++] = v;
			}
			if (nFire > 0)
				mover->addWeaponFireChunks(CHUNK_RECEIVE, fire, nFire);
			long critRoom = MAX_CRITICALHIT_CHUNKS - 1 - mover->getNumCriticalHitChunks(CHUNK_RECEIVE);
			long nCrit = (ev.numCrit < critRoom) ? ev.numCrit : critRoom;
			if (nCrit > 0)
				mover->addCriticalHitChunks(CHUNK_RECEIVE, (unsigned char*)(data + pos + ev.numFire * 4), nCrit);
			if (ev.numRadio)
				mover->addRadioChunks(CHUNK_RECEIVE, (unsigned char*)(data + pos + ev.numFire * 4 + ev.numCrit), ev.numRadio);
		}
		pos += need;
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::sendTurretWeaponFireUpdate (void) {

	static unsigned char buffer[MAX_SYNC_MSG];
	NetMoverEventsHeader* hdr = (NetMoverEventsHeader*)buffer;
	hdr->type = MCMSG_TURRET_WEAPONFIRE_UPDATE;
	hdr->numMovers = 0;
	long size = sizeof(NetMoverEventsHeader);
	for (long i = 0; i < numTurrets; i++) {
		TurretPtr turret = turretRoster[i];
		if (!turret)
			continue;
		unsigned long fire[MAX_WEAPONFIRE_CHUNKS];
		long n = turret->grabWeaponFireChunks(CHUNK_SEND, fire);
		turret->clearWeaponFireChunks(CHUNK_SEND);
		if (n <= 0)
			continue;
		if (n > 255)
			n = 255;
		if (size + (long)sizeof(NetTurretFire) + n * 4 > MAX_SYNC_MSG)
			break;
		NetTurretFire tf;
		tf.index = (unsigned char)i;
		tf.numFire = (unsigned char)n;
		memcpy(buffer + size, &tf, sizeof(tf));
		size += sizeof(tf);
		for (long j = 0; j < n; j++) {
			unsigned int v = (unsigned int)fire[j];
			memcpy(buffer + size + j * 4, &v, 4);
		}
		size += n * 4;
		hdr->numMovers++;
	}
	if (hdr->numMovers)
		sendMessage(0, buffer, (int)size, true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleTurretWeaponFireUpdate (NETPLAYER sender, MCMSG_TurretWeaponFireUpdate* msg) {

	const unsigned char* data = (const unsigned char*)msg;
	long size = lastMessageSize;
	if (size < (long)sizeof(NetMoverEventsHeader))
		return;
	long count = ((const NetMoverEventsHeader*)data)->numMovers;
	long pos = sizeof(NetMoverEventsHeader);
	for (long i = 0; i < count; i++) {
		if (pos + (long)sizeof(NetTurretFire) > size)
			return;
		NetTurretFire tf;
		memcpy(&tf, data + pos, sizeof(tf));
		pos += sizeof(tf);
		if (pos + tf.numFire * 4 > size)
			return;
		TurretPtr turret = (tf.index < numTurrets) ? turretRoster[tf.index] : NULL;
		if (turret) {
			unsigned long fire[MAX_WEAPONFIRE_CHUNKS];
			long n = 0;
			for (long j = 0; j < tf.numFire && n < MAX_WEAPONFIRE_CHUNKS - 1; j++) {
				unsigned int v;
				memcpy(&v, data + pos + j * 4, 4);
				fire[n++] = v;
			}
			if (n > 0)
				turret->addWeaponFireChunks(CHUNK_RECEIVE, fire, n);
		}
		pos += tf.numFire * 4;
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::sendMoverCriticalUpdate (void) {
	// sent together with the weapon fire (sendMoverWeaponFireUpdate)
}

//---------------------------------------------------------------------------

void MultiPlayer::handleMoverCriticalUpdate (NETPLAYER sender, MCMSG_MoverCriticalUpdate* msg) {
}

//---------------------------------------------------------------------------

void MultiPlayer::sendWeaponHitUpdate (void) {

	updateWeaponHitChunks();
}

//---------------------------------------------------------------------------

void MultiPlayer::sendWorldUpdate (void) {

	updateWorldChunks();
}

//---------------------------------------------------------------------------

void MultiPlayer::handleWorldUpdate (NETPLAYER sender, MCMSG_WorldUpdate* msg) {

	if (lastMessageSize < (long)sizeof(NetChunkList))
		return;
	const NetChunkList* hdr = (const NetChunkList*)msg;
	long count = hdr->count;
	if ((long)(sizeof(NetChunkList) + count * sizeof(NetWorldEvent)) > lastMessageSize)
		return;
	const unsigned char* data = (const unsigned char*)msg + sizeof(NetChunkList);
	for (long i = 0; i < count; i++) {
		NetWorldEvent ev;
		memcpy(&ev, data + i * sizeof(NetWorldEvent), sizeof(ev));
		ApplyWorldEvent(this, ev);
	}
}

//---------------------------------------------------------------------------
// Host, every frame during the mission. Events go out every frame (they are
// small and order matters); unit state at moverUpdateFrequency.

long MultiPlayer::updateServer (void) {

	if (!isServer() || !net)
		return(MPLAYER_NO_ERR);
	// Test aid (scripts/run_net_test.py): MC2_NET_TEST_CHARGE=1 sends the
	// host's units at the other players' units after 10 seconds, so the
	// test sees combat without driving the host's mouse.
	static bool charged = false;
	if (!charged && (scenarioTime > 10.0f) && getenv("MC2_NET_TEST_CHARGE")) {
		charged = true;
		Stuff::Vector3D goal;
		goal.Zero();
		long n = 0;
		for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++)
			if (moverRoster[i] && (moverRoster[i]->getCommanderId() != commanderID)
				&& (moverRoster[i]->getCommanderId() > -1)) {
				goal.x += moverRoster[i]->getPosition().x;
				goal.y += moverRoster[i]->getPosition().y;
				n++;
			}
		// Player brains only shoot back, so give attack orders.
		if (n)
			for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
				MoverPtr mover = moverRoster[i];
				if (!mover || (mover->getCommanderId() != commanderID) || !mover->getPilot())
					continue;
				MoverPtr best = NULL;
				float bestDist = 0.0f;
				for (long j = 0; j < MAX_MULTIPLAYER_MOVERS; j++) {
					MoverPtr enemy = moverRoster[j];
					if (!enemy || (enemy->getCommanderId() == commanderID) || (enemy->getCommanderId() < 0))
						continue;
					float d = mover->distanceFrom(enemy->getPosition());
					if (!best || (d < bestDist)) {
						best = enemy;
						bestDist = d;
					}
				}
				if (!best)
					continue;
				TacticalOrder order;
				order.init(ORDER_ORIGIN_PLAYER, TACTICAL_ORDER_ATTACK_OBJECT);
				order.targetWID = best->getWatchID();
				order.attackParams.type = ATTACK_TO_DESTROY;
				order.attackParams.method = ATTACKMETHOD_RANGED;
				order.attackParams.range = FIRERANGE_OPTIMAL;
				order.attackParams.pursue = true;
				order.moveParams.wayPath.mode[0] = TRAVEL_MODE_FAST;
				order.pack(NULL, NULL);
				mover->handleTacticalOrder(order);
			}
	}

	sendMoverWeaponFireUpdate();
	sendTurretWeaponFireUpdate();
	updateWeaponHitChunks();
	updateWorldChunks();
	static double lastMoverSend = 0.0;
	static double lastTurretSend = 0.0;
	double now = SyncNow();
	if (now - lastMoverSend >= moverUpdateFrequency) {
		lastMoverSend = now;
		lastServerUpdateTime = scenarioTime;
		sendMoverUpdate();
	}
	if (now - lastTurretSend >= turretUpdateFrequency) {
		lastTurretSend = now;
		sendTurretUpdate();
	}
	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------

long MultiPlayer::updateClients (bool forceIt) {

	return(updateServer());
}

//---------------------------------------------------------------------------
// A player dropped during the mission: the host's AI takes over their units
// so the battle goes on.

void MultiPlayer::playerLeftGame (DWORD commanderId) {

	if (commanderId >= MAX_MC_PLAYERS)
		return;
	if (!iAmHost || (mode != MULTIPLAYER_MODE_MISSION))
		return;
	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
		MoverPtr mover = moverRoster[i];
		if (mover && (mover->getCommanderId() == (long)commanderId))
			mover->setControl(CONTROL_AI);
	}
}

//---------------------------------------------------------------------------
// Test aid: with MC2_NET_STATEFILE=<path>, write every second of mission
// time one line per live roster mover: time, index, cell position, status.
// scripts/run_net_test.py compares the host's and the client's files.

void MultiPlayer::logState (void) {

	static FILE* file = NULL;
	static bool checked = false;
	static long lastSecond = -1;
	if (!checked) {
		checked = true;
		const char* path = getenv("MC2_NET_STATEFILE");
		if (path && path[0])
			file = fopen(path, "w");
	}
	if (!file || (mode != MULTIPLAYER_MODE_MISSION))
		return;
	// clients log against the host's mission clock so lines line up
	long second = (long)(iAmHost ? scenarioTime : hostMissionTime);
	if (second == lastSecond)
		return;
	lastSecond = second;
	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
		MoverPtr mover = moverRoster[i];
		if (!mover)
			continue;
		Stuff::Vector3D pos = mover->getPosition();
		fprintf(file, "%ld %ld %.1f %.1f %d %d\n", second, i, pos.x, pos.y,
			mover->isDestroyed() ? 1 : 0, mover->isDisabled() ? 1 : 0);
	}
	if (getenv("MC2_NET_DEBUG_COMBAT") && (second % 10 == 0))
		for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++) {
			MoverPtr mover = moverRoster[i];
			if (!mover || !mover->getPilot())
				continue;
			GameObjectPtr target = mover->getPilot()->getCurrentTarget();
			int contacts[MAX_MOVERS];
			long nEnemy = mover->getContacts(contacts, CONTACT_CRITERIA_ENEMY, CONTACT_SORT_NONE);
			fprintf(file, "%ld dbg %ld cid %ld team %ld ctrl %d target %ld enemies %ld order %d\n", second, i,
				mover->getCommanderId(), mover->getTeamId(), (int)mover->control.getType(),
				target ? target->getPartId() : 0, nEnemy, (int)mover->getPilot()->getCurTacOrder()->code);
		}
	fprintf(file, "%ld kills", second);
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		fprintf(file, " %d/%d", playerInfo[i].kills, playerInfo[i].losses);
	fprintf(file, "\n");
	fflush(file);
}

//---------------------------------------------------------------------------

void MultiPlayer::clearRosters (void) {

	for (long i = 0; i < MAX_MULTIPLAYER_MOVERS; i++)
		moverRoster[i] = NULL;
	for (long i = 0; i < MAX_MULTIPLAYER_TURRETS; i++)
		turretRoster[i] = NULL;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		for (long j = 0; j < MAX_LOCAL_MOVERS; j++)
			playerMoverRoster[i][j] = NULL;
	for (long i = 0; i < MAX_LOCAL_MOVERS; i++)
		localMovers[i] = NULL;
	numMovers = 0;
	numTurrets = 0;
	numLocalMovers = 0;
	numWeaponHitChunks = 0;
	numWorldEvents = 0;
}

//***************************************************************************
// Network pause: any player may pause (3 times per game) or resume; the
// host decides and tells everybody, so the battle stops on every machine.
//***************************************************************************

#define MAX_PAUSES_PER_PLAYER	3

#pragma pack(1)
typedef struct _NetPause {
	unsigned char	type;			// MCMSG_PAUSE
	unsigned char	paused;
	char			commanderID;	// who asked
	unsigned char	refused;		// host: no pauses left
} NetPause;
#pragma pack()

static void AnnouncePause (MultiPlayer* mp, long cid, bool paused) {

	char format[256];
	char text[512];
	cLoadString(paused ? IDS_MP_NET_PAUSED : IDS_MP_NET_RESUMED, format, 255);
	const char* name = ((cid >= 0) && (cid < MAX_MC_PLAYERS)) ? mp->playerInfo[cid].name : "";
	snprintf(text, sizeof(text), format, name);
	mp->addLocalChat(cid, text, true, false);
	printf("[net] %s by commander %ld\n", paused ? "paused" : "resumed", cid);
	fflush(stdout);
	if (paused)
		soundSystem->playDigitalSample(LOG_NEXTBACKBUTTONS);
}

void MultiPlayer::requestPause (bool pause) {

	NetPause msg;
	msg.type = MCMSG_PAUSE;
	msg.paused = pause ? 1 : 0;
	msg.commanderID = (char)commanderID;
	msg.refused = 0;
	if (iAmHost)
		handlePause(HOST_NETPLAYER, (const unsigned char*)&msg, sizeof(msg));
	else
		sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
}

void MultiPlayer::handlePause (NETPLAYER sender, const unsigned char* data, int size) {

	if (size < (int)sizeof(NetPause))
		return;
	NetPause msg;
	memcpy(&msg, data, sizeof(msg));
	if (iAmHost) {
		long cid = (sender == HOST_NETPLAYER) ? commanderID : findPlayer(sender);
		if ((cid < 0) || (mode != MULTIPLAYER_MODE_MISSION))
			return;
		bool pause = msg.paused != 0;
		if (pause == netPaused)
			return;
		if (pause && (pausesUsed[cid] >= MAX_PAUSES_PER_PLAYER)) {
			NetPause no = msg;
			no.refused = 1;
			if (sender == HOST_NETPLAYER) {
				char text[256];
				cLoadString(IDS_MP_NET_NO_PAUSES, text, 255);
				addLocalChat(cid, text, true, false);
			}
			else
				sendMessage(sender, &no, sizeof(no), true);
			return;
		}
		if (pause)
			pausesUsed[cid]++;
		netPaused = pause;
		NetPause out;
		out.type = MCMSG_PAUSE;
		out.paused = pause ? 1 : 0;
		out.commanderID = (char)cid;
		out.refused = 0;
		sendMessage(0, &out, sizeof(out), true);
		AnnouncePause(this, cid, pause);
		return;
	}
	if (msg.refused) {
		char text[256];
		cLoadString(IDS_MP_NET_NO_PAUSES, text, 255);
		addLocalChat(commanderID, text, true, false);
		return;
	}
	netPaused = msg.paused != 0;
	AnnouncePause(this, msg.commanderID, netPaused);
}
