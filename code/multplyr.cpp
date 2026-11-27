//***************************************************************************
//
//	multplyr.cpp - This file contains the MultiPlayer Class
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

#ifndef LINUX_BUILD
#include"dplay8.h"
#endif

#include"mpparameterscreen.h"
#include "net/gos_net.h"
#include "update/gos_update.h"
#include "mc2_version.h"

//sebi: commented include
//#ifndef VERSION_H
//#include"version.h"
//#endif

#include "../resource.h"

#ifdef USE_MISSION_RESULTS_SCREEN
extern bool EventsToMissionResultsScreen;
#else
bool EventsToMissionResultsScreen = false;
#endif

#ifdef USE_LOGISTICS
extern bool whackTimer;
void CancelBool(long value);
#endif

#ifndef MPPREFS_H
#include"mpprefs.h"
#endif

#ifdef USE_STRING_RESOURCES
extern HINSTANCE thisInstance;
long cLoadString (HINSTANCE hInstance,  UINT uID, LPTSTR lpBuffer, int nBufferMax );
#endif

#include"gamesound.h"

#define	MAX_MSG_SIZE		10240

extern CPrefs prefs;
extern bool quitGame;

extern float loadProgress;
extern bool aborted;

//***************************************************************************

DWORD ServerPlayerNum = 1; //commanderId (or checkInId) of server
bool MultiPlayer::launchedFromLobby = false;
bool MultiPlayer::registerZone = false;

long MultiPlayer::presetDropZones[MAX_MC_PLAYERS] = {-1, -1, -1, -1, -1, -1, -1, -1};
long MultiPlayer::colors[MAX_COLORS] = {0};

float ResourceBuildingRefreshRate = 60.0f;

// {35DC7890-C5EF-4171-B0CF-4D5C7AE7C2D7}
static const GUID MC2GUID = { 0x35dc7890, 0xc5ef, 0x4171, { 0xb0, 0xcf, 0x4d, 0x5c, 0x7a, 0xe7, 0xc2, 0xd7 } };
// {1F8251BB-1436-44b3-A5B0-0FCF6858C176}
static const GUID MC2DEMOGUID = { 0x1f8251bb, 0x1436, 0x44b3, { 0xa5, 0xb0, 0xf, 0xcf, 0x68, 0x58, 0xc1, 0x76 } };

void* MC2NetLib = NULL;
bool OnLAN = false;
NETMESSAGE ReceiveMsg;

//------------
// EXTERN vars
extern GameLog* CombatLog;
extern GameLog* NetLog;

extern bool LaunchedFromLobby;

extern UserHeapPtr systemHeap;

#ifdef _DEBUG
//extern DebugFileStream Debug;
#endif

extern char* startupPakFile;

// ConnectUsingDialog is a function defined in dpdialog.cpp.  
// It allows the user to choose the type of connection.
//extern HRESULT ConnectUsingDialog(HINSTANCE );

extern void SortMoverList (long numMovers, MoverPtr* moverList, Stuff::Vector3D dest);

extern void killTheGame(void);

#ifndef TERRAINEDIT
extern DebuggerPtr debugger;
#endif

//------------
// GLOBAL vars
MultiPlayer* MPlayer = NULL;
//extern long NumMissionScriptMessages;

///extern Scenario* scenario;
///extern Logistics* globalLogPtr;

void DEBUGWINS_print (const char* s, long window = 0);

//***************************************************************************
// MISC functions
//***************************************************************************

bool StartupNetworking (void) {

	return true;
}

//-----------------------------------------------------------------------------

void ResetNetworking (void) {

}

//-----------------------------------------------------------------------------

void ShutdownNetworking (void) {

	gos_NetGlobalShutdown();

}


//***************************************************************************
// LAN TRANSPORT
//
// Star topology over GameOS/gameos/net (ENet): clients only talk to the
// host, the host applies or validates every message and forwards it. The
// host is authoritative: it runs the simulation (isServer()), clients send
// orders and show what the host reports.
//
// Messages are the packed MCMSG_* structures from multplyr.h, sent as raw
// bytes; their fields are fixed-size so Windows, Linux and macOS builds can
// play together. Network ids (NETPLAYER): 1 = host, 2 + n = host peer n.
//***************************************************************************

#include <chrono>
#include <cstddef>

#define MC2_NET_PROTOCOL_STAMP	"MC2NET-4"		// bump when the wire format changes

// Tests pretend to be another release with MC2_NET_TEST_VERSION and
// MC2_NET_TEST_PROTOCOL (scripts/run_net_test.py --scenario version).
static const char* NetProtocolStamp (void) {
	static const char* stamp = getenv("MC2_NET_TEST_PROTOCOL") ? getenv("MC2_NET_TEST_PROTOCOL") : MC2_NET_PROTOCOL_STAMP;
	return(stamp);
}

static const char* NetGameVersion (void) {
	static const char* version = getenv("MC2_NET_TEST_VERSION") ? getenv("MC2_NET_TEST_VERSION") : MC2_VERSION;
	return(version);
}
#define HOST_NETPLAYER			((NETPLAYER)1)

static inline NETPLAYER PeerToNetPlayer (int peer) {
	return((NETPLAYER)(peer + 2));
}

static inline int NetPlayerToPeer (NETPLAYER player) {
	return((int)player - 2);
}

static double NetNow (void) {
	using namespace std::chrono;
	return(duration<double>(steady_clock::now().time_since_epoch()).count());
}

// Key network events go to stdout ("[net] ..."); MC2_NET_LOG=2 adds every
// message.
static int NetLogLevel (void) {
	static int level = -1;
	if (level < 0) {
		const char* env = getenv("MC2_NET_LOG");
		level = env ? atoi(env) : 1;
	}
	return(level);
}

#define NETLOG(lvl, ...) do { if (NetLogLevel() >= (lvl)) { printf("[net] " __VA_ARGS__); printf("\n"); fflush(stdout); } } while (0)

static void CopyString (char* dst, const char* src, size_t size) {
	if (!size)
		return;
	if (!src)
		src = "";
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
}

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

static unsigned int HostSessionId = 0;
#pragma pack()

//---------------------------------------------------------------------------
// Hash of the loaded data packs (file table of every .fst: sizes and name
// hashes). Two machines with different game data would desync, so the host
// refuses players whose hash differs.

static unsigned int ComputeDataHash (void) {
	unsigned int h = 2166136261u;
	auto mix = [&h](unsigned int v) {
		for (int b = 0; b < 4; b++) {
			h ^= (v >> (b * 8)) & 0xff;
			h *= 16777619u;
		}
	};
	for (long i = 0; i < numFastFiles; i++) {
		FastFile* ff = fastFiles ? fastFiles[i] : NULL;
		if (!ff)
			continue;
		long n = ff->getNumFiles();
		mix((unsigned int)n);
		const FILE_HANDLE* files = ff->getFilesInfo();
		for (long j = 0; files && j < n; j++) {
			const FILEENTRY* fe = files[j].pfe;
			if (!fe)
				continue;
			mix(fe->hash);
			mix(fe->realSize);
		}
	}
	return(h);
}

//***************************************************************************
// MULTIPLAYER class
//***************************************************************************

void* MultiPlayer::operator new (size_t ourSize) {

	void* result = systemHeap->Malloc(ourSize);
	return(result);
}

//---------------------------------------------------------------------------

void MultiPlayer::operator delete (void* us) {

	systemHeap->Free(us);
}

//---------------------------------------------------------------------------

void MultiPlayer::init (void) {

	// operator new does not clear memory and the class has no virtual
	// functions, so start from all zeroes and set the non-zero defaults.
	memset((void*)this, 0, sizeof(MultiPlayer));

	onLAN = true;
	versionStatus = VERSION_STATUS_GOOD;
	commanderID = -1;
	mode = MULTIPLAYER_MODE_NONE;
	isMPlayerGame = true;
	warpFactor = 1.0;
	winningTeam = -1;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		availableCIDs[i] = true;
		playerInfo[i].commanderID = -1;
		playerInfo[i].team = -1;
		playerList[i].commanderID = -1;
		lastSentPlayerInfo[i].commanderID = -1;
		for (long j = 0; j < 3; j++)
			commandersToLoad[i][j] = -1;
		for (long j = 0; j < 12; j++)
			mechData[i][j].objNumber = -1;
		reinforcements[i][0] = -1;
		reinforcements[i][1] = -1;
	}
	for (long i = 0; i < MAX_COLORS; i++)
		colorsCID[i] = -1;

	missionSettings.defaultCBills = 150000;
	missionSettings.resourcePoints = 0;
	missionSettings.timeLimit = -1.0;
	missionSettings.unlimitedAmmo = true;
	missionSettings.maxTeams = MAX_TEAMS;
	missionSettings.maxPlayers = MAX_MC_PLAYERS;
	missionSettings.dropWeight = 300;

	initUpdateFrequencies();
}

//---------------------------------------------------------------------------

long MultiPlayer:: setup (void) {

	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------

void MultiPlayer::initUpdateFrequencies() {

	pingUpdateFrequency = 1.0;
	moverUpdateFrequency = 0.1f;
	turretUpdateFrequency = 0.25f;
	worldUpdateFrequency = 0.1f;
}

//---------------------------------------------------------------------------

void MultiPlayer::setMode (long newMode) {

	mode = newMode;
	if (net) {
		// Loading a mission can stall the game loop for a long time; do not
		// drop peers that are busy loading.
		if ((newMode == MULTIPLAYER_MODE_LOADING) || (newMode == MULTIPLAYER_MODE_LOGISTICS))
			net->setTimeout(90000);
		else
			net->setTimeout(10000);
	}
}

//---------------------------------------------------------------------------

long MultiPlayer::numActivePlayers (void) {

	long n = 0;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && !playerInfo[i].leftSession)
			n++;
	return(n);
}

//---------------------------------------------------------------------------

static void RebuildPlayerList (MultiPlayer* mp) {

	long n = 0;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		mp->playerList[i].player = 0;
		mp->playerList[i].commanderID = -1;
		mp->playerList[i].name[0] = '\0';
	}
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((mp->playerInfo[i].commanderID > -1) && mp->playerInfo[i].player) {
			mp->playerList[n].player = mp->playerInfo[i].player;
			mp->playerList[n].commanderID = (char)i;
			CopyString(mp->playerList[n].name, mp->playerInfo[i].name, MAXLEN_PLAYER_NAME);
			n++;
		}
}

//---------------------------------------------------------------------------

void MultiPlayer::pumpNetwork (unsigned int waitMs) {

	if (pumping || !net || !net->active())
		return;
	pumping = true;
	gosNetEvent ev;
	unsigned int wait = waitMs;
	while (net && net->active() && net->poll(&ev, wait)) {
		wait = 0;
		switch (ev.type) {
			case gosNetEv_Connect:
				handlePeerConnected(ev.peer);
				break;
			case gosNetEv_Disconnect:
				handlePeerDisconnected(ev.peer);
				break;
			case gosNetEv_Receive:
				if (!ev.data.empty())
					processNetMessage(iAmHost ? PeerToNetPlayer(ev.peer) : HOST_NETPLAYER,
						ev.data.data(), (int)ev.data.size());
				break;
			default:
				break;
		}
	}
	pumping = false;
}

//---------------------------------------------------------------------------

long MultiPlayer::update (void) {

	updateSessionScan();
	if (net && net->active()) {
		pumpNetwork(0);
		if (iAmHost)
			refreshDiscoveryInfo();
		syncLobbyState();
		if (isServer() && (mode == MULTIPLAYER_MODE_MISSION))
			updateServer();
		logState();
		if (net)
			net->flush();
	}
	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------
// Session scan (LAN broadcast or one address for direct IP)

long MultiPlayer::beginSessionScan (char* ipAddress, bool persistent) {

	if (!scanner)
		scanner = new GosNetScanner;
	sessionScanningError = false;
	if (!scanner->open()) {
		sessionScanningError = true;
		return(MPLAYER_ERR_NO_CONNECTION);
	}
	numSessions = 0;
	scanBroadcast = !ipAddress || !ipAddress[0];
	CopyString(scanAddress, scanBroadcast ? "" : ipAddress, sizeof(scanAddress));
	sessionScanningPersistent = persistent;
	sessionScanning = true;
	scanQueryTime = 0.0;
	updateSessionScan();
	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------

long MultiPlayer::endSessionScan (void) {

	sessionScanning = false;
	if (scanner)
		scanner->close();
	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------

void MultiPlayer::updateSessionScan (void) {

	if (!sessionScanning || !scanner)
		return;
	double now = NetNow();
	if (now - scanQueryTime >= 1.0) {
		// "host" or "host:port"
		char host[64];
		CopyString(host, scanAddress, sizeof(host));
		unsigned short port = GOS_NET_DEFAULT_PORT;
		char* colon = strchr(host, ':');
		if (colon) {
			*colon = '\0';
			long p = atol(colon + 1);
			if (p > 0 && p < 65536)
				port = (unsigned short)p;
		}
		scanner->query(scanBroadcast ? NULL : host, port);
		NETLOG(2, "scan query %s", scanBroadcast ? "(broadcast)" : host);
		scanQueryTime = now;
	}

	gosNetRoom room;
	while (scanner->poll(&room)) {
		if (room.payload.size() < NET_BEACON_MIN_SIZE)
			continue;
		NetBeacon beacon;
		memset(&beacon, 0, sizeof(beacon));
		memcpy(&beacon, room.payload.data(), (room.payload.size() < sizeof(NetBeacon)) ? room.payload.size() : sizeof(NetBeacon));
		if (memcmp(beacon.magic, "MC2S", 4) != 0)
			continue;
		// The same host can answer twice (LAN broadcast and loopback).
		long index = -1;
		bool sameAddress = false;
		for (long i = 0; i < numSessions; i++)
			if (sessionList[i].handle.Data1 == beacon.sessionId) {
				index = i;
				sameAddress = strcmp(sessionList[i].address, room.address.c_str()) == 0;
			}
		if ((index > -1) && !sameAddress) {
			sessionSeenTime[index] = now;
			continue;
		}
		const bool newRoom = index < 0;
		if (index < 0) {
			if (numSessions >= MAX_SESSIONS)
				continue;
			index = numSessions++;
			NETLOG(1, "found room \"%s\" at %s", beacon.name, room.address.c_str());
		}
		MC2Session& s = sessionList[index];
		memset(&s, 0, sizeof(MC2Session));
		CopyString(s.name, beacon.name, MAXLEN_SESSION_NAME);
		CopyString(s.map, beacon.map, MAXLEN_MAP_NAME);
		s.maxPlayers = (char)beacon.maxPlayers;
		s.numPlayers = (char)beacon.numPlayers;
		s.locked = beacon.locked != 0;
		s.inProgress = beacon.inProgress != 0;
		CopyString(s.versionStamp, beacon.protocol, sizeof(s.versionStamp));
		CopyString(s.address, room.address.c_str(), sizeof(s.address));
		{
			char version[sizeof(beacon.gameVersion) + 1];
			CopyString(version, beacon.gameVersion, sizeof(version));
			CopyString(s.gameVersion, version, sizeof(s.gameVersion));
		}
		// The data hash only depends on the installed data, so once is enough.
		static unsigned int localDataHash = ComputeDataHash();
		s.compatible = (strncmp(beacon.protocol, NetProtocolStamp(), sizeof(beacon.protocol)) == 0)
			&& (beacon.dataHash == localDataHash);
		if (!s.compatible && newRoom)
			NETLOG(1, "room \"%s\" is not compatible (version %s, protocol %.15s)", beacon.name,
				s.gameVersion[0] ? s.gameVersion : "unknown", beacon.protocol);
		s.handle.Data1 = beacon.sessionId;
		s.ping = (int)((now - scanQueryTime) * 1000.0);
		sessionSeenTime[index] = now;
	}

	// Rooms that stopped answering disappear from the LAN list.
	if (scanBroadcast)
		for (long i = numSessions - 1; i >= 0; i--)
			if (now - sessionSeenTime[i] > 4.0) {
				for (long j = i; j < numSessions - 1; j++) {
					sessionList[j] = sessionList[j + 1];
					sessionSeenTime[j] = sessionSeenTime[j + 1];
				}
				numSessions--;
			}
}

//---------------------------------------------------------------------------

bool MPIncompatibleSessionLabel (const MC2Session* session, char* text, int textLen) {

	if (!session || session->compatible)
		return(false);
	char format[256];
	if (session->gameVersion[0] && strcmp(session->gameVersion, NetGameVersion()) != 0) {
		cLoadString(IDS_MP_ROOM_OTHER_VERSION, format, 255);
		snprintf(text, textLen, format, session->gameVersion);
	}
	else if (session->gameVersion[0])
		cLoadString(IDS_MP_ROOM_OTHER_DATA, text, textLen - 1);
	else {
		// A host from before 0.2 does not say which version it runs.
		cLoadString(IDS_MP_ROOM_OTHER_VERSION, format, 255);
		snprintf(text, textLen, format, "< 0.2");
	}
	return(true);
}

bool MPDescribeIncompatibleSession (const MC2Session* session, char* text, int textLen, bool* offerDownload) {

	if (offerDownload)
		*offerDownload = false;
	if (!session || session->compatible)
		return(false);
	char format[512];
	if (session->gameVersion[0] && strcmp(session->gameVersion, NetGameVersion()) != 0) {
		cLoadString(IDS_MP_VERSION_MISMATCH, format, 511);
		snprintf(text, textLen, format, session->gameVersion, NetGameVersion());
		gos_update::Version host, mine;
		const bool hostOk = gos_update::parseVersion(session->gameVersion, &host);
		const bool mineOk = gos_update::parseVersion(NetGameVersion(), &mine);
		char extra[512] = "";
		if (hostOk && (!mineOk || gos_update::compareVersions(host, mine) > 0)) {
			cLoadString(offerDownload ? IDS_MP_VERSION_HOST_NEWER : IDS_MP_VERSION_HOST_NEWER_INFO, format, 511);
			snprintf(extra, sizeof(extra), format, session->gameVersion);
			if (offerDownload)
				*offerDownload = true;
		}
		else if (mineOk) {
			cLoadString(IDS_MP_VERSION_HOST_OLDER, format, 511);
			snprintf(extra, sizeof(extra), format, NetGameVersion());
		}
		if (extra[0]) {
			const size_t used = strlen(text);
			snprintf(text + used, textLen - used, "\n\n%s", extra);
		}
	}
	else if (session->gameVersion[0])
		cLoadString(IDS_MP_DATA_MISMATCH, text, textLen - 1);
	else
		cLoadString(IDS_MP_CONNECTION_ERROR_WRONG_VERSION, text, textLen - 1);
	return(true);
}

//---------------------------------------------------------------------------

MC2Session* MultiPlayer::getSessions (long& sessionCount) {

	updateSessionScan();
	sessionCount = numSessions;
	return(sessionList);
}

//---------------------------------------------------------------------------

void MultiPlayer::refreshDiscoveryInfo (void) {

	if (!net || !iAmHost)
		return;
	NetBeacon beacon;
	memset(&beacon, 0, sizeof(beacon));
	memcpy(beacon.magic, "MC2S", 4);
	CopyString(beacon.protocol, NetProtocolStamp(), sizeof(beacon.protocol));
	CopyString(beacon.name, sessionName, MAXLEN_SESSION_NAME);
	CopyString(beacon.map, missionSettings.name[0] ? missionSettings.name : missionSettings.map, MAXLEN_MAP_NAME);
	beacon.maxPlayers = (unsigned char)missionSettings.maxPlayers;
	beacon.numPlayers = (unsigned char)numActivePlayers();
	beacon.locked = locked ? 1 : 0;
	beacon.inProgress = (inProgress || (mode != MULTIPLAYER_MODE_PARAMETERS)) ? 1 : 0;
	beacon.dataHash = dataHash;
	beacon.sessionId = HostSessionId;
	CopyString(beacon.gameVersion, NetGameVersion(), sizeof(beacon.gameVersion));
	net->setDiscoveryPayload(&beacon, sizeof(beacon));
}

//---------------------------------------------------------------------------

static long ColorIndexOf (long color) {

	for (long i = 0; i < MAX_COLORS; i++)
		if (MultiPlayer::colors[i] == color)
			return(i);
	return(0);
}

static void FillPlayerInfoFromPrefs (MC2Player& info, const char* playerName) {

	memset(&info, 0, sizeof(MC2Player));
	info.commanderID = -1;
	info.team = -1;
	info.dropZone = -1;
	CopyString(info.name, (playerName && playerName[0]) ? playerName : prefs.playerName[0], MAXLEN_PLAYER_NAME);
	CopyString(info.unitName, prefs.unitName[0], MAXLEN_UNIT_NAME);
	CopyString(info.insigniaFile, prefs.insigniaFile, MAXLEN_INSIGNIA_FILE);
	long base = ColorIndexOf(prefs.baseColor);
	info.baseColor[BASECOLOR_PREFERENCE] = (char)base;
	info.baseColor[BASECOLOR_SELF] = (char)base;
	info.baseColor[BASECOLOR_TEAM] = (char)base;
	info.stripeColor = (char)ColorIndexOf(prefs.highlightColor);
	info.faction = (char)((prefs.faction >= 0 && prefs.faction < 4) ? prefs.faction : 0);
	info.checkedIn = true;
}

//---------------------------------------------------------------------------

bool MultiPlayer::hostSession (char* sessionName, char* playerName, long mxPlayers) {

	closeSession();
	if (!net)
		net = new GosNetSession;
	if (!net->host(GOS_NET_DEFAULT_PORT, MAX_MC_PLAYERS - 1))
		return(false);
	NETLOG(1, "hosting \"%s\" on UDP %d", sessionName ? sessionName : "", GOS_NET_DEFAULT_PORT);

	dataHash = ComputeDataHash();
	HostSessionId = (unsigned int)(NetNow() * 1000.0) ^ ((unsigned int)gos_rand() << 16) ^ (unsigned int)gos_rand();
	iAmHost = true;
	inSession = true;
	hostLeft = false;
	hostDroppedOut = false;
	// same limit as the room broadcast, so host and clients show the same name
	CopyString(this->sessionName, sessionName, MAXLEN_SESSION_NAME);
	CopyString(this->playerName, playerName, sizeof(this->playerName));
	CopyString(sessionIPAddress, gos_NetLocalAddress().c_str(), sizeof(sessionIPAddress));
	myPlayer = HOST_NETPLAYER;
	serverPlayer = HOST_NETPLAYER;
	if (mxPlayers > 0 && mxPlayers <= MAX_MC_PLAYERS)
		missionSettings.maxPlayers = (char)mxPlayers;

	commanderID = 0;
	availableCIDs[0] = false;
	FillPlayerInfoFromPrefs(playerInfo[0], playerName);
	playerInfo[0].player = HOST_NETPLAYER;
	playerInfo[0].commanderID = 0;
	CopyString(playerInfo[0].ipAddress, sessionIPAddress, sizeof(playerInfo[0].ipAddress));
	playerInfo[0].cBills = missionSettings.defaultCBills;
	setPlayerTeam(0, 0);
	setNextFreeColor(0);
	RebuildPlayerList(this);
	refreshDiscoveryInfo();
	return(true);
}

//---------------------------------------------------------------------------

void MultiPlayer::updateSessionData (MC2SessionData* sessionData) {

	refreshDiscoveryInfo();
}

//---------------------------------------------------------------------------

void MultiPlayer::setLocked (bool set) {

	locked = set;
	missionSettings.locked = set;
}

//---------------------------------------------------------------------------

void MultiPlayer::setInProgress (bool set) {

	inProgress = set;
	missionSettings.inProgress = set;
}

//---------------------------------------------------------------------------

void MultiPlayer::setCancelled (bool set) {

	cancelled = set;
}

//---------------------------------------------------------------------------

void MultiPlayer::addTeamScore (int teamID, int score) {

	if ((teamID < 0) || (teamID >= MAX_TEAMS))
		return;
	teamScore[teamID] += score;
}

//---------------------------------------------------------------------------

long MultiPlayer::joinSession (MC2Session* session, char* playerName) {

	if (!session || !session->address[0])
		return(MPLAYER_ERR_SESSION_NOT_FOUND);
	closeSession();
	if (!net)
		net = new GosNetSession;
	if (!net->join(session->address, GOS_NET_DEFAULT_PORT))
		return(MPLAYER_ERR_HOST_NOT_FOUND);
	NETLOG(1, "joining %s", session->address);

	dataHash = ComputeDataHash();
	iAmHost = false;
	joinResult = 0;
	CopyString(this->playerName, playerName, sizeof(this->playerName));
	double end = NetNow() + 8.0;
	while ((joinResult == 0) && (NetNow() < end))
		pumpNetwork(10);

	if (joinResult == 1) {
		NETLOG(1, "joined as commander %ld", commanderID);
		inSession = true;
		hostLeft = false;
		hostDroppedOut = false;
		serverPlayer = HOST_NETPLAYER;
		CopyString(sessionIPAddress, session->address, sizeof(sessionIPAddress));
		CopyString(sessionName, session->name, sizeof(sessionName));
		return(MPLAYER_NO_ERR);
	}

	long result = joinResult ? joinResult : MPLAYER_ERR_NO_CONNECTION;
	NETLOG(1, "join failed (%ld)", result);
	net->shutdown();
	joinResult = 0;
	commanderID = -1;
	return(result);
}

//---------------------------------------------------------------------------

long MultiPlayer::closeSession (void) {

	if (net)
		net->shutdown();
	inSession = false;
	iAmHost = false;
	joinResult = 0;
	commanderID = -1;
	myPlayer = 0;
	serverPlayer = 0;
	locked = false;
	inProgress = false;
	cancelled = false;
	missionSettings.locked = false;
	missionSettings.inProgress = false;
	startLogistics = false;
	startLoading = false;
	startMission = false;
	endMission = false;
	missionSetupReceived = false;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		memset(&playerInfo[i], 0, sizeof(MC2Player));
		playerInfo[i].commanderID = -1;
		playerInfo[i].team = -1;
		memset(&lastSentPlayerInfo[i], 0, sizeof(MC2Player));
		lastSentPlayerInfo[i].commanderID = -1;
		availableCIDs[i] = true;
		readyToLoad[i] = false;
		mechDataReceived[i] = false;
		missionDataLoaded[i] = false;
		missionFullySetup[i] = false;
		insigniaList[i] = false;
		pausesUsed[i] = 0;
	}
	for (long i = 0; i < MAX_COLORS; i++)
		colorsCID[i] = -1;
	for (long i = 0; i < MAX_TEAMS; i++)
		teamSeniority[i] = 0;
	RebuildPlayerList(this);
	return(0);
}

//---------------------------------------------------------------------------

void MultiPlayer::leaveSession (void) {

	closeSession();
}

//---------------------------------------------------------------------------

long MultiPlayer::bootPlayer (NETPLAYER bootedPlayer) {

	if (!iAmHost || !net)
		return(MPLAYER_NO_ERR);
	long cid = findPlayer(bootedPlayer);
	if ((cid < 0) || (bootedPlayer == HOST_NETPLAYER))
		return(MPLAYER_NO_ERR);
	MCMSG_LeaveSession msg;
	msg.subType = 1;
	msg.commanderID = (char)cid;
	sendMessage(bootedPlayer, &msg, sizeof(msg), true);
	playerInfo[cid].booted = true;
	net->disconnect(NetPlayerToPeer(bootedPlayer));
	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------
// Loading handshake. Every machine runs Logistics::beginMission; these wait
// (pumping the network) until all players reached the same step.

bool MultiPlayer::waitFor (bool (MultiPlayer::*done)(void), float timeoutSeconds) {

	double end = NetNow() + timeoutSeconds;
	while (!(this->*done)()) {
		if (!net || !net->active() || hostLeft)
			return(false);
		if (NetNow() > end)
			return(false);
		pumpNetwork(5);
		if (net)
			net->flush();
	}
	return(true);
}

bool MultiPlayer::startLoadingReady (void) {

	return(iAmHost || missionSetupReceived);
}

bool MultiPlayer::allMechDataReceived (void) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && !playerInfo[i].leftSession
			&& !mechDataReceived[i])
			return(false);
	return(true);
}

bool MultiPlayer::allMissionDataLoaded (void) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && !playerInfo[i].leftSession
			&& !missionDataLoaded[i])
			return(false);
	return(true);
}

bool MultiPlayer::allMissionFullySetup (void) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && !playerInfo[i].leftSession
			&& !missionFullySetup[i])
			return(false);
	return(true);
}

bool MultiPlayer::waitTillStartLoading (void) {

	return(waitFor(&MultiPlayer::startLoadingReady, 60.0f));
}

//---------------------------------------------------------------------------

bool MultiPlayer::waitTillMechDataReceived (void) {

	return(waitFor(&MultiPlayer::allMechDataReceived, 60.0f));
}

//---------------------------------------------------------------------------

bool MultiPlayer::waitTillMissionLoaded (void) {

	return(waitFor(&MultiPlayer::allMissionDataLoaded, 180.0f));
}

//---------------------------------------------------------------------------

bool MultiPlayer::waitTillMissionSetup (void) {

	bool ok = waitFor(&MultiPlayer::allMissionFullySetup, 120.0f);
	if (ok) {
		startTime = scenarioTime;
		startMission = true;
	}
	return(ok);
}

//---------------------------------------------------------------------------

bool MultiPlayer::waitForSessionEntry (void) {

	return(inSession);
}

//---------------------------------------------------------------------------

bool MultiPlayer::playersReadyToLoad (void) {

		return(allMechDataReceived());
}

//---------------------------------------------------------------------------

bool MultiPlayer::launchBrowser (const char* link) {

	return(false);
}

//---------------------------------------------------------------------------

void MultiPlayer::initParametersScreen (void) {

	MC2Player* info = getPlayerInfo(commanderID);
	if (info)
		info->ready = false;
}

//---------------------------------------------------------------------------

void MultiPlayer::setDefaultPlayerInfo (void) {

	MC2Player* info = getPlayerInfo(commanderID);
	if (!info)
		return;
	NETPLAYER player = info->player;
	char cid = info->commanderID;
	char team = info->team;
	FillPlayerInfoFromPrefs(*info, playerName);
	info->player = player;
	info->commanderID = cid;
	info->team = team;
	info->cBills = missionSettings.defaultCBills;
}

//---------------------------------------------------------------------------
// Colors: baseColor[BASECOLOR_PREFERENCE] is what the player picked,
// baseColor[BASECOLOR_SELF] the unique color they got, and
// baseColor[BASECOLOR_TEAM] the color of their team (the SELF color of the
// team member with the lowest commander id).

static bool ColorTakenByOther (MultiPlayer* mp, long colorIndex, long commanderID) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((i != commanderID) && (mp->playerInfo[i].commanderID > -1) && mp->playerInfo[i].player
			&& (mp->playerInfo[i].baseColor[BASECOLOR_SELF] == colorIndex))
			return(true);
	return(false);
}

long MultiPlayer::setClosestColor (long colorIndex, long commanderID) {

	if ((commanderID < 0) || (commanderID >= MAX_MC_PLAYERS))
		return(colorIndex);
	if ((colorIndex < 0) || (colorIndex >= MAX_COLORS))
		colorIndex = 0;
	long curIndex = colorIndex;
	for (long offset = 0; offset < MAX_COLORS; offset++) {
		long up = (colorIndex + offset) % MAX_COLORS;
		long down = (colorIndex - offset + MAX_COLORS) % MAX_COLORS;
		if (!ColorTakenByOther(this, up, commanderID)) {
			curIndex = up;
			break;
		}
		if (!ColorTakenByOther(this, down, commanderID)) {
			curIndex = down;
			break;
		}
	}
	setColor(curIndex, commanderID);
	return(curIndex);
}

//---------------------------------------------------------------------------

long MultiPlayer::setNextFreeColor (long commanderID) {

	if ((commanderID < 0) || (commanderID >= MAX_MC_PLAYERS))
		return(0);
	return(setClosestColor(playerInfo[commanderID].baseColor[BASECOLOR_PREFERENCE], commanderID));
}

//---------------------------------------------------------------------------

void MultiPlayer::setPlayerBaseColor (long commanderID, long colorIndex) {

	if ((commanderID < 0) || (commanderID >= MAX_MC_PLAYERS))
		return;
	playerInfo[commanderID].baseColor[BASECOLOR_PREFERENCE] = (char)colorIndex;
	setClosestColor(colorIndex, commanderID);
}

//---------------------------------------------------------------------------

void MultiPlayer::recalcTeamColors (void) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		if ((playerInfo[i].commanderID < 0) || !playerInfo[i].player)
			continue;
		playerInfo[i].baseColor[BASECOLOR_TEAM] = playerInfo[i].baseColor[BASECOLOR_SELF];
		for (long j = 0; j < i; j++)
			if ((playerInfo[j].commanderID > -1) && playerInfo[j].player
				&& (playerInfo[j].team == playerInfo[i].team)) {
				playerInfo[i].baseColor[BASECOLOR_TEAM] = playerInfo[j].baseColor[BASECOLOR_SELF];
				break;
			}
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::setPlayerTeam (long commanderID, long teamID) {

	if ((commanderID < 0) || (commanderID >= MAX_MC_PLAYERS) || (teamID < 0) || (teamID >= MAX_TEAMS))
		return;
	playerInfo[commanderID].team = (char)teamID;
	playerInfo[commanderID].teamSelected = (char)teamID;
	playerInfo[commanderID].teamSeniority = ++teamSeniority[teamID];
	recalcTeamColors();
}

//---------------------------------------------------------------------------

void MultiPlayer::setColor (long colorIndex, long commanderID) {

	if ((commanderID < 0) || (commanderID >= MAX_MC_PLAYERS) || (colorIndex < 0) || (colorIndex >= MAX_COLORS))
		return;
	for (long i = 0; i < MAX_COLORS; i++)
		if (colorsCID[i] == commanderID)
			colorsCID[i] = -1;
	colorsCID[colorIndex] = (char)commanderID;
	playerInfo[commanderID].baseColor[BASECOLOR_SELF] = (char)colorIndex;
	recalcTeamColors();
}

//---------------------------------------------------------------------------

MC2Player GetPlayersList[MAX_MC_PLAYERS];

const MC2Player* MultiPlayer::getPlayers (long& playerCount) {

	playerCount = 0;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player)
			GetPlayersList[playerCount++] = playerInfo[i];
	return(GetPlayersList);
}

//---------------------------------------------------------------------------

void MultiPlayer::logMessage (NETMESSAGE* message, bool sent) {
}

//---------------------------------------------------------------------------
// player: 0 or MCGROUP_ALLPLAYERS = every other machine, MCGROUP_SERVER =
// the host, otherwise one player's network id. A client always sends to the
// host, which forwards what others need. Messages are never delivered to the
// sender itself (toSelf is ignored): callers apply their own changes.

void MultiPlayer::sendMessage (NETPLAYER player,
							   void* data,
							   int dataSize,
							   bool guaranteed,
							   bool toSelf) {

	if (!net || !net->active() || (dataSize <= 0))
		return;
	gosNetChannel channel = guaranteed ? gosNet_Reliable : gosNet_Unreliable;
	if (!iAmHost) {
		net->send(0, data, (size_t)dataSize, channel);
		return;
	}
	if ((player == 0) || (player == MCGROUP_ALLPLAYERS) || (player == MCGROUP_INNERSPHERE) || (player == MCGROUP_CLAN))
		net->broadcast(data, (size_t)dataSize, channel);
	else if ((player != MCGROUP_SERVER) && (player != HOST_NETPLAYER))
		net->send(NetPlayerToPeer(player), data, (size_t)dataSize, channel);
}

//---------------------------------------------------------------------------

bool MultiPlayer::hostGame (char* sessionName, char* playerName, long nPlayers) {

	return(hostSession(sessionName, playerName, nPlayers));
}

//---------------------------------------------------------------------------

long MultiPlayer::joinGame (char* ipAddress, char* sessionName, char* playerName) {

	MC2Session session;
	memset(&session, 0, sizeof(session));
	CopyString(session.address, ipAddress, sizeof(session.address));
	CopyString(session.name, sessionName, sizeof(session.name));
	return(joinSession(&session, playerName));
}

//---------------------------------------------------------------------------
// Lobby state sync: the host owns every MC2Player and the mission settings
// and sends whatever changed; a client sends its own MC2Player when the UI
// changed it. This also covers UI code that edits playerInfo directly.

void MultiPlayer::syncLobbyState (void) {

	if (!net || !inSession)
		return;
	if ((mode != MULTIPLAYER_MODE_PARAMETERS) && (mode != MULTIPLAYER_MODE_LOGISTICS)
		&& (mode != MULTIPLAYER_MODE_NONE) && (mode != MULTIPLAYER_MODE_BROWSER))
		return;
	double now = NetNow();
	if (now - lastSyncTime < 0.1)
		return;
	lastSyncTime = now;

	if (iAmHost) {
		for (long i = 0; i < MAX_MC_PLAYERS; i++) {
			if ((playerInfo[i].commanderID < 0) || !playerInfo[i].player)
				continue;
			if (memcmp(&playerInfo[i], &lastSentPlayerInfo[i], sizeof(MC2Player)) != 0)
				sendPlayerUpdate(0, 1, i);
		}
		if (memcmp(&missionSettings, &lastSentMissionSettings, sizeof(MissionSettings)) != 0)
			sendMissionSettingsUpdate(0);
	}
	else if (commanderID > -1) {
		if (memcmp(&playerInfo[commanderID], &lastSentPlayerInfo[commanderID], sizeof(MC2Player)) != 0)
			sendPlayerUpdate(0, 5, commanderID);
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePeerConnected (int peer) {
	NETLOG(1, "peer %d connected", peer);

	if (iAmHost)
		return; // wait for the join request
	// Client connected to the host: ask to join.
	MCMSG_PlayerUpdate msg;
	msg.stage = 0;
	CopyString(msg.versionStamp, NetProtocolStamp(), sizeof(msg.versionStamp));
	msg.dataHash = dataHash;
	FillPlayerInfoFromPrefs(msg.info, playerName);
	sendMessage(MCGROUP_SERVER, &msg, sizeof(msg), true);
	net->flush();
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePeerDisconnected (int peer) {
	NETLOG(1, "peer %d disconnected", peer);

	if (!iAmHost) {
		if (joinResult == 0)
			joinResult = MPLAYER_ERR_NO_CONNECTION;
		else if (inSession) {
			hostLeft = true;
			hostDroppedOut = true;
			inSession = false;
			netPaused = false;
			char text[512];
			char format[256];
			cLoadString(IDS_PLAYER_LEFT, format, 255);
			long host = findPlayer(HOST_NETPLAYER);
			sprintf(text, format, (host > -1) ? playerInfo[host].name : "Host");
			addLocalChat(host, text, true, false);
		}
		return;
	}
	handlePlayerLeftSession(PeerToNetPlayer(peer));
}

//---------------------------------------------------------------------------

void MultiPlayer::hostHandleJoinRequest (NETPLAYER sender, MCMSG_PlayerUpdate* msg) {

	if (findPlayer(sender) > -1)
		return; // duplicate request
	long error = MPLAYER_NO_ERR;
	char stamp[sizeof(msg->versionStamp) + 1];
	CopyString(stamp, msg->versionStamp, sizeof(stamp));
	long freeCID = -1;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if (availableCIDs[i]) {
			freeCID = i;
			break;
		}
	if ((strcmp(stamp, NetProtocolStamp()) != 0) || (msg->dataHash != dataHash))
		error = MPLAYER_ERR_BAD_VERSION;
	else if (locked)
		error = MPLAYER_ERR_SESSION_LOCKED;
	else if (inProgress || (mode != MULTIPLAYER_MODE_PARAMETERS))
		error = MPLAYER_ERR_SESSION_IN_PROGRESS;
	else if ((freeCID < 0) || (numActivePlayers() >= missionSettings.maxPlayers))
		error = MPLAYER_ERR_SESSION_FULL;

	MCMSG_PlayerCID reply;
	if (error != MPLAYER_NO_ERR) {
		NETLOG(1, "refused join from peer %d (%ld)", NetPlayerToPeer(sender), error);
		reply.subType = 1;
		reply.commanderID = (char)error;
		sendMessage(sender, &reply, sizeof(reply), true);
		net->disconnect(NetPlayerToPeer(sender));
		return;
	}

	long cid = freeCID;
	availableCIDs[cid] = false;
	MC2Player& info = playerInfo[cid];
	info = msg->info;
	info.name[MAXLEN_PLAYER_NAME - 1] = '\0';
	info.unitName[MAXLEN_UNIT_NAME - 1] = '\0';
	info.insigniaFile[MAXLEN_INSIGNIA_FILE - 1] = '\0';
	info.player = sender;
	info.commanderID = (char)cid;
	CopyString(info.ipAddress, net->peerAddress(NetPlayerToPeer(sender)).c_str(), sizeof(info.ipAddress));
	info.checkedIn = true;
	info.ready = false;
	info.leftSession = false;
	info.booted = false;
	info.winner = false;
	info.score = info.kills = info.losses = 0;
	info.cBills = missionSettings.defaultCBills;
	if ((info.baseColor[BASECOLOR_PREFERENCE] < 0) || (info.baseColor[BASECOLOR_PREFERENCE] >= MAX_COLORS))
		info.baseColor[BASECOLOR_PREFERENCE] = 0;
	if ((info.stripeColor < 0) || (info.stripeColor >= MAX_COLORS))
		info.stripeColor = 0;
	// first team nobody else is on, so a new game can be launched right away
	long team = 0;
	for (; team < MAX_TEAMS - 1; team++) {
		bool used = false;
		for (long i = 0; i < MAX_MC_PLAYERS; i++)
			if ((i != cid) && (playerInfo[i].commanderID > -1) && playerInfo[i].player && (playerInfo[i].team == team))
				used = true;
		if (!used)
			break;
	}
	setPlayerTeam(cid, team);
	setNextFreeColor(cid);
	RebuildPlayerList(this);

	reply.subType = 0;
	NETLOG(1, "%s joined as commander %ld", info.name, cid);
	reply.commanderID = (char)cid;
	sendMessage(sender, &reply, sizeof(reply), true);
	MCMSG_MissionSettingsUpdate settings;
	settings.missionSettings = missionSettings;
	sendMessage(sender, &settings, sizeof(settings), true);
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player) {
			MCMSG_PlayerUpdate update;
			update.stage = 1;
			update.info = playerInfo[i];
			sendMessage(sender, &update, sizeof(update), true);
		}
	// Everybody else learns about the new player from syncLobbyState().

	char format[256];
	char text[512];
	cLoadString(IDS_MULTIPLAYER_PLAYERJOINED, format, 255);
	sprintf(text, format, info.name);
	addLocalChat(cid, text, true, false);
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerLeftSession (NETPLAYER leftPlayer) {

	long cid = findPlayer(leftPlayer);
	if (cid < 0)
		return;

	char format[256];
	char text[512];
	cLoadString(IDS_PLAYER_LEFT, format, 255);
	sprintf(text, format, playerInfo[cid].name);
	addLocalChat(cid, text, true, false);

	if (iAmHost) {
		MCMSG_LeaveSession msg;
		msg.subType = 0;
		msg.commanderID = (char)cid;
		sendMessage(0, &msg, sizeof(msg), true);
	}

	if ((mode == MULTIPLAYER_MODE_LOADING) || (mode == MULTIPLAYER_MODE_MISSION) || (mode == MULTIPLAYER_MODE_RESULTS)) {
		// Keep the slot (commander ids are fixed once the mission loads).
		playerInfo[cid].leftSession = true;
		playerLeftGame(cid);
	}
	else {
		memset(&playerInfo[cid], 0, sizeof(MC2Player));
		playerInfo[cid].commanderID = -1;
		playerInfo[cid].team = -1;
		memset(&lastSentPlayerInfo[cid], 0, sizeof(MC2Player));
		lastSentPlayerInfo[cid].commanderID = -1;
		availableCIDs[cid] = true;
		for (long i = 0; i < MAX_COLORS; i++)
			if (colorsCID[i] == cid)
				colorsCID[i] = -1;
		recalcTeamColors();
		RebuildPlayerList(this);
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::handleTerminateSession (void) {

	hostLeft = true;
	inSession = false;
}

//---------------------------------------------------------------------------

bool MultiPlayer::isServerMissing (void) {

	return(!iAmHost && (hostLeft || !net || !net->connected(0)));
}

//---------------------------------------------------------------------------
// Chat

void MultiPlayer::addLocalChat (long commanderID, const char* text, bool hideName, bool teamOnly) {

	if (!text || !text[0])
		return;
	if (chatCount >= MAX_STORED_CHATS) {
		for (long i = 0; i < MAX_STORED_CHATS - 1; i++) {
			memcpy(currentChatMessages[i], currentChatMessages[i + 1], MAX_CHAT_LENGTH);
			currentChatMessagePlayerIDs[i] = currentChatMessagePlayerIDs[i + 1];
		}
		chatCount = MAX_STORED_CHATS - 1;
	}
	CopyString(currentChatMessages[chatCount], text, MAX_CHAT_LENGTH);
	long id = (commanderID < 0 ? 0 : commanderID) & 0x00ffffff;
	if (hideName)
		id |= 0x01000000;
	if (teamOnly)
		id |= 0x10000000;
	currentChatMessagePlayerIDs[chatCount] = id;
	chatCount++;
}

//---------------------------------------------------------------------------

void MultiPlayer::getChatMessages( char** buffer, long* playerIDs, long& count )
{
	long n = (chatCount < count) ? chatCount : count;
	for (long i = 0; i < n; i++) {
		buffer[i] = currentChatMessages[i];
		playerIDs[i] = currentChatMessagePlayerIDs[i];
	}
	count = n;
	chatCount = 0;
}

//---------------------------------------------------------------------------

static void AddWhisper(MultiPlayer* mp, long sender, long recipient, const char* text)
{
    char format[128];
    char label[MAX_CHAT_LENGTH + 128];
    if (sender == mp->commanderID) {
        cLoadString(IDS_MP_CHAT_WHISPER_TO, format, sizeof(format) - 1);
        snprintf(label, sizeof(label), format, static_cast<int>(recipient + 1),
                 mp->playerInfo[recipient].name);
    } else {
        cLoadString(IDS_MP_CHAT_WHISPER, label, sizeof(label) - 1);
    }
    // Keep the prefix and bound the payload separately.
    const size_t prefix = strlen(label);
    snprintf(label + prefix, sizeof(label) - prefix, " %s", text);
    mp->addLocalChat(sender, label, false, true);
}

static void SendChatMessage (MultiPlayer* mp, signed char team, const char* text, bool hideName,
                             signed char recipient = -1) {

	if (!text)
		return;
	unsigned char buffer[sizeof(MCMSG_Chat) + MAX_CHAT_LENGTH];
	memset(buffer, 0, sizeof(buffer));
	MCMSG_Chat* msg = (MCMSG_Chat*)buffer;
	msg->type = MCMSG_CHAT;
	msg->allPlayers = (team < 0 && recipient < 0);
	msg->whisperTo = recipient;
	msg->hideName = hideName;
	msg->commanderID = (char)mp->commanderID;
	msg->team = team;
	CopyString(msg->string, text, MAX_CHAT_LENGTH);
	int size = (int)(sizeof(MCMSG_Chat) + strlen(msg->string) + 1);
	if (recipient >= 0) {
        AddWhisper(mp, mp->commanderID, recipient, msg->string);
        mp->sendMessage(mp->playerInfo[recipient].player, msg, size, true);
    } else {
        mp->addLocalChat(mp->commanderID, msg->string, hideName, team >= 0);
        mp->sendMessage(0, msg, size, true);
    }
}

void MultiPlayer::sendChat (NETPLAYER receiver, char team, char* chatMessage) {

	SendChatMessage(this, team, chatMessage, false);
}

//---------------------------------------------------------------------------

bool MultiPlayer::sendWhisper(long recipient, const char* text)
{
    const auto* player = getPlayerInfo(recipient);
    if (!player || !player->player || player->leftSession || recipient == commanderID)
        return false;
    SendChatMessage(this, -1, text, false, static_cast<signed char>(recipient));
    return true;
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerActionChat(NETPLAYER receiver, const char* playerName, unsigned long resID )
{
	char format[256];
	char text[512];
	cLoadString(resID, format, 255);
	snprintf(text, sizeof(text), format, playerName ? playerName : "");
	SendChatMessage(this, -1, text, true);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleChat (NETPLAYER sender, MCMSG_Chat* msg) {

	long cid = msg->commanderID;
	if (iAmHost) {
		// trust the connection, not the message, for who said it
		cid = findPlayer(sender);
		if (cid < 0)
			return;
		msg->commanderID = (char)cid;
	}
    const auto* author = getPlayerInfo(cid);
    if (!author || !author->player)
        return;
    if (msg->whisperTo != -1) {
        const long recipient = msg->whisperTo;
        const auto* target = getPlayerInfo(recipient);
        if (!target || !target->player || target->leftSession || recipient == cid)
            return;
        if (recipient == commanderID)
            AddWhisper(this, cid, recipient, msg->string);
        else if (iAmHost)
            sendMessage(target->player, msg, lastMessageSize, true);
        return;
    }
    if (iAmHost) {
        // A client sends to the host; relay only to the intended audience.
        for (const auto& player : playerInfo)
            if (player.player && !player.leftSession && player.player != HOST_NETPLAYER &&
                player.player != sender && (msg->team < 0 || player.team == msg->team))
                sendMessage(player.player, msg, lastMessageSize, true);
    }
	bool teamOnly = msg->team >= 0;
	MC2Player* me = getPlayerInfo(commanderID);
	if (!teamOnly || (me && (me->team == msg->team)))
		addLocalChat(cid, msg->string, msg->hideName, teamOnly);
}

//---------------------------------------------------------------------------
// Player info, mission settings

void MultiPlayer::sendPlayerInfo (NETPLAYER receiver) {

	sendPlayerUpdate(receiver, 1, commanderID);
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerCID (NETPLAYER receiver, unsigned char subType, char CID) {

	MCMSG_PlayerCID msg;
	msg.subType = subType;
	msg.commanderID = CID;
	sendMessage(receiver, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerUpdate (NETPLAYER receiver, long stage, long newCommanderID) {

	long cid = (newCommanderID < 0) ? commanderID : newCommanderID;
	if ((cid < 0) || (cid >= MAX_MC_PLAYERS) || !net)
		return;
	if (!iAmHost && (cid != commanderID))
		return; // clients may only change their own entry
	MCMSG_PlayerUpdate msg;
	msg.stage = (char)(iAmHost ? 1 : 5);
	msg.senderTime = scenarioTime;
	CopyString(msg.versionStamp, NetProtocolStamp(), sizeof(msg.versionStamp));
	msg.dataHash = dataHash;
	msg.info = playerInfo[cid];
	sendMessage(iAmHost ? receiver : MCGROUP_SERVER, &msg, sizeof(msg), true);
	NETLOG(2, "send player update commander %ld ready %d team %d", cid, (int)msg.info.ready, (int)msg.info.team);
	lastSentPlayerInfo[cid] = playerInfo[cid];
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerUpdate (NETPLAYER sender, MCMSG_PlayerUpdate* msg) {

	if (iAmHost) {
		if (msg->stage == 0) {
			hostHandleJoinRequest(sender, msg);
			return;
		}
		long cid = findPlayer(sender);
		if (cid < 0)
			return;
		// A client changed its own entry; take the fields players may edit.
		MC2Player& info = playerInfo[cid];
		const MC2Player& in = msg->info;
		NETLOG(2, "player update from commander %ld: ready %d away %d team %d cbills %d", cid, (int)in.ready, (int)in.away, (int)in.team, in.cBills);
		CopyString(info.name, in.name, MAXLEN_PLAYER_NAME);
		CopyString(info.unitName, in.unitName, MAXLEN_UNIT_NAME);
		CopyString(info.insigniaFile, in.insigniaFile, MAXLEN_INSIGNIA_FILE);
		info.stripeColor = ((in.stripeColor >= 0) && (in.stripeColor < MAX_COLORS)) ? in.stripeColor : 0;
		info.faction = in.faction;
        info.preparedRevision = in.preparedRevision;
        info.loadoutCost = in.loadoutCost;
        info.away = in.away;
        info.ready = in.ready && in.preparedRevision == missionSettings.preparationRevision &&
            in.loadoutCost >= 0 && in.loadoutCost <= info.cBills;
		if ((in.team >= 0) && (in.team < MAX_TEAMS) && (in.team != info.team))
			setPlayerTeam(cid, in.team);
		if ((in.baseColor[BASECOLOR_PREFERENCE] != info.baseColor[BASECOLOR_PREFERENCE])
			&& (in.baseColor[BASECOLOR_PREFERENCE] >= 0) && (in.baseColor[BASECOLOR_PREFERENCE] < MAX_COLORS))
			setPlayerBaseColor(cid, in.baseColor[BASECOLOR_PREFERENCE]);
		RebuildPlayerList(this);
		// syncLobbyState() sends the result to everybody (sender included,
		// in case the host changed something).
		return;
	}

	long cid = msg->info.commanderID;
	if ((cid < 0) || (cid >= MAX_MC_PLAYERS))
		return;
	playerInfo[cid] = msg->info;
	availableCIDs[cid] = false;
	if (cid == commanderID) {
		lastSentPlayerInfo[cid] = playerInfo[cid];
		myPlayer = playerInfo[cid].player;
	}
	RebuildPlayerList(this);
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerCID (NETPLAYER sender, MCMSG_PlayerCID* msg) {

	if (iAmHost)
		return;
	if (msg->subType == 0) {
		if ((msg->commanderID < 0) || (msg->commanderID >= MAX_MC_PLAYERS)) {
			joinResult = MPLAYER_ERR_NO_CONNECTION;
			return;
		}
		commanderID = msg->commanderID;
		joinResult = 1;
	}
	else {
		joinResult = (msg->commanderID < 0) ? msg->commanderID : MPLAYER_ERR_NO_CONNECTION;
		if (msg->commanderID == MPLAYER_ERR_BAD_VERSION)
			versionStatus = VERSION_STATUS_BAD;
	}
}

//---------------------------------------------------------------------------

void MultiPlayer::sendMissionSettingsUpdate (NETPLAYER receiver) {

	if (!iAmHost)
		return;
	MCMSG_MissionSettingsUpdate msg;
	msg.missionSettings = missionSettings;
	sendMessage(receiver, &msg, sizeof(msg), true);
	lastSentMissionSettings = missionSettings;
}

//---------------------------------------------------------------------------

void MultiPlayer::handleMissionSettingsUpdate (NETPLAYER sender, MCMSG_MissionSettingsUpdate* msg) {

	if (iAmHost)
		return;
	missionSettings = msg->missionSettings;
	missionSettings.map[MAXLEN_MAP_NAME - 1] = '\0';
	missionSettings.name[MAXLEN_MAP_DESC - 1] = '\0';
	missionSettings.url[sizeof(missionSettings.url) - 1] = '\0';
	locked = missionSettings.locked;
	inProgress = missionSettings.inProgress;
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerInfo (NETPLAYER sender, MCMSG_PlayerInfo* msg) {
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerCheckIn (void) {
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerCheckIn (NETPLAYER sender, MCMSG_PlayerCheckIn* msg) {
}

//---------------------------------------------------------------------------

void MultiPlayer::sendPlayerSetup (void) {
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerSetup (NETPLAYER sender, MCMSG_PlayerSetup* msg) {
}

//---------------------------------------------------------------------------
// Insignia files ship with the game data, so only the file name (in
// MC2Player) is synced.

void MultiPlayer::sendPlayerInsignia (char* insigniaFileName, unsigned char* insigniaData, long dataSize) {

	MC2Player* info = getPlayerInfo(commanderID);
	if (info && insigniaFileName)
		CopyString(info->insigniaFile, insigniaFileName, MAXLEN_INSIGNIA_FILE);
}

//---------------------------------------------------------------------------

void MultiPlayer::handlePlayerInsignia (NETPLAYER sender, MCMSG_PlayerInsignia* msg) {
}

//---------------------------------------------------------------------------
// Mission setup handshake (Logistics::beginMission)
//   0  host -> all: commandersToLoad and random seed, start loading
//   1  each -> all: one CompressedMech of the player's force (lastMech ends it)
//   2  each -> all: mission data loaded
//   4  each -> all: mission set up, ready to start
//   6  host -> all: the host launched the game from the parameters screen

static void ApplyMissionSetup (MultiPlayer* mp, long cid, const MCMSG_MissionSetup& msg) {
	NETLOG(1, "mission setup %d from commander %ld", msg.subType, cid);

	switch (msg.subType) {
		case 0:
			mp->randomSeed = msg.randomSeed;
			memcpy(mp->commandersToLoad, msg.commandersToLoad, sizeof(mp->commandersToLoad));
			for (long i = 0; i < MAX_MC_PLAYERS; i++) {
				mp->mechDataReceived[i] = false;
				mp->missionDataLoaded[i] = false;
				mp->missionFullySetup[i] = false;
				for (long j = 0; j < 12; j++)
					mp->mechData[i][j].objNumber = -1;
			}
			mp->missionSetupReceived = true;
			break;
		case 1: {
			if ((cid < 0) || (cid >= MAX_MC_PLAYERS))
				return;
			if (msg.mechData.objNumber > -1) {
				for (long j = 0; j < 12; j++)
					if (mp->mechData[cid][j].objNumber < 0) {
						mp->mechData[cid][j] = msg.mechData;
						mp->mechData[cid][j].commanderID = (char)cid;
						if (mp->mechData[cid][j].numComponents > 50)
							mp->mechData[cid][j].numComponents = 50;
						if (mp->mechData[cid][j].numComponents < 0)
							mp->mechData[cid][j].numComponents = 0;
						break;
					}
			}
			if (msg.mechData.lastMech)
				mp->mechDataReceived[cid] = true;
			break;
		}
		case 2:
			if ((cid >= 0) && (cid < MAX_MC_PLAYERS))
				mp->missionDataLoaded[cid] = true;
			break;
		case 4:
			if ((cid >= 0) && (cid < MAX_MC_PLAYERS))
				mp->missionFullySetup[cid] = true;
			break;
		case 6:
			if (!mp->iAmHost)
				mp->startLoading = true;
			break;
	}
}

void MultiPlayer::sendMissionSetup (NETPLAYER receiver, long subType, CompressedMech* mechData) {

	MCMSG_MissionSetup msg;
	msg.subType = (char)subType;
	if (mechData)
		msg.mechData = *mechData;
	else
		memset(&msg.mechData, 0, sizeof(msg.mechData));
	msg.mechData.commanderID = (char)commanderID;

	if ((subType == 0) || (subType == 6)) {
		if (!iAmHost)
			return;
		if (subType == 0) {
			randomSeed = (int)(gos_rand() | (gos_rand() << 15));
			memcpy(msg.commandersToLoad, commandersToLoad, sizeof(commandersToLoad));
		}
		msg.randomSeed = randomSeed;
		ApplyMissionSetup(this, commanderID, msg);
		sendMessage(0, &msg, sizeof(msg), true);
	}
	else {
		ApplyMissionSetup(this, commanderID, msg);
		sendMessage(iAmHost ? 0 : MCGROUP_SERVER, &msg, sizeof(msg), true);
	}
	if (net)
		net->flush();
}

//---------------------------------------------------------------------------

void MultiPlayer::handleMissionSetup (NETPLAYER sender, MCMSG_MissionSetup* msg) {

	long cid = msg->mechData.commanderID;
	if (iAmHost) {
		if ((msg->subType == 0) || (msg->subType == 6))
			return; // only the host sends these
		cid = findPlayer(sender);
		if (cid < 0)
			return;
		msg->mechData.commanderID = (char)cid;
		ApplyMissionSetup(this, cid, *msg);
		// forward to the other clients
		for (long i = 0; i < MAX_MC_PLAYERS; i++)
			if ((playerInfo[i].commanderID > -1) && playerInfo[i].player
				&& (playerInfo[i].player != sender) && (playerInfo[i].player != HOST_NETPLAYER))
				sendMessage(playerInfo[i].player, msg, sizeof(MCMSG_MissionSetup), true);
		return;
	}
	ApplyMissionSetup(this, cid, *msg);
}

//---------------------------------------------------------------------------

void MultiPlayer::handleLeaveSession (NETPLAYER sender, MCMSG_LeaveSession* msg) {

	if (iAmHost) {
		// A client says goodbye; the disconnect event does the rest.
		long cid = findPlayer(sender);
		if (cid > -1)
			net->disconnect(NetPlayerToPeer(sender));
		return;
	}
	long cid = msg->commanderID;
	if ((cid < 0) || (cid >= MAX_MC_PLAYERS))
		return;
	if (msg->subType == 1) {
		// booted
		playerInfo[cid].leftSession = true;
		playerInfo[cid].booted = true;
		return;
	}
	handlePlayerLeftSession(playerInfo[cid].player);
}

//---------------------------------------------------------------------------

void MultiPlayer::sendLeaveSession (char subType, char commanderID) {

	MCMSG_LeaveSession msg;
	msg.subType = (unsigned char)subType;
	msg.commanderID = commanderID;
	sendMessage(iAmHost ? 0 : MCGROUP_SERVER, &msg, sizeof(msg), true);
}

//---------------------------------------------------------------------------

void MultiPlayer::processMessages (void) {

	pumpNetwork(0);
}

//---------------------------------------------------------------------------

bool MultiPlayer::processGameMessage (NETMESSAGE* msg) {

	if (msg && msg->m_pData && (msg->m_size > 0))
		processNetMessage(msg->m_pPlayer, (const unsigned char*)msg->m_pData, msg->m_size);
	return(true);
}

//---------------------------------------------------------------------------

void MultiPlayer::processNetMessage (NETPLAYER sender, const unsigned char* data, int size) {

	if (!data || (size < 1))
		return;
	// Copy into an aligned buffer: handlers take the packed structures and
	// may write to them (and the chat string needs a terminator).
	static unsigned char buffer[MAX_MSG_SIZE * 8];
	if (size >= (int)sizeof(buffer))
		return;
	memcpy(buffer, data, size);
	buffer[size] = 0;
	void* m = buffer;
	unsigned char type = buffer[0];
	lastMessageSize = size;
	NETLOG(2, "recv type %d size %d from %u", type, size, sender);

	// Before the host accepted us only the join reply matters; on the host,
	// only a join request from an unknown peer.
	if (!iAmHost && (joinResult != 1) && (type != MCMSG_PLAYER_CID))
		return;
	if (iAmHost && (findPlayer(sender) < 0) && !((type == MCMSG_PLAYER_UPDATE)
		&& (size >= (int)sizeof(MCMSG_PlayerUpdate)) && (((MCMSG_PlayerUpdate*)m)->stage == 0)))
		return;

#define NEED(T) if (size < (int)sizeof(T)) return
	switch (type) {
		case MCMSG_CHAT:
			NEED(MCMSG_Chat);
            if (size <= (int)sizeof(MCMSG_Chat) || size > (int)sizeof(MCMSG_Chat) + MAX_CHAT_LENGTH)
                return;
			handleChat(sender, (MCMSG_Chat*)m);
			break;
		case MCMSG_PLAYER_CID:
			NEED(MCMSG_PlayerCID);
			handlePlayerCID(sender, (MCMSG_PlayerCID*)m);
			break;
		case MCMSG_PLAYER_UPDATE:
			NEED(MCMSG_PlayerUpdate);
			handlePlayerUpdate(sender, (MCMSG_PlayerUpdate*)m);
			break;
		case MCMSG_MISSION_SETTINGS_UPDATE:
			NEED(MCMSG_MissionSettingsUpdate);
			handleMissionSettingsUpdate(sender, (MCMSG_MissionSettingsUpdate*)m);
			break;
		case MCMSG_MISSION_SETUP:
			NEED(MCMSG_MissionSetup);
			handleMissionSetup(sender, (MCMSG_MissionSetup*)m);
			break;
		case MCMSG_LEAVE_SESSION:
			NEED(MCMSG_LeaveSession);
			handleLeaveSession(sender, (MCMSG_LeaveSession*)m);
			break;
		case MCMSG_START_MISSION:
			handleStartMission(sender);
			break;
		case MCMSG_END_MISSION:
			NEED(MCMSG_EndMission);
			handleEndMission(sender, (MCMSG_EndMission*)m);
			break;
		case MCMSG_PLAYER_ORDER:
			NEED(MCMSG_PlayerOrder);
			handlePlayerOrder(sender, (MCMSG_PlayerOrder*)m);
			break;
		case MCMSG_HOLD_POSITION:
			NEED(MCMSG_HoldPosition);
			handleHoldPosition(sender, (MCMSG_HoldPosition*)m);
			break;
		case MCMSG_PLAYER_MOVERGROUP:
			NEED(MCMSG_PlayerMoverGroup);
			handlePlayerMoverGroup(sender, (MCMSG_PlayerMoverGroup*)m);
			break;
		case MCMSG_PLAYER_ARTILLERY:
			NEED(MCMSG_PlayerArtillery);
			handlePlayerArtillery(sender, (MCMSG_PlayerArtillery*)m);
			break;
		case MCMSG_REINFORCEMENT:
			NEED(MCMSG_Reinforcement);
			handleReinforcement(sender, (MCMSG_Reinforcement*)m);
			break;
		case MCMSG_MOVER_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_MoverUpdate);
			handleMoverUpdate(sender, (MCMSG_MoverUpdate*)m);
			break;
		case MCMSG_TURRET_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_TurretUpdate);
			handleTurretUpdate(sender, (MCMSG_TurretUpdate*)m);
			break;
		case MCMSG_MOVER_WEAPONFIRE_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_MoverWeaponFireUpdate);
			handleMoverWeaponFireUpdate(sender, (MCMSG_MoverWeaponFireUpdate*)m);
			break;
		case MCMSG_TURRET_WEAPONFIRE_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_TurretWeaponFireUpdate);
			handleTurretWeaponFireUpdate(sender, (MCMSG_TurretWeaponFireUpdate*)m);
			break;
		case MCMSG_MOVER_CRITICAL_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_MoverCriticalUpdate);
			handleMoverCriticalUpdate(sender, (MCMSG_MoverCriticalUpdate*)m);
			break;
		case MCMSG_WEAPONHIT_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_WeaponHitUpdate);
			handleWeaponHitUpdate(sender, (MCMSG_WeaponHitUpdate*)m);
			break;
		case MCMSG_PAUSE:
			handlePause(sender, buffer, size);
			break;
		case MCMSG_WORLD_UPDATE:
			if (iAmHost) return;
			NEED(MCMSG_WorldUpdate);
			handleWorldUpdate(sender, (MCMSG_WorldUpdate*)m);
			break;
		default:
			break;
	}
#undef NEED
}

//---------------------------------------------------------------------------

void MultiPlayer::redistributeRP( )
{
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player)
			playerInfo[i].cBills = missionSettings.defaultCBills;
}

//---------------------------------------------------------------------------

void MultiPlayer::calcDropZones (char dropZonesCID[MAX_MC_PLAYERS], char hqs[MAX_TEAMS]) {

	// Drop zone i goes to the i-th player, teammates next to each other.
	long n = 0;
	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		dropZonesCID[i] = -1;
	for (long team = 0; team < MAX_TEAMS; team++)
		for (long i = 0; i < MAX_MC_PLAYERS; i++)
			if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && (playerInfo[i].team == team))
				dropZonesCID[n++] = (char)i;
	for (long i = 0; i < MAX_TEAMS; i++)
		hqs[i] = (char)i;
}

//---------------------------------------------------------------------------

void MultiPlayer::initStartupParameters (bool fresh) {
}

//---------------------------------------------------------------------------

void MultiPlayer::resetForNewGame (void) {


	startLogistics = false;
	startLoading = false;
	startMission = false;
	endMission = false;
	missionSetupReceived = false;
	for (long i = 0; i < MAX_MC_PLAYERS; i++) {
		playerInfo[i].ready = false;
		mechDataReceived[i] = false;
		missionDataLoaded[i] = false;
		missionFullySetup[i] = false;
	}
}

//---------------------------------------------------------------------------

bool MultiPlayer::allPlayersCheckedIn (void) {

	return(true);
}

//---------------------------------------------------------------------------

bool MultiPlayer::allPlayersReady (void) {

	for (long i = 0; i < MAX_MC_PLAYERS; i++)
		if ((playerInfo[i].commanderID > -1) && playerInfo[i].player && !playerInfo[i].leftSession
            && (!playerInfo[i].ready || playerInfo[i].away || playerInfo[i].preparedRevision != missionSettings.preparationRevision
                || playerInfo[i].loadoutCost < 0 || playerInfo[i].loadoutCost > playerInfo[i].cBills))
			return(false);
	return(true);
}

//---------------------------------------------------------------------------

void MultiPlayer::switchServers (void) {
}

//---------------------------------------------------------------------------

void MultiPlayer::setServer (NETPLAYER player, char playerIPAddress[16]) {

	serverPlayer = player;
	if (playerIPAddress)
		CopyString(sessionIPAddress, playerIPAddress, sizeof(sessionIPAddress));
}

//---------------------------------------------------------------------------

void MultiPlayer::sendNewServer (void) {
}

//---------------------------------------------------------------------------

void MultiPlayer::handleNewServer (NETPLAYER sender, MCMSG_NewServer* msg) {
}

//---------------------------------------------------------------------------

extern char* GetTime();
extern const char *SpecialtySkillsTable[NUM_SPECIALTY_SKILLS];

long MultiPlayer::saveTranscript (const char* fileName, bool debugging) {

	return(MPLAYER_NO_ERR);
}

//---------------------------------------------------------------------------

void MultiPlayer::destroy (void) {

	closeSession();
	endSessionScan();
	delete net;
	net = NULL;
	delete scanner;
	scanner = NULL;
}

//---------------------------------------------------------------------------

void MultiPlayer::calcPlayerRanks (void) {
}

//***************************************************************************
// IN-MISSION SYNC
//***************************************************************************

// The in-mission part (rosters, chunks, orders, server updates) is in
// multplyr_sync.cpp.
