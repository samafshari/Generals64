/**
 * RestClient — thin synchronous HTTP POST helper used by the game
 * client to talk to the auth/telemetry REST API on the relay server.
 * Wraps WinHTTP so we avoid pulling in a third-party HTTP library.
 * Synchronous is fine because all calls run from background threads
 * (the telemetry sender thread, a token-exchange thread at startup);
 * blocking on a 5-second request timeout doesn't stall the game loop.
 */

#pragma once
#ifndef __RESTCLIENT_H_
#define __RESTCLIENT_H_

#include "Common/GameCommon.h"
#include "Common/AsciiString.h"

namespace RestClient
{
	/// Result of a single POST. httpStatus is the HTTP response code (200,
	/// 401, 500, etc.) or 0 if the request failed before getting a response.
	struct Response
	{
		int         httpStatus;
		AsciiString body;
		Bool        ok() const { return httpStatus >= 200 && httpStatus < 300; }
	};

	/// Build a base URL of the form "http://<relay-host>:28912" from the
	/// launcher-supplied g_relayServerHost. Returns empty string if the
	/// relay host isn't configured.
	AsciiString defaultBaseUrl();

	/// Sends POST <baseUrl><path> with the given JSON body and an optional
	/// Authorization: Bearer header. 5-second timeout. Safe to call from
	/// any thread — WinHTTP handles are opened per-call.
	Response post(const AsciiString &baseUrl,
	              const AsciiString &path,
	              const AsciiString &jsonBody,
	              const AsciiString &bearerToken = AsciiString::TheEmptyString);

	/// Exchanges the single-use launch token (g_authLaunchToken, l_-prefixed,
	/// 1-minute TTL) for a 24-hour game token (g_-prefixed), storing the
	/// result in g_authGameToken. Idempotent: if g_authGameToken is already
	/// populated, returns TRUE immediately without making a call. Returns
	/// FALSE on network failure or invalid/expired launch token — callers
	/// should treat that as "no auth available" and skip operations that
	/// require it. Thread-safe (internally serialized) so both LANAPI and
	/// GameTelemetry can trigger the exchange without racing.
	Bool ensureGameToken();
}

#endif // __RESTCLIENT_H_
