/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// LANAPI.h ///////////////////////////////////////////////////////////////
// LANAPI singleton class - defines interface to LAN broadcast communications
// Author: Matthew D. Campbell, October 2001

#pragma once

#include <mutex>
#include <vector>

#include "GameNetwork/Transport.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/LANPlayer.h"
#include "GameNetwork/LANGameInfo.h"

//static const Int g_lanPlayerNameLength = 20;
static const Int g_lanPlayerNameLength = 12; // reduced length because of game option length
//static const Int g_lanLoginNameLength = 16;
//static const Int g_lanHostNameLength = 16;
static const Int g_lanLoginNameLength = 1;
static const Int g_lanHostNameLength = 1;
//static const Int g_lanGameNameLength = 32;
static const Int g_lanGameNameLength = 16; // reduced length because of game option length
static const Int g_lanGameNameReservedLength = 16; // save N wchars for ID info
static const Int g_lanMaxChatLength = 100;
static const Int m_lanMaxOptionsLength = MAX_LANAPI_PACKET_SIZE - ( 8 + (g_lanGameNameLength+1)*2 + 4 + (g_lanPlayerNameLength+1)*2
																														+ (g_lanLoginNameLength+1) + (g_lanHostNameLength+1) + 4 ); // +4 for sessionID field
static const Int g_maxSerialLength = 23; // including the trailing '\0'

struct LANMessage;

/**
 * The LANAPI class is used to instantiate a singleton which
 * implements the interface to all LAN broadcast communications.
 */
class LANAPIInterface : public SubsystemInterface
{
public:

	virtual ~LANAPIInterface() { };

	virtual void init() = 0;															///< Initialize or re-initialize the instance
	virtual void reset() = 0;															///< reset the logic system
	virtual void update() = 0;														///< update the world

	virtual void setIsActive(Bool isActive ) = 0;								///< Tell TheLAN whether or not the app is active.

	// Possible types of chat messages
	enum ChatType
	{
		LANCHAT_NORMAL = 0,
		LANCHAT_EMOTE,
		LANCHAT_SYSTEM,
	};

	// Request functions generate network traffic
	virtual void RequestLocations() = 0;																				///< Request everybody to respond with where they are
	virtual void RequestGameJoin( LANGameInfo *game, UnsignedInt ip = 0 ) = 0;				///< Request to join a game
	virtual void RequestGameJoinDirectConnect( UnsignedInt ipaddress ) = 0;						///< Request to join a game at an IP address
	virtual void RequestGameLeave() = 0;																				///< Tell everyone we're leaving
	virtual void RequestAccept() = 0;																						///< Indicate we're OK with the game options
	virtual void RequestHasMap() = 0;																						///< Send our map status
	virtual void RequestChat( UnicodeString message, ChatType format ) = 0;						///< Send a chat message
	virtual void RequestGameStart() = 0;																				///< Tell everyone the game is starting
	virtual void RequestGameStartTimer( Int seconds ) = 0;
	virtual void RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip = 0 ) = 0;		///< Change the game options
	virtual void RequestGameCreate( UnicodeString gameName, Bool isDirectConnect ) = 0;	///< Try to host a game
	virtual void RequestGameAnnounce() = 0;																			///< Sound out current game info if host
//	virtual void RequestSlotList() = 0;																					///< Pump out the Slot info.
	virtual void RequestSetName( UnicodeString newName ) = 0;													///< Pick a new name
	virtual void RequestLobbyLeave( Bool forced ) = 0;																///< Announce that we're leaving the lobby
	virtual void ResetGameStartTimer() = 0;

	// Possible result codes passed to On functions
	enum ReturnType
	{
		RET_OK,							// Any function
		RET_TIMEOUT,				// OnGameJoin/Leave/Start, etc
		RET_GAME_FULL,			// OnGameJoin
		RET_DUPLICATE_NAME,	// OnGameJoin
		RET_CRC_MISMATCH,		// OnGameJoin
		RET_SERIAL_DUPE,		// OnGameJoin
		RET_GAME_STARTED,		// OnGameJoin
		RET_GAME_EXISTS,		// OnGameCreate
		RET_GAME_GONE,			// OnGameJoin
		RET_BUSY,						// OnGameCreate/Join/etc if another action is in progress
		RET_UNKNOWN,				// Default message for oddity
	};
	UnicodeString getErrorStringFromReturnType( ReturnType ret );

	// On functions are (generally) the result of network traffic
	virtual void OnGameList( LANGameInfo *gameList ) = 0;																							///< List of games
	virtual void OnPlayerList( LANPlayer *playerList ) = 0;																				///< List of players in the Lobby
	virtual void OnGameJoin( ReturnType ret, LANGameInfo *theGame ) = 0;															///< Did we get in the game?
	virtual void OnPlayerJoin( Int slot, UnicodeString playerName ) = 0;													///< Someone else joined our game (host only; joiners get a slotlist)
	virtual void OnHostLeave() = 0;																													///< Host left the game
	virtual void OnPlayerLeave( UnicodeString player ) = 0;																				///< Someone left the game
	virtual void OnAccept( UnsignedInt playerSessionID, Bool status ) = 0;																///< Someone's accept status changed
	virtual void OnHasMap( UnsignedInt playerSessionID, Bool status ) = 0;																///< Someone's map status changed
	virtual void OnChat( UnicodeString player, UnsignedInt ip,
											 UnicodeString message, ChatType format ) = 0;														///< Chat message from someone
	virtual void OnGameStart() = 0;																													///< The game is starting
	virtual void OnGameStartTimer( Int seconds ) = 0;
	virtual void OnGameOptions( UnsignedInt playerSessionID, Int playerSlot, AsciiString options ) = 0;	///< Someone sent game options
	virtual void OnGameCreate( ReturnType ret ) = 0;																							///< Your game is created
//	virtual void OnSlotList( ReturnType ret, LANGameInfo *theGame ) = 0;															///< Slotlist for a game in setup
	virtual void OnNameChange( UnsignedInt sessionID, UnicodeString newName ) = 0;												///< Someone has morphed

	// Misc utility functions
	virtual LANGameInfo * LookupGame( UnicodeString gameName ) = 0;														///< return a pointer to a game we know about
	virtual LANGameInfo * LookupGameByListOffset( Int offset ) = 0;														///< return a pointer to a game we know about
	virtual LANGameInfo * LookupGameByHost( UnsignedInt hostIP ) = 0;													///< return a pointer to the most recent game associated to the host IP address
	virtual Bool SetLocalIP( UnsignedInt localIP ) = 0;																		///< For multiple NIC machines
	virtual void SetLocalIP( AsciiString localIP ) = 0;																		///< For multiple NIC machines
	virtual Bool AmIHost() = 0;																											///< Am I hosting a game?
	virtual inline UnicodeString GetMyName() = 0;																		///< What's my name?
	virtual inline LANGameInfo *GetMyGame() = 0;															          ///< What's my Game?
	virtual void fillInLANMessage( LANMessage *msg ) = 0;																	///< Fill in default params
	virtual void checkMOTD() = 0;
};


/**
 * LAN message class
 */
#pragma pack(push, 1)
struct LANMessage
{
	LANMessage() { memset(this, 0, sizeof(*this)); }

	enum Type				          ///< What kind of message are we?
	{
		// Locating everybody
		MSG_REQUEST_LOCATIONS,	///< Hey, where is everybody?
		MSG_GAME_ANNOUNCE,			///< Here I am, and here's my game info!
		MSG_LOBBY_ANNOUNCE,			///< Hey, I'm in the lobby!

		// Joining games
		MSG_REQUEST_JOIN,				///< Let me in!  Let me in!
		MSG_JOIN_ACCEPT,				///< Okay, you can join.
		MSG_JOIN_DENY,					///< Go away!  We don't want any!

		// Leaving games
		MSG_REQUEST_GAME_LEAVE,	///< I want to leave the game
		MSG_REQUEST_LOBBY_LEAVE,///< I'm leaving the lobby

		// Game options, chat, etc
		MSG_SET_ACCEPT,					///< I'm cool with everything as is.
		MSG_MAP_AVAILABILITY,		///< I do (not) have the map.
		MSG_CHAT,								///< Just spouting my mouth off.
		MSG_GAME_START,					///< Hold on; we're starting!
		MSG_GAME_START_TIMER,		///< The game will start in N seconds
		MSG_GAME_OPTIONS,				///< Here's some info about the game.
		MSG_INACTIVE,						///< I've alt-tabbed out.  Unaccept me cause I'm a poo-flinging monkey.

		MSG_REQUEST_GAME_INFO,	///< For direct connect, get the game info from a specific IP Address
	} messageType;

	WideChar name[g_lanPlayerNameLength+1]; ///< My name, for convenience
	char userName[g_lanLoginNameLength+1];	///< login name, for convenience
	char hostName[g_lanHostNameLength+1];		///< machine name, for convenience
	UnsignedInt sessionID;									///< Unique session ID per game instance (replaces IP-based identification)

	// No additional data is required for REQUEST_LOCATIONS, LOBBY_ANNOUNCE,
	// REQUEST_LOBBY_LEAVE, GAME_START.
	union
	{
		// StartTimer is sent with GAME_START_TIMER
		struct
		{
			Int seconds;
		} StartTimer;

		// GameJoined is sent with REQUEST_GAME_LEAVE
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
		} GameToLeave;

		// GameInfo if sent with GAME_ANNOUNCE
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			Bool inProgress;
			char options[m_lanMaxOptionsLength+1];
			Bool isDirectConnect;
		} GameInfo;

		// PlayerInfo is sent with REQUEST_GAME_INFO for direct connect games.
		struct
		{
			UnsignedInt ip;
			WideChar playerName[g_lanPlayerNameLength+1];
		} PlayerInfo;

		// GameToJoin is sent with REQUEST_JOIN
		struct
		{
			UnsignedInt gameSessionID;	///< Session ID of the game to join (was gameIP)
			UnsignedInt exeCRC;
			UnsignedInt iniCRC;
			char serial[g_maxSerialLength];
		} GameToJoin;

		// GameJoined is sent with JOIN_ACCEPT
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			UnsignedInt gameSessionID;		///< Session ID of the game (was gameIP)
			UnsignedInt playerSessionID;	///< Session ID of the joining player (was playerIP)
			Int slotPosition;
		} GameJoined;

		// GameNotJoined is sent with JOIN_DENY
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			UnsignedInt gameSessionID;		///< Session ID of the game (was gameIP)
			UnsignedInt playerSessionID;	///< Session ID of the denied player (was playerIP)
			LANAPIInterface::ReturnType reason;
		} GameNotJoined;

		// Accept is sent with SET_ACCEPT
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			Bool isAccepted;
		} Accept;

		// Accept is sent with MAP_AVAILABILITY
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			UnsignedInt mapCRC;	// to make sure we're talking about the same map
			Bool hasMap;
		} MapStatus;

		// Chat is sent with CHAT
		struct
		{
			WideChar gameName[g_lanGameNameLength+1];
			LANAPIInterface::ChatType chatType;
			WideChar message[g_lanMaxChatLength+1];
		} Chat;

		// GameOptions is sent with GAME_OPTIONS
		struct
		{
			char options[m_lanMaxOptionsLength+1];
		} GameOptions;

	};
};
#pragma pack(pop)

static_assert(sizeof(LANMessage) <= MAX_LANAPI_PACKET_SIZE, "LANMessage struct cannot be larger than the max packet size");


/**
 * The LANAPI class is used to instantiate a singleton which
 * implements the interface to all LAN broadcast communications.
 */
class LANAPI : public LANAPIInterface
{
public:

	LANAPI();
	virtual ~LANAPI();

	virtual void init();															///< Initialize or re-initialize the instance
	virtual void reset();															///< reset the logic system
	virtual void update();														///< update the world

	virtual void setIsActive(Bool isActive);								///< tell TheLAN whether or not

	// Request functions generate network traffic
	virtual void RequestLocations();																				///< Request everybody to respond with where they are
	virtual void RequestGameJoin( LANGameInfo *game, UnsignedInt ip = 0 );				///< Request to join a game
	virtual void RequestGameJoinDirectConnect( UnsignedInt ipaddress );						///< Request to join a game at an IP address
	virtual void RequestGameLeave();																				///< Tell everyone we're leaving
	virtual void RequestAccept();																						///< Indicate we're OK with the game options
	virtual void RequestHasMap();																						///< Send our map status
	virtual void RequestChat( UnicodeString message, ChatType format );						///< Send a chat message
	virtual void RequestGameStart();																				///< Tell everyone the game is starting
	virtual void RequestGameStartTimer( Int seconds );
	virtual void RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip = 0 );		///< Change the game options
	virtual void RequestGameCreate( UnicodeString gameName, Bool isDirectConnect );	///< Try to host a game
	virtual void RequestGameAnnounce();																			///< Send out game info if host
	virtual void RequestSetName( UnicodeString newName );													///< Pick a new name
//	virtual void RequestSlotList();																					///< Pump out the Slot info.
	virtual void RequestLobbyLeave( Bool forced );																///< Announce that we're leaving the lobby
	virtual void ResetGameStartTimer();

	// On functions are (generally) the result of network traffic
	virtual void OnGameList( LANGameInfo *gameList );																							///< List of games
	virtual void OnPlayerList( LANPlayer *playerList );																				///< List of players in the Lobby
	virtual void OnGameJoin( ReturnType ret, LANGameInfo *theGame );															///< Did we get in the game?
	virtual void OnPlayerJoin( Int slot, UnicodeString playerName );													///< Someone else joined our game (host only; joiners get a slotlist)
	virtual void OnHostLeave();																													///< Host left the game
	virtual void OnPlayerLeave( UnicodeString player );																				///< Someone left the game
	virtual void OnAccept( UnsignedInt playerSessionID, Bool status );																///< Someone's accept status changed
	virtual void OnHasMap( UnsignedInt playerSessionID, Bool status );																///< Someone's map status changed
	virtual void OnChat( UnicodeString player, UnsignedInt ip,
											 UnicodeString message, ChatType format );														///< Chat message from someone
	virtual void OnGameStart();																													///< The game is starting
	virtual void OnGameStartTimer( Int seconds );
	virtual void OnGameOptions( UnsignedInt playerSessionID, Int playerSlot, AsciiString options );	///< Someone sent game options
	virtual void OnGameCreate( ReturnType ret );																							///< Your game is created
	//virtual void OnSlotList( ReturnType ret, LANGameInfo *theGame );															///< Slotlist for a game in setup
	virtual void OnNameChange( UnsignedInt sessionID, UnicodeString newName );												///< Someone has morphed
	virtual void OnInActive( UnsignedInt sessionID );																								///< Someone has alt-tabbed out.


	// Misc utility functions
	virtual LANGameInfo * LookupGame( UnicodeString gameName );														///< return a pointer to a game we know about
	virtual LANGameInfo * LookupGameByListOffset( Int offset );														///< return a pointer to a game we know about
	virtual LANGameInfo * LookupGameByHost( UnsignedInt hostIP );													///< return a pointer to the most recent game associated to the host IP address
	virtual LANPlayer * LookupPlayer( UnsignedInt playerSessionID );													///< return a pointer to a player we know about
	virtual Bool SetLocalIP( UnsignedInt localIP );																		///< For multiple NIC machines
	virtual void SetLocalIP( AsciiString localIP );																		///< For multiple NIC machines
	virtual Bool AmIHost();																											///< Am I hosting a game?
	virtual UnicodeString GetMyName() { return m_name; }                 ///< What's my name?
	virtual LANGameInfo* GetMyGame() { return m_currentGame; }					      ///< What's my Game?
	virtual UnsignedInt GetLocalIP() { return m_sessionId; }							///< Get local identity (session ID, used for player identification)
	virtual UnsignedInt GetLocalSessionID() { return m_sessionId; }				///< What's my session ID?
	virtual UnsignedInt GetNetworkIP() { return m_localIP; }							///< Get actual network IP (for transport binding and display)
	virtual void fillInLANMessage( LANMessage *msg );																	///< Fill in default params
	virtual void checkMOTD();
protected:

	enum PendingActionType
	{
		ACT_NONE = 0,
		ACT_JOIN,
		ACT_JOINDIRECTCONNECT,
		ACT_LEAVE,
	};

	static const UnsignedInt s_resendDelta; // in ms

protected:
	LANPlayer *					m_lobbyPlayers;			///< List of players in the lobby
	LANGameInfo *				m_games;								///< List of games
	UnicodeString				m_name;							///< Who do we think we are?
	AsciiString					m_userName;						///< login name
	AsciiString					m_hostName;						///< machine name
	UnsignedInt					m_gameStartTime;
	Int									m_gameStartSeconds;

	PendingActionType		m_pendingAction;	///< What action are we performing?
	UnsignedInt					m_expiration;						///< When should we give up on our action?
	UnsignedInt					m_actionTimeout;
	UnsignedInt					m_directConnectRemoteIP;///< The IP address of the game we are direct connecting to.

	// Resend timer ---------------------------------------------------------------------------
	UnsignedInt					m_lastResendTime; // in ms

	Bool								m_isInLANMenu;		///< true while we are in a LAN menu (lobby, game options, direct connect)
	Bool								m_inLobby;											///< Are we in the lobby (not in a game)?
	LANGameInfo *				m_currentGame;							///< Pointer to game (setup screen) we are currently in (null for lobby)

	UnsignedInt					m_localIP;
	UnsignedInt					m_sessionId;		///< Random per-instance ID (replaces IP for identity in relay mode)
	Transport*					m_transport;

	UnsignedInt					m_broadcastAddr;

	UnsignedInt					m_lastUpdate;
	AsciiString					m_lastGameopt; /// @todo: hack for demo - remove this

	Bool								m_isActive;			///< is the game currently active?

	// Relay server connection (replaces UDP broadcast)
	SOCKET							m_relaySocket;				///< TCP socket to relay server
	Bool								m_relayConnected;			///< Are we connected to the relay server?
	char								m_relayRecvBuf[4096];	///< TCP receive buffer (lobby + game packets)
	int									m_relayRecvLen;				///< Bytes in receive buffer

	// Serializes all relaySend* writers so packets from different callers
	// (e.g. lobby MSG_CHAT from the UI thread vs. netcode commands from
	// the game thread) can't interleave bytes on the shared TCP socket.
	std::mutex						m_relaySendMutex;

public:
	// Game packet queue for in-game Transport (TCP relay mode)
	TransportMessage		m_gamePacketQueue[MAX_MESSAGES];
	Bool relaySendGamePacket(const UnsignedByte *data, Int len, UnsignedInt destSessionID = 0);
	Bool relayRecv();
protected:

	// Relay protocol type bytes (must match RelayServer.cs)
	static const char RELAY_TYPE_LOBBY       = 0;
	static const char RELAY_TYPE_GAME        = 1;
	static const char RELAY_TYPE_FILTERCODE  = 2;
	static const char RELAY_TYPE_AUTH        = 3;
	static const char RELAY_TYPE_AUTH_REJECT = 4;
	static const char RELAY_TYPE_GAMERESULT  = 5;
	// Server-pushed player cosmetic state (display name, color, shader id).
	// Broadcast by the relay on AUTH for every connected user and whenever
	// anyone's profile changes via the REST API. Clients cache the payload
	// in CosmeticsCache, keyed by authenticated user id.
	static const char RELAY_TYPE_COSMETICS   = 8;
	// Periodic in-game score events. Built by GameTelemetry from the
	// local player's ScoreKeeper and shipped through the relay so the
	// server can credit per-field deltas onto the User rollup while the
	// match is still being played. Binary payload (see
	// packScoreEventPacket) — no JSON overhead on the hot path. Type
	// byte stays at 9 for wire compatibility; the semantics changed
	// from "totals snapshot" to "deltas-since-last-event".
	static const char RELAY_TYPE_SCORE_EVENTS = 9;
	// Server → client assignment of the match's canonical session GUID.
	// Relay mints one GUID per MSG_GAME_START and broadcasts it to every
	// authenticated peer on the host's filter code (host included); the
	// engine stores the 16 raw bytes as m_sessionId and uses them
	// verbatim on every outgoing SCORE_EVENTS / GAMERESULT for the
	// match. See RelayServer.cs::BuildSessionAssignPacket +
	// GameTelemetry::onRelayAssignSession.
	static const char RELAY_TYPE_SESSION_ASSIGN = 10;
	// Per-minute FPS aggregate shipped by GameTelemetry at every
	// minute roll. Fixed 37-byte payload; see packFpsBucketPacket.
	static const char RELAY_TYPE_FPS_BUCKET = 11;
	// Detailed in-game event batch (object created/destroyed,
	// upgrade / science purchased, superpower fired, …). Variable-
	// length JSON body framed by a small fixed prefix; see
	// packGameEventsPacket. Up to 16 KB body, capped relay-side.
	static const char RELAY_TYPE_GAME_EVENTS = 12;
	// Per-trigger sim-object position snapshot. Variable-length
	// payload carrying a per-object record array. See
	// packPositionSnapshotPacket and GameTelemetry::snapshotPositions.
	static const char RELAY_TYPE_POSITION_SNAPSHOT = 13;

public:
	Bool relayConnect();
	void relayDisconnect();
	Bool relaySendAll(const char *data, int len);
	Bool relaySend(LANMessage *msg);
	// Ship the end-of-game GAMERESULT JSON through the relay's
	// RELAY_TYPE_GAMERESULT channel. Called by GameTelemetry on
	// onGameWon/Lost/Surrendered/Exited.
	Bool relaySendGameResult(const char *json, int len);
	// Ship a periodic in-game score events batch through the relay's
	// RELAY_TYPE_SCORE_EVENTS channel. <buf> is the pre-built binary
	// payload (sessionId/slot/frame/utcMillis + ten Int32 delta fields
	// — see packScoreEventPacket for the layout). Fire-and-forget;
	// mirrors relaySendGameResult's send pattern.
	Bool relaySendScoreEvents(const UnsignedByte *buf, Int len);

	// Pack helpers used by the GameTelemetry sender thread. They
	// construct the framed [4:size][4:sessionID][1:type][payload]
	// bytes for the two telemetry packet types so the sim thread can
	// hand a ready-to-write buffer to the worker queue and the worker
	// thread can call relaySendAll without touching any sim state.
	// Pack is split from send so relaySendAll is only ever invoked
	// from the telemetry thread (the existing wrappers above retain
	// the pack+send shape for any non-telemetry caller).
	void packGameResultPacket(const char *json, int len, std::vector<UnsignedByte> &out);
	void packScoreEventPacket(const UnsignedByte *payload, Int len, std::vector<UnsignedByte> &out);
	void packFpsBucketPacket (const UnsignedByte *payload, Int len, std::vector<UnsignedByte> &out);
	void packGameEventsPacket(const UnsignedByte *payload, Int len, std::vector<UnsignedByte> &out);
	void packPositionSnapshotPacket(const UnsignedByte *payload, Int len, std::vector<UnsignedByte> &out);

protected:
	void sendMessage(LANMessage *msg, UnsignedInt ip = 0); // Convenience function
	void removePlayer(LANPlayer *player);
	void removeGame(LANGameInfo *game);
	void addPlayer(LANPlayer *player);
	void addGame(LANGameInfo *game);
	AsciiString createSlotString();

	// Functions to handle incoming messages -----------------------------------
	void handleRequestLocations( LANMessage *msg, UnsignedInt senderIP );
	void handleGameAnnounce( LANMessage *msg, UnsignedInt senderIP );
	void handleLobbyAnnounce( LANMessage *msg, UnsignedInt senderIP );
	void handleRequestGameInfo( LANMessage *msg, UnsignedInt senderIP );
	void handleRequestJoin( LANMessage *msg, UnsignedInt senderIP );
	void handleJoinAccept( LANMessage *msg, UnsignedInt senderIP );
	void handleJoinDeny( LANMessage *msg, UnsignedInt senderIP );
	void handleRequestGameLeave( LANMessage *msg, UnsignedInt senderIP );
	void handleRequestLobbyLeave( LANMessage *msg, UnsignedInt senderIP );
	void handleSetAccept( LANMessage *msg, UnsignedInt senderIP );
	void handleHasMap( LANMessage *msg, UnsignedInt senderIP );
	void handleChat( LANMessage *msg, UnsignedInt senderIP );
	void handleGameStart( LANMessage *msg, UnsignedInt senderIP );
	void handleGameStartTimer( LANMessage *msg, UnsignedInt senderIP );
	void handleGameOptions( LANMessage *msg, UnsignedInt senderIP );
	void handleInActive( LANMessage *msg, UnsignedInt senderIP );

};
