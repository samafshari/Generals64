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

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/RestClient.h"

#define WIN32_LEAN_AND_MEAN  // only bare bones windows stuff wanted

#include "Common/crc.h"
#include "Common/GameState.h"
#include "Common/Registry.h"
#include "GameNetwork/LANAPI.h"
#include "Common/CosmeticsCache.h"
#include "GameLogic/GameTelemetry.h"
#include "GameNetwork/networkutil.h"
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "GameClient/GameText.h"
#include "GameClient/MapUtil.h"
#include "Common/UserPreferences.h"
#include "GameLogic/GameLogic.h"


static const UnsignedShort lobbyPort = 28910; ///< TCP port for relay server lobby communication

// Relay server host. Empty by default — the launcher (Discombobulator)
// is the single hardcoded gateway to the server and must pass the host
// via -relayserver on the command line. Running the exe directly without
// that argument disables multiplayer but still allows single-player.
// Populated by parseRelayServer in CommandLine.cpp at engine startup.
char g_relayServerHost[256] = "";

// FilterCode for private lobby isolation (set via -filtercode command line).
// Clients with the same FilterCode see each other; others are invisible.
// Default differs per build so ZH and original-Generals players don't share
// the same lobby unless they explicitly opt in via -filtercode.
#if defined(RTS_ZEROHOUR)
char g_relayFilterCode[24] = "ZEROHOUR";
#else
char g_relayFilterCode[24] = "GENERALS";
#endif

AsciiString GetMessageTypeString(UnsignedInt type);

const UnsignedInt LANAPI::s_resendDelta = 10 * 1000;	///< This is how often we announce ourselves to the world
/*
LANGame::LANGame()
{
	m_gameName = L"";

	int player;
	for (player = 0; player < MAX_SLOTS; ++player)
	{
		m_playerName[player] = L"";
		m_playerIP[player]= 0;
		m_playerAccepted[player] = false;
	}
	m_lastHeard = 0;
	m_inProgress = false;
	m_next = nullptr;
}
*/




LANAPI::LANAPI() : m_transport(nullptr)
{
	DEBUG_LOG(("LANAPI::LANAPI() - max game option size is %d, sizeof(LANMessage)=%d, MAX_LANAPI_PACKET_SIZE=%d",
		m_lanMaxOptionsLength, sizeof(LANMessage), MAX_LANAPI_PACKET_SIZE));

	m_lastResendTime = 0;
	m_lobbyPlayers = nullptr;
	m_games = nullptr;
	m_name = L"";
	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_localIP = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;
	m_broadcastAddr = INADDR_BROADCAST;
	m_directConnectRemoteIP = 0;
	m_actionTimeout = 5000; // ms
	m_lastUpdate = 0;
	m_transport = new Transport;
	m_isActive = TRUE;
	m_relaySocket = INVALID_SOCKET;
	m_relayConnected = FALSE;
	m_relayRecvLen = 0;
	for (int i = 0; i < MAX_MESSAGES; ++i)
		m_gamePacketQueue[i].length = 0;

	// Generate a session ID that is unique across processes launched in the same
	// second on the same box. Previously this used rand() seeded from
	// time() ^ PID, which collided for back-to-back launches (play.bat for-loop
	// with sequential PIDs → correlated rand() output → colliding session IDs →
	// instances filtered each other out as "self" in LANAPI::update). Mix in
	// QueryPerformanceCounter (sub-microsecond resolution) so every launch is
	// unambiguously distinct.
	{
		LARGE_INTEGER qpc = {};
		QueryPerformanceCounter(&qpc);
		UnsignedInt pid = (UnsignedInt)GetCurrentProcessId();
		UnsignedInt t   = (UnsignedInt)time(nullptr);
		// Fold the 64-bit QPC into 32 bits and combine with time+PID so
		// the high bits carry wall-clock variation and the low bits carry
		// counter variation.
		UnsignedInt qpcLo = (UnsignedInt)(qpc.QuadPart & 0xFFFFFFFFu);
		UnsignedInt qpcHi = (UnsignedInt)((qpc.QuadPart >> 32) & 0xFFFFFFFFu);
		m_sessionId = qpcLo ^ (qpcHi * 2654435761u) ^ (t * 0x9E3779B1u) ^ pid;
		// Also seed rand() for any other callers in this process.
		srand(t ^ pid ^ qpcLo);
	}
}

LANAPI::~LANAPI()
{
	reset();
	delete m_transport;
}

// ---- Relay server methods ----

Bool LANAPI::relayConnect()
{
	if (m_relayConnected)
		return TRUE;

	// Relay server must have been set via -relayserver on the command
	// line — otherwise the launcher didn't pass it through or the user
	// ran the exe directly. Fail fast so the caller can surface the
	// error instead of stalling on a DNS resolution of an empty string.
	if (g_relayServerHost[0] == '\0')
	{
		DEBUG_LOG(("LANAPI::relayConnect - no relay server host set (launch via Discombobulator or pass -relayserver <host>)"));
		return FALSE;
	}

	// Exchange the 1-minute launch token (l_...) for a 24-hour game token
	// (g_...) the first time we need it. The relay AUTH handshake below
	// reads g_authGameToken directly; without this call it stays empty
	// and the server rejects the connection.
	RestClient::ensureGameToken();

	const char *serverAddr = g_relayServerHost;

	// Initialize Winsock
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	m_relaySocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_relaySocket == INVALID_SOCKET)
	{
		DEBUG_LOG(("LANAPI::relayConnect - Failed to create socket"));
		return FALSE;
	}

	// Set non-blocking for connect
	u_long nonBlocking = 1;
	ioctlsocket(m_relaySocket, FIONBIO, &nonBlocking);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(lobbyPort);
	addr.sin_addr.s_addr = inet_addr(serverAddr);

	if (addr.sin_addr.s_addr == INADDR_NONE)
	{
		// Try DNS resolution
		struct hostent *host = gethostbyname(serverAddr);
		if (host)
			addr.sin_addr = *(struct in_addr *)host->h_addr;
		else
		{
			DEBUG_LOG(("LANAPI::relayConnect - Cannot resolve %s", serverAddr));
			closesocket(m_relaySocket);
			m_relaySocket = INVALID_SOCKET;
			return FALSE;
		}
	}

	connect(m_relaySocket, (struct sockaddr *)&addr, sizeof(addr));

	// Wait up to 3 seconds for connection
	fd_set writefds;
	FD_ZERO(&writefds);
	FD_SET(m_relaySocket, &writefds);
	struct timeval tv = { 3, 0 };

	if (select(0, nullptr, &writefds, nullptr, &tv) > 0)
	{
		// Set back to blocking mode for reliable sends.
		// relayRecv() uses select() to avoid blocking on reads.
		u_long blocking = 0;
		ioctlsocket(m_relaySocket, FIONBIO, &blocking);
		// Disable Nagle's algorithm for low-latency sends
		int nodelay = 1;
		setsockopt(m_relaySocket, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));
		// Short send timeout — game thread must not stall for long
		DWORD sendTimeout = 200;
		setsockopt(m_relaySocket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&sendTimeout, sizeof(sendTimeout));
		// Large buffers to absorb bursts
		int bufSize = 131072;
		setsockopt(m_relaySocket, SOL_SOCKET, SO_SNDBUF, (const char *)&bufSize, sizeof(bufSize));
		setsockopt(m_relaySocket, SOL_SOCKET, SO_RCVBUF, (const char *)&bufSize, sizeof(bufSize));
		m_relayConnected = TRUE;
		m_relayRecvLen = 0;
		DEBUG_LOG(("LANAPI::relayConnect - Connected to relay server %s:%d", serverAddr, lobbyPort));

		// ── AUTH handshake ─────────────────────────────────────────
		// Send AUTH packet first: [4:size][4:sessionID=0][1:type=3][token]
		// The relay requires this before any other packets. If
		// g_authGameToken is empty, the connection will be rejected.
		{
			extern char g_authGameToken[];
			int tokenLen = (int)strlen(g_authGameToken);
			if (tokenLen > 0)
			{
				int pktSize = 4 + 4 + 1 + tokenLen;
				char buf[256];
				memcpy(buf, &pktSize, 4);
				UnsignedInt zero = 0;
				memcpy(buf + 4, &zero, 4);
				buf[8] = RELAY_TYPE_AUTH;
				memcpy(buf + 9, g_authGameToken, tokenLen);
				relaySendAll(buf, pktSize);
				DEBUG_LOG(("LANAPI::relayConnect - Sent AUTH token (%d bytes)", tokenLen));

				// Brief check for AUTH_REJECT (type 4). On success,
				// the server silently proceeds — no explicit ACK.
				fd_set readfds;
				FD_ZERO(&readfds);
				FD_SET(m_relaySocket, &readfds);
				struct timeval authTv = { 2, 0 };
				if (select(0, &readfds, nullptr, nullptr, &authTv) > 0)
				{
					char rejectBuf[256];
					int n = recv(m_relaySocket, rejectBuf, sizeof(rejectBuf), MSG_PEEK);
					if (n >= 9)
					{
						int rejectSize = 0;
						memcpy(&rejectSize, rejectBuf, 4);
						if (rejectSize <= n && rejectSize >= 9 && rejectBuf[8] == RELAY_TYPE_AUTH_REJECT)
						{
							recv(m_relaySocket, rejectBuf, rejectSize, 0);
							DEBUG_LOG(("LANAPI::relayConnect - AUTH rejected by relay server"));
							closesocket(m_relaySocket);
							m_relaySocket = INVALID_SOCKET;
							m_relayConnected = FALSE;
							return FALSE;
						}
					}
				}
			}
			else
			{
				DEBUG_LOG(("LANAPI::relayConnect - WARNING: no auth game token set, relay will reject"));
			}
		}

		// Send FilterCode packet: [4:size][4:sessionID=0][1:type=2][FilterCode]
		{
			int codeLen = (int)strlen(g_relayFilterCode);
			int pktSize = 4 + 4 + 1 + codeLen;
			char buf[64];
			memcpy(buf, &pktSize, 4);
			UnsignedInt zero = 0;
			memcpy(buf + 4, &zero, 4);
			buf[8] = RELAY_TYPE_FILTERCODE;
			memcpy(buf + 9, g_relayFilterCode, codeLen);
			relaySendAll(buf, pktSize);
			DEBUG_LOG(("LANAPI::relayConnect - Sent FilterCode: %s", g_relayFilterCode));
		}

		return TRUE;
	}

	DEBUG_LOG(("LANAPI::relayConnect - Connection to %s:%d timed out", serverAddr, lobbyPort));
	closesocket(m_relaySocket);
	m_relaySocket = INVALID_SOCKET;
	return FALSE;
}

void LANAPI::relayDisconnect()
{
	if (m_relaySocket != INVALID_SOCKET)
	{
		closesocket(m_relaySocket);
		m_relaySocket = INVALID_SOCKET;
	}
	m_relayConnected = FALSE;
	m_relayRecvLen = 0;
}

// Send all bytes on the relay TCP socket reliably (blocking mode).
// Serialized via m_relaySendMutex so concurrent callers (game thread for
// lobby/game packets, GameTelemetry worker thread for telemetry batches)
// can't interleave bytes mid-frame and corrupt the packet stream.
Bool LANAPI::relaySendAll(const char *data, int len)
{
	std::lock_guard<std::mutex> lk(m_relaySendMutex);
	int totalSent = 0;
	while (totalSent < len)
	{
		int sent = send(m_relaySocket, data + totalSent, len - totalSent, 0);
		if (sent == SOCKET_ERROR)
		{
			DEBUG_LOG(("LANAPI::relaySendAll - send failed, error %d", WSAGetLastError()));
			relayDisconnect();
			return FALSE;
		}
		totalSent += sent;
	}
	return TRUE;
}

Bool LANAPI::relaySend(LANMessage *msg)
{
	if (!m_relayConnected)
		return FALSE;

	// Packet format: [4:size][4:sessionID][1:type=0][LANMessage]
	int payloadSize = sizeof(LANMessage);
	int packetSize = 4 + 4 + 1 + payloadSize;

	char packet[1024];
	if (packetSize > (int)sizeof(packet))
		return FALSE;

	memcpy(packet, &packetSize, 4);
	memcpy(packet + 4, &m_sessionId, 4);
	packet[8] = RELAY_TYPE_LOBBY;
	memcpy(packet + 9, msg, payloadSize);

	return relaySendAll(packet, packetSize);
}

Bool LANAPI::relaySendGamePacket(const UnsignedByte *data, Int len, UnsignedInt destSessionID)
{
	if (!m_relayConnected)
		return FALSE;

	// Packet format: [4:size][4:senderSessionID][1:type=1][4:destSessionID][transport packet data]
	int packetSize = 4 + 4 + 1 + 4 + len;

	char packet[2048];
	if (packetSize > (int)sizeof(packet))
		return FALSE;

	memcpy(packet, &packetSize, 4);
	memcpy(packet + 4, &m_sessionId, 4);
	packet[8] = RELAY_TYPE_GAME;
	memcpy(packet + 9, &destSessionID, 4);
	memcpy(packet + 13, data, len);

	return relaySendAll(packet, packetSize);
}

// Pack the framed GAMERESULT bytes into <out>. Layout:
//   [4:size][4:sessionID][1:type=5][json payload]
// Pack vs send is split (vs. the older inline build-and-send) so the
// GameTelemetry sender thread can build off the sim thread and then
// hand the ready buffer to relaySendAll on its own thread.
void LANAPI::packGameResultPacket(const char *json, int len, std::vector<UnsignedByte> &out)
{
	int packetSize = 4 + 4 + 1 + len;
	out.resize((size_t)packetSize);
	memcpy(out.data(),     &packetSize, 4);
	memcpy(out.data() + 4, &m_sessionId, 4);
	out[8] = (UnsignedByte)RELAY_TYPE_GAMERESULT;
	if (len > 0)
		memcpy(out.data() + 9, json, len);
}

// Pack the framed SCORE_EVENTS bytes into <out>. Same header shape as
// packGameResultPacket; the caller owns the payload bytes verbatim.
void LANAPI::packScoreEventPacket(const UnsignedByte *payload, Int len, std::vector<UnsignedByte> &out)
{
	int packetSize = 4 + 4 + 1 + len;
	out.resize((size_t)packetSize);
	memcpy(out.data(),     &packetSize, 4);
	memcpy(out.data() + 4, &m_sessionId, 4);
	out[8] = (UnsignedByte)RELAY_TYPE_SCORE_EVENTS;
	if (len > 0)
		memcpy(out.data() + 9, payload, len);
}

Bool LANAPI::relaySendGameResult(const char *json, int len)
{
	if (!m_relayConnected)
		return FALSE;

	// Wrapper kept for non-telemetry callers — packs + ships in one
	// call. GameTelemetry bypasses this and calls packGameResultPacket
	// + enqueues onto the sender thread instead.
	std::vector<UnsignedByte> packet;
	packGameResultPacket(json, len, packet);
	return relaySendAll((const char *)packet.data(), (int)packet.size());
}

Bool LANAPI::relaySendScoreEvents(const UnsignedByte *buf, Int len)
{
	if (!m_relayConnected)
		return FALSE;

	// Wrapper kept for any non-telemetry caller. GameTelemetry uses
	// packScoreEventPacket + the sender-thread queue directly so
	// relaySendAll is only ever invoked off the sim thread.
	std::vector<UnsignedByte> packet;
	packScoreEventPacket(buf, len, packet);
	return relaySendAll((const char *)packet.data(), (int)packet.size());
}

Bool LANAPI::relayRecv()
{
	if (!m_relayConnected)
		return FALSE;

	// Check if data is available before reading (socket is blocking)
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(m_relaySocket, &readfds);
	struct timeval tv = { 0, 0 }; // non-blocking poll

	if (select(0, &readfds, nullptr, nullptr, &tv) > 0)
	{
		int space = (int)sizeof(m_relayRecvBuf) - m_relayRecvLen;
		if (space > 0)
		{
			int n = recv(m_relaySocket, m_relayRecvBuf + m_relayRecvLen, space, 0);
			if (n > 0)
				m_relayRecvLen += n;
			else if (n == 0)
			{
				// Server closed connection
				relayDisconnect();
				return FALSE;
			}
			else
			{
				relayDisconnect();
				return FALSE;
			}
		}
	}

	// Process complete packets from buffer
	// Packet format: [4:size][4:sessionID][1:type][payload]
	Bool gotMessage = FALSE;
	while (m_relayRecvLen >= 9)
	{
		int packetSize;
		memcpy(&packetSize, m_relayRecvBuf, 4);

		if (packetSize < 9 || packetSize > 2048)
		{
			// Bad packet - disconnect
			relayDisconnect();
			return FALSE;
		}

		if (m_relayRecvLen < packetSize)
			break; // Need more data

		// Extract sender session ID and type
		UnsignedInt senderSessionID;
		memcpy(&senderSessionID, m_relayRecvBuf + 4, 4);
		char type = m_relayRecvBuf[8];
		int payloadSize = packetSize - 9;

		if (type == RELAY_TYPE_LOBBY)
		{
			// Lobby LANMessage - put in lobby transport inBuffer
			if (payloadSize > 0 && payloadSize <= (int)sizeof(LANMessage))
			{
				for (int i = 0; i < MAX_MESSAGES; ++i)
				{
					if (m_transport->m_inBuffer[i].length == 0)
					{
						memcpy(m_transport->m_inBuffer[i].data, m_relayRecvBuf + 9, payloadSize);
						m_transport->m_inBuffer[i].length = payloadSize;
						m_transport->m_inBuffer[i].addr = senderSessionID;
						m_transport->m_inBuffer[i].port = lobbyPort;
						gotMessage = TRUE;
						break;
					}
				}
			}
		}
		else if (type == RELAY_TYPE_GAME)
		{
			// Game packet format: [4:destSessionID][transport data...]
			// The relay server routes by destSessionID, but we also filter client-side.
			if (payloadSize > 4)
			{
				UnsignedInt destSessionID;
				memcpy(&destSessionID, m_relayRecvBuf + 9, 4);

				// Only accept packets addressed to us (or broadcast with dest=0)
				if (destSessionID != 0 && destSessionID != m_sessionId)
				{
					// Not for us — skip
				}
				else
				{
					int gameDataSize = payloadSize - 4;
					if (gameDataSize > 0 && gameDataSize <= MAX_NETWORK_MESSAGE_LEN)
					{
						for (int i = 0; i < MAX_MESSAGES; ++i)
						{
							if (m_gamePacketQueue[i].length == 0)
							{
								memcpy(&m_gamePacketQueue[i], m_relayRecvBuf + 13, gameDataSize);
								m_gamePacketQueue[i].length = gameDataSize - (int)sizeof(TransportMessageHeader);
								m_gamePacketQueue[i].addr = senderSessionID;
								m_gamePacketQueue[i].port = 0;
								gotMessage = TRUE;
								break;
							}
						}
					}
				}
			}
		}
		else if (type == RELAY_TYPE_COSMETICS)
		{
			// Wire layout after the 9-byte header:
			//   [4:userId][1:nameLen][N:displayName UTF-8]
			//   [1:colorLen][M:color UTF-8 hex (may be empty)]
			//   [4:shaderId]
			if (payloadSize >= 4 + 1 + 1 + 4)
			{
				const char* p = m_relayRecvBuf + 9;
				const char* pEnd = m_relayRecvBuf + 9 + payloadSize;
				Int userId = 0;
				memcpy(&userId, p, 4); p += 4;

				unsigned char nameLen = (unsigned char)*p; p += 1;
				AsciiString name;
				if (p + nameLen <= pEnd)
				{
					char nameBuf[256] = { 0 };
					memcpy(nameBuf, p, nameLen);
					nameBuf[nameLen] = '\0';
					name = nameBuf;
					p += nameLen;
				}

				if (p + 1 <= pEnd)
				{
					unsigned char colorLen = (unsigned char)*p; p += 1;
					UnsignedInt colorPacked = 0;
					if (p + colorLen <= pEnd && colorLen == 6)
					{
						// Parse 6-char hex into 0x00RRGGBB.
						char hex[7] = { 0 };
						memcpy(hex, p, 6);
						hex[6] = '\0';
						UnsignedInt v = 0;
						sscanf_s(hex, "%x", &v);
						colorPacked = v & 0x00FFFFFFu;
					}
					p += colorLen;

					Int shaderId = 0;
					if (p + 4 <= pEnd)
					{
						memcpy(&shaderId, p, 4);
					}

					PlayerCosmetic c;
					c.displayName = name;
					c.colorPacked = colorPacked;
					c.shaderId    = shaderId;
					CosmeticsCache::Instance().Set(userId, c);
				}
			}
		}
		else if (type == RELAY_TYPE_SESSION_ASSIGN)
		{
			// Fixed-width payload: 16 raw GUID bytes (no .NET
			// little-endian reshuffle — byte order matches the
			// textual "N" hex form). Hand straight to GameTelemetry
			// which hex-encodes into m_sessionId and unblocks the
			// SCORE_EVENTS / GAMERESULT sender paths. Silently drop
			// malformed / short payloads; a bogus ASSIGN isn't worth
			// tearing the connection down for.
			if (payloadSize == 16 && TheGameTelemetry)
			{
				TheGameTelemetry->onRelayAssignSession(
					(const UnsignedByte*)(m_relayRecvBuf + 9));
			}
		}

		// Remove processed packet from buffer
		int remaining = m_relayRecvLen - packetSize;
		if (remaining > 0)
			memmove(m_relayRecvBuf, m_relayRecvBuf + packetSize, remaining);
		m_relayRecvLen = remaining;
	}

	return gotMessage;
}

// ---- End relay server methods ----

void LANAPI::init()
{
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;

	// Connect to relay server instead of binding UDP. If this fails, we must
	// NOT fall back to binding the well-known UDP lobby port on the local NIC:
	// only one process on the box can hold that port, so the 2nd..Nth launched
	// instances would silently error out with a "socket error" dialog. Instead,
	// leave relay-mode semantics in effect (port 0, ephemeral) and let the
	// caller surface the failure via LANSocketErrorDetected.
	if (!relayConnect())
	{
		DEBUG_LOG(("LANAPI::init - relayConnect() failed; staying in relay-mode fallback (no UDP lobby bind)"));
	}

	// Still init the transport for the inBuffer/outBuffer structures
	// but we won't use it for actual UDP send/recv in lobby.
	// Use port 0 unconditionally so running multiple game instances on the same
	// machine never fights over the well-known lobby port.
	m_transport->reset();
	m_transport->init((UnsignedInt)0, (UnsignedShort)0);

	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;
	m_directConnectRemoteIP = 0;

	m_lastGameopt = "";

#if TELL_COMPUTER_IDENTITY_IN_LAN_LOBBY
	char userName[UNLEN + 1];
	DWORD bufSize = ARRAY_SIZE(userName);
	if (GetUserNameA(userName, &bufSize))
	{
		m_userName.set(userName, bufSize - 1);
	}
	else
	{
		m_userName = "unknown";
	}

	char computerName[MAX_COMPUTERNAME_LENGTH + 1];
	bufSize = ARRAY_SIZE(computerName);
	if (GetComputerNameA(computerName, &bufSize))
	{
		m_hostName.set(computerName, bufSize - 1);
	}
	else
	{
		m_hostName = "unknown";
	}
#endif
}

void LANAPI::reset()
{
	if (m_inLobby)
	{
		LANMessage msg;
		fillInLANMessage( &msg );
		msg.messageType = LANMessage::MSG_REQUEST_LOBBY_LEAVE;
		sendMessage(&msg);
	}
	m_transport->update();

	LANGameInfo *theGame = m_games;
	LANGameInfo *deletableGame = nullptr;

	while (theGame)
	{
		deletableGame = theGame;
		theGame = theGame->getNext();
		delete deletableGame;
	}

	LANPlayer *thePlayer = m_lobbyPlayers;
	LANPlayer *deletablePlayer = nullptr;

	while (thePlayer)
	{
		deletablePlayer = thePlayer;
		thePlayer = thePlayer->getNext();
		delete deletablePlayer;
	}

	m_games = nullptr;
	m_lobbyPlayers = nullptr;
	m_directConnectRemoteIP = 0;
	m_pendingAction = ACT_NONE;
	m_expiration = 0;
	m_inLobby = true;
	m_isInLANMenu = TRUE;
	m_currentGame = nullptr;

	relayDisconnect();
}

void LANAPI::sendMessage(LANMessage *msg, UnsignedInt ip /* = 0 */)
{
	// Send via relay server - it broadcasts to all other connected clients.
	// The server acts as the virtual LAN, so all messages go through it
	// regardless of whether they were originally broadcast or directed.
	relaySend(msg);
}


AsciiString GetMessageTypeString(UnsignedInt type)
{
	AsciiString returnString;

	switch (type)
	{
		case LANMessage::MSG_REQUEST_LOCATIONS:
			returnString.format( "Request Locations (%d)",type);
			break;
		case LANMessage::MSG_GAME_ANNOUNCE:
			returnString.format("Game Announce (%d)",type);
			break;
		case LANMessage::MSG_LOBBY_ANNOUNCE:
			returnString.format("Lobby Announce (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_JOIN:
			returnString.format("Request Join (%d)",type);
			break;
		case LANMessage::MSG_JOIN_ACCEPT:
			returnString.format("Join Accept (%d)",type);
			break;
		case LANMessage::MSG_JOIN_DENY:
			returnString.format("Join Deny (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_GAME_LEAVE:
			returnString.format("Request Game Leave (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_LOBBY_LEAVE:
			returnString.format("Request Lobby Leave (%d)",type);
			break;
		case LANMessage::MSG_SET_ACCEPT:
			returnString.format("Set Accept(%d)",type);
			break;
		case LANMessage::MSG_CHAT:
			returnString.format("Chat (%d)",type);
			break;
		case LANMessage::MSG_GAME_START:
			returnString.format("Game Start (%d)",type);
			break;
		case LANMessage::MSG_GAME_START_TIMER:
			returnString.format("Game Start Timer (%d)",type);
			break;
		case LANMessage::MSG_GAME_OPTIONS:
			returnString.format("Game Options (%d)",type);
			break;
		case LANMessage::MSG_REQUEST_GAME_INFO:
			returnString.format("Request GameInfo (%d)", type);
			break;
		case LANMessage::MSG_INACTIVE:
			returnString.format("Inactive (%d)", type);
			break;
		default:
			returnString.format("Unknown Message (%d)",type);
	}
	return returnString;
}


void LANAPI::checkMOTD()
{
#if defined(RTS_DEBUG)
	if (TheGlobalData->m_useLocalMOTD)
	{
		// for a playtest, let's log some play statistics, eh?
		if (TheGlobalData->m_playStats <= 0)
			TheWritableGlobalData->m_playStats = 30;

		static UnsignedInt oldMOTDCRC = 0;
		UnsignedInt newMOTDCRC = 0;
		AsciiString asciiMOTD;
		char buf[4096];
		FILE *fp = fopen(TheGlobalData->m_MOTDPath.str(), "r");
		Int len;
		if (fp)
		{
			while( (len = fread(buf, 1, 4096, fp)) > 0 )
			{
				buf[len] = 0;
				asciiMOTD.concat(buf);
			}
			fclose(fp);
			CRC crcObj;
			crcObj.computeCRC(asciiMOTD.str(), asciiMOTD.getLength());
			newMOTDCRC = crcObj.get();
		}

		if (oldMOTDCRC != newMOTDCRC)
		{
			// different MOTD... display it
			oldMOTDCRC = newMOTDCRC;
			AsciiString line;
			while (asciiMOTD.nextToken(&line, "\n"))
			{
				if (line.getCharAt(line.getLength()-1) == '\r')
					line.removeLastChar();	// there is a trailing '\r'

				if (line.isEmpty())
				{
					line = " ";
				}

				UnicodeString uniLine;
				uniLine.translate(line);
				OnChat( L"MOTD", 0, uniLine, LANCHAT_SYSTEM );
			}
		}
	}
#endif
}

extern Bool LANbuttonPushed;
extern Bool LANSocketErrorDetected;
void LANAPI::update()
{
	if(LANbuttonPushed)
		return;
	static const UnsignedInt LANAPIUpdateDelay = 200;
	UnsignedInt now = timeGetTime();

	if( now > m_lastUpdate + LANAPIUpdateDelay)
	{
		m_lastUpdate = now;
	}
	else
	{
		return;
	}

	// Receive messages from relay server
	if (!m_relayConnected)
	{
		// Try to reconnect
		relayConnect();
	}
	relayRecv();

	// Handle any new messages
	int i;
	for (i=0; i<MAX_MESSAGES && !LANbuttonPushed; ++i)
	{
		if (m_transport->m_inBuffer[i].length > 0)
		{
			// Process the new message
			UnsignedInt senderIP = m_transport->m_inBuffer[i].addr;
			LANMessage *msg = (LANMessage *)(m_transport->m_inBuffer[i].data);
			if (msg->sessionID == m_sessionId)
			{
				m_transport->m_inBuffer[i].length = 0;
				continue;
			}
			//DEBUG_LOG(("LAN message type %s from %ls (%s@%s)", GetMessageTypeString(msg->messageType).str(),
			//	msg->name, msg->userName, msg->hostName));
			switch (msg->messageType)
			{
				// Location specification
			case LANMessage::MSG_REQUEST_LOCATIONS:		// Hey, where is everybody?
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_LOCATIONS from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestLocations( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_ANNOUNCE:				// Here someone is, and here's his game info!
				DEBUG_LOG(("LANAPI::update - got a MSG_GAME_ANNOUNCE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleGameAnnounce( msg, senderIP );
				break;
			case LANMessage::MSG_LOBBY_ANNOUNCE:			// Hey, I'm in the lobby!
				DEBUG_LOG(("LANAPI::update - got a MSG_LOBBY_ANNOUNCE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleLobbyAnnounce( msg, senderIP );
				break;
			case LANMessage::MSG_REQUEST_GAME_INFO:
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_GAME_INFO from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestGameInfo( msg, senderIP );
				break;

				// Joining games
			case LANMessage::MSG_REQUEST_JOIN:				// Let me in!  Let me in!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_JOIN from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestJoin( msg, senderIP );
				break;
			case LANMessage::MSG_JOIN_ACCEPT:					// Okay, you can join.
				DEBUG_LOG(("LANAPI::update - got a MSG_JOIN_ACCEPT from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleJoinAccept( msg, senderIP );
				break;
			case LANMessage::MSG_JOIN_DENY:						// Go away!  We don't want any!
				DEBUG_LOG(("LANAPI::update - got a MSG_JOIN_DENY from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleJoinDeny( msg, senderIP );
				break;

				// Leaving games, lobby
			case LANMessage::MSG_REQUEST_GAME_LEAVE:				// I'm outa here!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_GAME_LEAVE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestGameLeave( msg, senderIP );
				break;
			case LANMessage::MSG_REQUEST_LOBBY_LEAVE:				// I'm outa here!
				DEBUG_LOG(("LANAPI::update - got a MSG_REQUEST_LOBBY_LEAVE from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleRequestLobbyLeave( msg, senderIP );
				break;

				// Game options, chat, etc
			case LANMessage::MSG_SET_ACCEPT:					// I'm cool with everything as is.
				handleSetAccept( msg, senderIP );
				break;
			case LANMessage::MSG_MAP_AVAILABILITY:		// Map status
				handleHasMap( msg, senderIP );
				break;
			case LANMessage::MSG_CHAT:								// Just spouting my mouth off.
				handleChat( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_START:					// Hold on; we're starting!
				handleGameStart( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_START_TIMER:
				handleGameStartTimer( msg, senderIP );
				break;
			case LANMessage::MSG_GAME_OPTIONS:				// Here's some info about the game.
				DEBUG_LOG(("LANAPI::update - got a MSG_GAME_OPTIONS from %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(senderIP)));
				handleGameOptions( msg, senderIP );
				break;
			case LANMessage::MSG_INACTIVE:		// someone is telling us that we're inactive.
				handleInActive( msg, senderIP );
				break;

			default:
				DEBUG_LOG(("Unknown LAN message type %d", msg->messageType));
			}

			// Mark it as read
			m_transport->m_inBuffer[i].length = 0;
		}
	}
	if(LANbuttonPushed)
		return;
	// Send out periodic I'm Here messages
	if (now > s_resendDelta + m_lastResendTime)
	{
		m_lastResendTime = now;

		if (m_inLobby)
		{
			RequestSetName(m_name);
		}
		else if (m_currentGame && !m_currentGame->isGameInProgress())
		{
			if (AmIHost())
			{
				RequestGameOptions( GenerateGameOptionsString(), true );
				RequestGameAnnounce();
			}
			else
			{
#if TELL_COMPUTER_IDENTITY_IN_LAN_LOBBY
				AsciiString text;
				text.format("User=%s", m_userName.str());
				RequestGameOptions( text, true );
				text.format("Host=%s", m_hostName.str());
				RequestGameOptions( text, true );
#endif
				RequestGameOptions( "HELLO", false );
			}
		}
		else if (m_currentGame)
		{
			// game is in progress - RequestGameAnnounce will check if we should send it
			RequestGameAnnounce();
		}
	}

	Bool playerListChanged = false;
	Bool gameListChanged = false;

	// Weed out people we haven't heard from in a while
	LANPlayer *player = m_lobbyPlayers;
	while (player)
	{
		if (player->getLastHeard() + s_resendDelta*2 < now)
		{
			// He's gone!
			removePlayer(player);
			LANPlayer *nextPlayer = player->getNext();
			delete player;
			player = nextPlayer;
			playerListChanged = true;
		}
		else
		{
			player = player->getNext();
		}
	}

	// Weed out people we haven't heard from in a while
	LANGameInfo *game = m_games;
	while (game)
	{
		if (game != m_currentGame && game->getLastHeard() + s_resendDelta*2 < now)
		{
			// He's gone!
			removeGame(game);
			LANGameInfo *nextGame = game->getNext();
			delete game;
			game = nextGame;
			gameListChanged = true;
		}
		else
		{
			game = game->getNext();
		}
	}
	if ( m_currentGame && !m_currentGame->isGameInProgress() )
	{
		if ( !AmIHost() && (m_currentGame->getLastHeard() + s_resendDelta*16 < now) )
		{
			// We haven't heard from the host in a while.  Bail.
			// Actually, fake a host leaving message. :)
			LANMessage msg;
			fillInLANMessage( &msg );
			msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
			msg.sessionID = m_currentGame->getSessionID(0);
			wcslcpy(msg.name, m_currentGame->getPlayerName(0).str(), ARRAY_SIZE(msg.name));
			handleRequestGameLeave(&msg, m_currentGame->getIP(0));
			UnicodeString text;
			text = TheGameText->fetch("LAN:HostNotResponding");
			OnChat(UnicodeString::TheEmptyString, m_sessionId, text, LANCHAT_SYSTEM);
		}
		else if ( AmIHost() )
		{
			// Check each player for timeouts
			for (int p=1; p<MAX_SLOTS; ++p)
			{
				if (m_currentGame->getIP(p) && m_currentGame->getPlayerLastHeard(p) + s_resendDelta*8 < now)
				{
					LANMessage msg;
					fillInLANMessage( &msg );
					UnicodeString theStr;
					theStr.format(TheGameText->fetch("LAN:PlayerDropped"), m_currentGame->getPlayerName(p).str());
					msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
					msg.sessionID = m_currentGame->getSessionID(p);
					wcslcpy(msg.name, m_currentGame->getPlayerName(p).str(), ARRAY_SIZE(msg.name));
					handleRequestGameLeave(&msg, m_currentGame->getIP(p));
					OnChat(UnicodeString::TheEmptyString, m_sessionId, theStr, LANCHAT_SYSTEM);
				}
			}
		}
	}

	if (playerListChanged)
	{
		OnPlayerList(m_lobbyPlayers);
	}

	if (gameListChanged)
	{
		OnGameList(m_games);
	}

	// Time out old actions
	if (m_pendingAction != ACT_NONE && now > m_expiration)
	{
		switch (m_pendingAction)
		{
		case ACT_JOIN:
			OnGameJoin(RET_TIMEOUT, nullptr);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		case ACT_LEAVE:
			OnPlayerLeave(m_name);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		case ACT_JOINDIRECTCONNECT:
			OnGameJoin(RET_TIMEOUT, nullptr);
			m_pendingAction = ACT_NONE;
			m_currentGame = nullptr;
			m_inLobby = true;
			break;
		default:
			m_pendingAction = ACT_NONE;
		}
	}

	// send out "game starting" messages
	if ( m_gameStartTime && m_gameStartSeconds && m_gameStartTime <= now )
	{
		// m_gameStartTime is when the next message goes out
		// m_gameStartSeconds is how many seconds remain in the message

		RequestGameStartTimer( m_gameStartSeconds );
	}
	else if (m_gameStartTime && m_gameStartTime <= now)
	{
//		DEBUG_LOG(("m_gameStartTime=%d, now=%d, m_gameStartSeconds=%d", m_gameStartTime, now, m_gameStartSeconds));
		ResetGameStartTimer();
		RequestGameStart();
	}

	// Check for an MOTD every few seconds
	static UnsignedInt lastMOTDCheck = 0;
	static const UnsignedInt motdInterval = 30000;
	if (now > lastMOTDCheck + motdInterval)
	{
		checkMOTD();
		lastMOTDCheck = now;
	}
}

// Request functions generate network traffic
void LANAPI::RequestLocations()
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_LOCATIONS;
	fillInLANMessage( &msg );
	sendMessage(&msg);
}

void LANAPI::RequestGameJoin( LANGameInfo *game, UnsignedInt ip /* = 0 */ )
{
	if ((m_pendingAction != ACT_NONE) && (m_pendingAction != ACT_JOINDIRECTCONNECT))
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}

	if (!game)
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}

	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_JOIN;
	fillInLANMessage( &msg );
	msg.GameToJoin.gameSessionID = game->getSlot(0)->getSessionID();
	msg.GameToJoin.exeCRC = TheGlobalData->m_exeCRC;
	msg.GameToJoin.iniCRC = TheGlobalData->m_iniCRC;

	AsciiString s;
	GetStringFromRegistry("\\ergc", "", s);
	strlcpy(msg.GameToJoin.serial, s.str(), ARRAY_SIZE(msg.GameToJoin.serial));

	sendMessage(&msg, ip);

	m_pendingAction = ACT_JOIN;
	m_expiration = timeGetTime() + m_actionTimeout;
}

void LANAPI::RequestGameJoinDirectConnect(UnsignedInt ipaddress)
{
	if (m_pendingAction != ACT_NONE)
	{
		OnGameJoin( RET_BUSY, nullptr );
		return;
	}

	if (ipaddress == 0)
	{
		OnGameJoin( RET_GAME_GONE, nullptr );
		return;
	}

	m_directConnectRemoteIP = ipaddress;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_GAME_INFO;
	fillInLANMessage(&msg);
	msg.PlayerInfo.ip = GetLocalIP();
	wcslcpy(msg.PlayerInfo.playerName, m_name.str(), ARRAY_SIZE(msg.PlayerInfo.playerName));

	sendMessage(&msg, ipaddress);

	m_pendingAction = ACT_JOINDIRECTCONNECT;
	m_expiration = timeGetTime() + m_actionTimeout;
}

void LANAPI::RequestGameLeave()
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_GAME_LEAVE;
	fillInLANMessage( &msg );
	wcslcpy(msg.PlayerInfo.playerName, m_name.str(), ARRAY_SIZE(msg.PlayerInfo.playerName));
	sendMessage(&msg);
	m_transport->update();  // Send immediately, before OnPlayerLeave below resets everything.

	if (m_currentGame && m_currentGame->getSessionID(0) == m_sessionId)
	{
		// Exit out immediately if we're hosting
		OnPlayerLeave(m_name);
		removeGame(m_currentGame);
		m_currentGame = nullptr;
		m_inLobby = true;
	}
	else
	{
		m_pendingAction = ACT_LEAVE;
		m_expiration = timeGetTime() + m_actionTimeout;
	}
}

void LANAPI::RequestGameAnnounce()
{
	// In game - are we a game host?
	if (m_currentGame && !(m_currentGame->getIsDirectConnect()))
	{
		if (m_currentGame->getSessionID(0) == m_sessionId || (m_currentGame->isGameInProgress() && TheNetwork && TheNetwork->isPacketRouter())) // if we're in game we should reply if we're the packet router
		{
			LANMessage reply;
			fillInLANMessage( &reply );
			reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;

			AsciiString gameOpts = GameInfoToAsciiString(m_currentGame);
			strlcpy(reply.GameInfo.options,gameOpts.str(), ARRAY_SIZE(reply.GameInfo.options));
			wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
			reply.GameInfo.inProgress = m_currentGame->isGameInProgress();
			reply.GameInfo.isDirectConnect = m_currentGame->getIsDirectConnect();

			sendMessage(&reply);
		}
	}
}

void LANAPI::RequestAccept()
{
	if (m_inLobby || !m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_SET_ACCEPT;
	msg.Accept.isAccepted = true;
	wcslcpy(msg.Accept.gameName, m_currentGame->getName().str(), ARRAY_SIZE(msg.Accept.gameName));
	sendMessage(&msg);
}

void LANAPI::RequestHasMap()
{
	if (m_inLobby || !m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_MAP_AVAILABILITY;
	msg.MapStatus.hasMap = m_currentGame->getSlot(m_currentGame->getLocalSlotNum())->hasMap();
	wcslcpy(msg.MapStatus.gameName, m_currentGame->getName().str(), ARRAY_SIZE(msg.MapStatus.gameName));
	CRC mapNameCRC;
//mapNameCRC.computeCRC(m_currentGame->getMap().str(), m_currentGame->getMap().getLength());
	AsciiString portableMapName = TheGameState->realMapPathToPortableMapPath(m_currentGame->getMap());
	mapNameCRC.computeCRC(portableMapName.str(), portableMapName.getLength());
	msg.MapStatus.mapCRC = mapNameCRC.get();
	sendMessage(&msg);

	if (!msg.MapStatus.hasMap)
	{
		UnicodeString text;
		UnicodeString mapDisplayName;
		const MapMetaData *mapData = TheMapCache->findMap( m_currentGame->getMap() );
		Bool willTransfer = TRUE;
		if (mapData)
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
			if (mapData->m_isOfficial)
				willTransfer = FALSE;
		}
		else
		{
			mapDisplayName.format(L"%hs", TheGameState->getMapLeafName(m_currentGame->getMap()).str());
			willTransfer = WouldMapTransfer(m_currentGame->getMap());
		}
		if (willTransfer)
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMapWillTransfer"), mapDisplayName.str());
		else
			text.format(TheGameText->fetch("GUI:LocalPlayerNoMap"), mapDisplayName.str());
		OnChat(L"SYSTEM", m_sessionId, text, LANCHAT_SYSTEM);
	}
}

void LANAPI::RequestChat( UnicodeString message, ChatType format )
{
	LANMessage msg;
	fillInLANMessage( &msg );
	wcslcpy(msg.Chat.gameName, (m_currentGame) ? m_currentGame->getName().str() : L"", ARRAY_SIZE(msg.Chat.gameName));
	msg.messageType = LANMessage::MSG_CHAT;
	msg.Chat.chatType = format;
	wcslcpy(msg.Chat.message, message.str(), ARRAY_SIZE(msg.Chat.message));
	sendMessage(&msg);

	OnChat(m_name, m_sessionId, message, format);
}

void LANAPI::RequestGameStart()
{
	if (m_inLobby || !m_currentGame || m_currentGame->getSessionID(0) != m_sessionId)
		return;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_GAME_START;
	fillInLANMessage( &msg );
	sendMessage(&msg);
	m_transport->update(); // force a send

	OnGameStart();
}

void LANAPI::ResetGameStartTimer()
{
	m_gameStartTime = 0;
	m_gameStartSeconds = 0;
}

void LANAPI::RequestGameStartTimer( Int seconds )
{
	if (m_inLobby || !m_currentGame || m_currentGame->getSessionID(0) != m_sessionId)
		return;

	UnsignedInt now = timeGetTime();
	m_gameStartTime = now + 1000;
	m_gameStartSeconds = (seconds) ? seconds - 1 : 0;

	LANMessage msg;
	msg.messageType = LANMessage::MSG_GAME_START_TIMER;
	msg.StartTimer.seconds = seconds;
	fillInLANMessage( &msg );
	sendMessage(&msg);
	m_transport->update(); // force a send

	OnGameStartTimer(seconds);
}

void LANAPI::RequestGameOptions( AsciiString gameOptions, Bool isPublic, UnsignedInt ip /* = 0 */ )
{
	DEBUG_ASSERTCRASH(gameOptions.getLength() < m_lanMaxOptionsLength, ("Game options string is too long!"));

	if (!m_currentGame)
		return;

	LANMessage msg;
	fillInLANMessage( &msg );
	msg.messageType = LANMessage::MSG_GAME_OPTIONS;
	strlcpy(msg.GameOptions.options, gameOptions.str(), ARRAY_SIZE(msg.GameOptions.options));
	sendMessage(&msg, ip);

	m_lastGameopt = gameOptions;

	int player;
	for (player = 0; player<MAX_SLOTS; ++player)
	{
		if (m_currentGame->getSessionID(player) == m_sessionId)
		{
			OnGameOptions(m_sessionId, player, AsciiString(msg.GameOptions.options));
			break;
		}
	}

	// We can request game options (side, color, etc) while we don't have a slot yet.  Of course, we don't need to
	// call OnGameOptions for those, so it's okay to silently fail.
	//DEBUG_ASSERTCRASH(player != MAX_SLOTS, ("Requested game options, but we're not in slot list!");
}

void LANAPI::RequestGameCreate( UnicodeString gameName, Bool isDirectConnect )
{
	// No games of the same name should exist...  Ignore that for now.
	/// @todo: make sure LAN games with identical names don't crash things like in RA2.

	if ((!m_inLobby || m_currentGame) && !isDirectConnect)
	{
		DEBUG_ASSERTCRASH(m_inLobby && m_currentGame, ("Can't create a game while in one!"));
		OnGameCreate(LANAPIInterface::RET_BUSY);
		return;
	}

	if (m_pendingAction != ACT_NONE)
	{
		OnGameCreate(LANAPIInterface::RET_BUSY);
		return;
	}

	// Create the local game object
	m_inLobby = false;
	LANGameInfo *myGame = NEW LANGameInfo;

	// Propagate the local client-server preference into the GameInfo at creation
	// time. This makes the CS=1 flag part of the options string from the first
	// broadcast, so joiners pick up the mode via ParseAsciiStringToGameInfo
	// instead of depending on each client's own -clientserver flag.
	myGame->setClientServerMode(TheGlobalData->m_clientServerMode);

	// Default lockstep / logic frame rate for new LAN games. The constructor
	// sets this to 70 anyway, but the explicit call here is the discoverable
	// hook the host UI uses — when the user picks a different value from the
	// LAN options menu's game-speed combo, that path also calls setGameFps.
	myGame->setGameFps(70);

	myGame->setSeed(GetTickCount());

//	myGame->setInProgress(false);
	myGame->enterGame();
	UnicodeString s;
	s.format(L"%8.8X%8.8X", m_sessionId, myGame->getSeed());
	if (gameName.isEmpty())
		s.concat(m_name);
	else
		s.concat(gameName);

	s.truncateTo(g_lanGameNameLength);

	DEBUG_LOG(("Setting local game name to '%ls'", s.str()));

	myGame->setName(s);

	LANGameSlot newSlot;
	newSlot.setState(SLOT_PLAYER, m_name);
	newSlot.setIP(m_sessionId);  // Use session ID instead of local IP for relay identification
	newSlot.setSessionID(m_sessionId);
	newSlot.setPort(NETWORK_BASE_PORT_NUMBER);
	newSlot.setLastHeard(0);
	newSlot.setLogin(m_userName);
	newSlot.setHost(m_hostName);

	myGame->setSlot(0,newSlot);
	myGame->setNext(nullptr);
	LANPreferences pref;

	AsciiString mapName = pref.getPreferredMap();

	myGame->setMap(mapName);
	myGame->setIsDirectConnect(isDirectConnect);

	myGame->setLastHeard(timeGetTime());
	m_currentGame = myGame;

/// @todo: Need to initialize the players elsewere.
/*	for (int player = 1; player < MAX_SLOTS; ++player)
	{
		myGame->setPlayerName(player, L"");
		myGame->setIP(player, 0);
		myGame->setAccepted(player, false);
	}*/

	// Add the game to the local game list
	addGame(myGame);

	// Send an announcement
	//RequestSlotList();
/*
	LANMessage msg;
	wcslcpy(msg.name, m_name.str(), ARRAY_SIZE(msg.name));
	wcscpy(msg.GameInfo.gameName, myGame->getName().str());
	for (player=0; player<MAX_SLOTS; ++player)
	{
		wcscpy(msg.GameInfo.name[player], myGame->getPlayerName(player).str());
		msg.GameInfo.ip[player] = myGame->getIP(player);
		msg.GameInfo.playerAccepted[player] = myGame->getAccepted(player);
	}
	msg.messageType = LANMessage::MSG_GAME_ANNOUNCE;
*/
	OnGameCreate(LANAPIInterface::RET_OK);
}


/*static const char slotListID		= 'S';
static const char gameOptionsID	= 'G';
static const char acceptID			= 'A';
static const char wannaStartID	= 'W';

AsciiString LANAPI::createSlotString()
{
	AsciiString slotList;
	slotList.concat(slotListID);
	for (int i=0; i<MAX_SLOTS; ++i)
	{
		LANGameSlot *slot = GetMyGame()->getLANSlot(i);
		AsciiString str;
		if (slot->isHuman())
		{
			str = "H";
			LANPlayer *user = slot->getUser();
			DEBUG_ASSERTCRASH(user, ("Human player has no User*!"));
			AsciiString name;
			name.translate(user->getName());
			str.concat(name);
			str.concat(',');
		}
		else if (slot->isAI())
		{
			if (slot->getState() == SLOT_EASY_AI)
				str = "CE,";
			if (slot->getState() == SLOT_MED_AI)
				str = "CM,";
			else
				str = "CB,";
		}
		else if (slot->getState() == SLOT_OPEN)
		{
			str = "O,";
		}
		else if (slot->getState() == SLOT_CLOSED)
		{
			str = "X,";
		}
		else
		{
			DEBUG_CRASH(("Bad slot type"));
			str = "X,";
		}

		slotList.concat(str);
	}
	return slotList;
}
*/
/*
void LANAPI::RequestSlotList()
{

	LANMessage reply;
	reply.messageType = LANMessage::MSG_GAME_ANNOUNCE;
	wcslcpy(reply.name, m_name.str(), ARRAY_SIZE(reply.name));
	int player;
	for (player = 0; player < MAX_SLOTS; ++player)
	{
		wcslcpy(reply.GameInfo.name[player], m_currentGame->getPlayerName(player).str(), ARRAY_SIZE(reply.GameInfo.name[player]));
		reply.GameInfo.ip[player] = m_currentGame->getIP(player);
		reply.GameInfo.playerAccepted[player] = m_currentGame->getSlot(player)->isAccepted();
	}
	wcslcpy(reply.GameInfo.gameName, m_currentGame->getName().str(), ARRAY_SIZE(reply.GameInfo.gameName));
	reply.GameInfo.inProgress = m_currentGame->isGameInProgress();

	sendMessage(&reply);

	OnSlotList(LANAPIInterface::RET_OK, m_currentGame);
}
*/
void LANAPI::RequestSetName( UnicodeString newName )
{
	newName.trim();
	if (m_pendingAction != ACT_NONE)
	{
		// Can't change name while joining games
		OnNameChange(m_sessionId, newName);
		return;
	}

	// Set up timer
	m_lastResendTime = timeGetTime();

	if (m_inLobby && m_pendingAction == ACT_NONE)
	{
		m_name = newName;
		LANMessage msg;
		fillInLANMessage( &msg );
		msg.messageType = LANMessage::MSG_LOBBY_ANNOUNCE;
		sendMessage(&msg);

		// Update the interface
		LANPlayer *player = LookupPlayer(m_sessionId);
		if (!player)
		{
			player = NEW LANPlayer;
			player->setIP(m_sessionId);
			player->setSessionID(m_sessionId);
		}
		else
		{
			removePlayer(player);
		}
		player->setName(m_name);
		player->setHost(m_hostName);
		player->setLogin(m_userName);
		player->setLastHeard(timeGetTime());

		addPlayer(player);

		OnNameChange(player->getSessionID(), player->getName());
	}
}

void LANAPI::fillInLANMessage( LANMessage *msg )
{
	if (!msg)
		return;

	wcslcpy(msg->name, m_name.str(), ARRAY_SIZE(msg->name));
	strlcpy(msg->userName, m_userName.str(), ARRAY_SIZE(msg->userName));
	strlcpy(msg->hostName, m_hostName.str(), ARRAY_SIZE(msg->hostName));
	msg->sessionID = m_sessionId;
}

void LANAPI::RequestLobbyLeave( Bool forced )
{
	LANMessage msg;
	msg.messageType = LANMessage::MSG_REQUEST_LOBBY_LEAVE;
	fillInLANMessage( &msg );
	sendMessage(&msg);

	if (forced)
		m_transport->update();
}

// Misc utility functions
LANGameInfo * LANAPI::LookupGame( UnicodeString gameName )
{
	LANGameInfo *theGame = m_games;

	while (theGame && theGame->getName() != gameName)
	{
		theGame = theGame->getNext();
	}

	return theGame; // null means we didn't find anything.
}

LANGameInfo * LANAPI::LookupGameByListOffset( Int offset )
{
	LANGameInfo *theGame = m_games;

	if (offset < 0)
		return nullptr;

	while (offset-- && theGame)
	{
		theGame = theGame->getNext();
	}

	return theGame; // null means we didn't find anything.
}

LANGameInfo* LANAPI::LookupGameByHost(UnsignedInt hostIP)
{
	LANGameInfo* lastGame = nullptr;
	UnsignedInt lastHeard = 0;

	for (LANGameInfo* game = m_games; game; game = game->getNext())
	{
		if (game->getHostIP() == hostIP && game->getLastHeard() >= lastHeard)
		{
			lastGame = game;
			lastHeard = game->getLastHeard();
		}
	}

	return lastGame;
}

void LANAPI::removeGame( LANGameInfo *game )
{
	LANGameInfo *g = m_games;
	if (!game)
	{
		return;
	}
	else if (m_games == game)
	{
		m_games = m_games->getNext();
	}
	else
	{
		while (g->getNext() && g->getNext() != game)
		{
			g = g->getNext();
		}
		if (g->getNext() == game)
		{
			g->setNext(game->getNext());
		}
		else
		{
			// Odd.  We went the whole way without finding it in the list.
			DEBUG_CRASH(("LANGameInfo wasn't in the list"));
		}
	}
}

LANPlayer * LANAPI::LookupPlayer( UnsignedInt playerSessionID )
{
	LANPlayer *thePlayer = m_lobbyPlayers;

	while (thePlayer && thePlayer->getSessionID() != playerSessionID)
	{
		thePlayer = thePlayer->getNext();
	}

	return thePlayer; // null means we didn't find anything.
}

void LANAPI::removePlayer( LANPlayer *player )
{
	LANPlayer *p = m_lobbyPlayers;
	if (!player)
	{
		return;
	}
	else if (m_lobbyPlayers == player)
	{
		m_lobbyPlayers = m_lobbyPlayers->getNext();
	}
	else
	{
		while (p->getNext() && p->getNext() != player)
		{
			p = p->getNext();
		}
		if (p->getNext() == player)
		{
			p->setNext(player->getNext());
		}
		else
		{
			// Odd.  We went the whole way without finding it in the list.
			DEBUG_CRASH(("LANPlayer wasn't in the list"));
		}
	}
}

void LANAPI::addGame( LANGameInfo *game )
{
	if (!m_games)
	{
		m_games = game;
		game->setNext(nullptr);
		return;
	}
	else
	{
		if (game->getName().compareNoCase(m_games->getName()) < 0)
		{
			game->setNext(m_games);
			m_games = game;
			return;
		}
		else
		{
			LANGameInfo *g = m_games;
			while (g->getNext() && g->getNext()->getName().compareNoCase(game->getName()) > 0)
			{
				g = g->getNext();
			}
			game->setNext(g->getNext());
			g->setNext(game);
			return;
		}
	}
}

void LANAPI::addPlayer( LANPlayer *player )
{
	if (!m_lobbyPlayers)
	{
		m_lobbyPlayers = player;
		player->setNext(nullptr);
		return;
	}
	else
	{
		if (player->getName().compareNoCase(m_lobbyPlayers->getName()) < 0)
		{
			player->setNext(m_lobbyPlayers);
			m_lobbyPlayers = player;
			return;
		}
		else
		{
			LANPlayer *p = m_lobbyPlayers;
			while (p->getNext() && p->getNext()->getName().compareNoCase(player->getName()) > 0)
			{
				p = p->getNext();
			}
			player->setNext(p->getNext());
			p->setNext(player);
			return;
		}
	}
}

Bool LANAPI::SetLocalIP( UnsignedInt localIP )
{
	m_localIP = localIP;

	m_transport->reset();
	// The lobby transport is only used for its inBuffer/outBuffer structures
	// once we've switched to relay mode; it never actually sends or receives
	// packets itself. Bind to INADDR_ANY:0 (ephemeral) unconditionally so that
	// multiple game instances on the same machine never fight over the
	// well-known lobby port (28910). Previously we used lobbyPort as a fallback
	// when the relay failed, which caused instances 2..N to fail UDP::Bind and
	// pop the "Network Error" dialog even though the real cause was that the
	// relay wasn't reachable.
	Bool retval = m_transport->init((UnsignedInt)0, (UnsignedShort)0);
	m_transport->allowBroadcasts(true);

	// If we couldn't reach the relay, report that as a SetLocalIP failure so
	// the caller (LanLobbyMenuInit) surfaces the existing SocketError dialog
	// instead of dropping the player into an eerily-empty lobby.
	if (retval && !m_relayConnected)
	{
		DEBUG_LOG(("LANAPI::SetLocalIP - transport bind OK but relay not connected; reporting failure"));
		retval = FALSE;
	}

	return retval;
}

void LANAPI::SetLocalIP( AsciiString localIP )
{
	UnsignedInt resolvedIP = ResolveIP(localIP);
	SetLocalIP(resolvedIP);
}

Bool LANAPI::AmIHost()
{
	return m_currentGame && m_currentGame->getSessionID(0) == m_sessionId;
}

void LANAPI::setIsActive(Bool isActive) {
	DEBUG_LOG(("LANAPI::setIsActive - entering"));
	if (isActive != m_isActive) {
		DEBUG_LOG(("LANAPI::setIsActive - m_isActive changed to %s", isActive ? "TRUE" : "FALSE"));
		if (isActive == FALSE) {
			if ((m_inLobby == FALSE) && (m_currentGame != nullptr)) {
				LANMessage msg;
				fillInLANMessage( &msg );
				msg.messageType = LANMessage::MSG_INACTIVE;
				sendMessage(&msg);
				DEBUG_LOG(("LANAPI::setIsActive - sent an IsActive message"));
			}
		}
	}
	m_isActive = isActive;
}
