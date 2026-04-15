// Stub type definitions for removed GameSpy SDK.
// These replace the types previously provided by the GameSpy SDK headers
// (gp/gp.h, peer/peer.h, gstats/gpersist.h, ghttp/ghttp.h).

#pragma once

// GP types (from gp/gp.h)
typedef int GPProfile;
typedef int GPEnum;
typedef int GPResult;
typedef int GPErrorCode;
typedef void* GPConnection;

// GP status constants
#define GP_PLAYING   2
#define GP_ONLINE    1
#define GP_OFFLINE   0

// GP error constants
#define GP_NO_ERROR  0

// GP buffer size constants (from gp/gp.h)
#define GP_NICK_LEN             31
#define GP_EMAIL_LEN            51
#define GP_PASSWORD_LEN         31
#define GP_STATUS_STRING_LEN    256
#define GP_LOCATION_STRING_LEN  256
#define GP_COUNTRYCODE_LEN      3
#define GP_REASON_LEN           1025

// ghttp stub functions (from ghttp/ghttp.h)
inline void ghttpStartup() {}
inline void ghttpCleanup() {}
inline void ghttpSetProxy(const char*) {}

// Peer types (from peer/peer.h)
typedef void* PEER;
typedef int PEERBool;
#define PEERTrue  1
#define PEERFalse 0

// RoomType enum (from peer/peer.h)
typedef enum { TitleRoom, GroupRoom, StagingRoom, NumRoomTypes } RoomType;
