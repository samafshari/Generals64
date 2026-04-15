/**
 * RestClient — WinHTTP-backed synchronous POST helper. See header.
 */

#include "PreRTS.h"

#include "GameNetwork/RestClient.h"

#include <windows.h>
#include <winhttp.h>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

extern char g_relayServerHost[];   // LANAPI.cpp
extern char g_authLaunchToken[];   // CommandLine.cpp
extern char g_authGameToken[];     // CommandLine.cpp

namespace RestClient
{

static const DWORD kTimeoutMs = 5000;
static const INTERNET_PORT kRelayHttpPort = 28912;

AsciiString defaultBaseUrl()
{
	if (g_relayServerHost[0] == '\0')
		return AsciiString::TheEmptyString;
	AsciiString url;
	url.format("http://%s:%u", g_relayServerHost, (UnsignedInt)kRelayHttpPort);
	return url;
}

// ── UTF-8 <-> wide-char bridging ────────────────────────────────

static std::wstring toWide(const char *s)
{
	if (!s || !*s) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	if (n <= 0) return std::wstring();
	std::wstring out;
	out.resize(n - 1);
	MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), n);
	return out;
}

static AsciiString fromWide(const wchar_t *s, int len)
{
	AsciiString out;
	if (!s || len <= 0) return out;
	int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
	if (n <= 0) return out;
	std::string tmp;
	tmp.resize(n);
	WideCharToMultiByte(CP_UTF8, 0, s, len, tmp.data(), n, nullptr, nullptr);
	out.set(tmp.c_str());
	return out;
}

// ── URL parse ───────────────────────────────────────────────────

struct Parsed
{
	std::wstring host;
	std::wstring path;
	INTERNET_PORT port = 0;
	bool https = false;
	bool ok = false;
};

static Parsed parseUrl(const AsciiString &base, const AsciiString &path)
{
	Parsed p;
	AsciiString full;
	full.concat(base.str());
	full.concat(path.str());

	std::wstring url = toWide(full.str());
	if (url.empty()) return p;

	URL_COMPONENTSW uc{};
	uc.dwStructSize    = sizeof(uc);
	wchar_t hostBuf[256]{};
	wchar_t pathBuf[1024]{};
	uc.lpszHostName    = hostBuf;
	uc.dwHostNameLength = (DWORD)(sizeof(hostBuf) / sizeof(hostBuf[0]));
	uc.lpszUrlPath     = pathBuf;
	uc.dwUrlPathLength = (DWORD)(sizeof(pathBuf) / sizeof(pathBuf[0]));

	// WinHttpCrackUrl's signature takes LPURL_COMPONENTS which aliases to
	// URL_COMPONENTSA when UNICODE is undefined (the default in this
	// codebase). The structs are layout-compatible across the A/W split —
	// they just differ in whether the char pointers are narrow or wide.
	// Since we pass wide-char URLs, we use URL_COMPONENTSW and cast
	// through LPURL_COMPONENTS so the function writes wide buffers.
	if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, reinterpret_cast<LPURL_COMPONENTS>(&uc)))
		return p;

	p.host.assign(uc.lpszHostName, uc.dwHostNameLength);
	p.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
	p.port  = uc.nPort;
	p.https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
	p.ok    = !p.host.empty();
	return p;
}

// ── POST ────────────────────────────────────────────────────────

Response post(const AsciiString &baseUrl,
              const AsciiString &path,
              const AsciiString &jsonBody,
              const AsciiString &bearerToken)
{
	Response r{};
	r.httpStatus = 0;

	if (baseUrl.isEmpty())
		return r;

	Parsed u = parseUrl(baseUrl, path);
	if (!u.ok)
		return r;

	HINTERNET hSession = WinHttpOpen(L"GeneralsRemastered/1.0",
	                                 WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                                 WINHTTP_NO_PROXY_NAME,
	                                 WINHTTP_NO_PROXY_BYPASS,
	                                 0);
	if (!hSession) return r;
	WinHttpSetTimeouts(hSession, (int)kTimeoutMs, (int)kTimeoutMs, (int)kTimeoutMs, (int)kTimeoutMs);

	HINTERNET hConnect = WinHttpConnect(hSession, u.host.c_str(), u.port, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return r; }

	DWORD flags = u.https ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect,
	                                        L"POST",
	                                        u.path.c_str(),
	                                        nullptr,
	                                        WINHTTP_NO_REFERER,
	                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
	                                        flags);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return r;
	}

	// Build headers. Content-Type always; Authorization only if a token
	// was provided.
	std::wstring headers = L"Content-Type: application/json\r\n";
	if (!bearerToken.isEmpty())
	{
		headers += L"Authorization: Bearer ";
		headers += toWide(bearerToken.str());
		headers += L"\r\n";
	}

	const char *bodyStr = jsonBody.str();
	DWORD bodyLen = (DWORD)jsonBody.getLength();

	BOOL sent = WinHttpSendRequest(hRequest,
	                               headers.c_str(),
	                               (DWORD)headers.size(),
	                               (LPVOID)bodyStr,
	                               bodyLen,
	                               bodyLen,
	                               0);
	if (sent && WinHttpReceiveResponse(hRequest, nullptr))
	{
		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		WinHttpQueryHeaders(hRequest,
		                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                    WINHTTP_HEADER_NAME_BY_INDEX,
		                    &status,
		                    &statusSize,
		                    WINHTTP_NO_HEADER_INDEX);
		r.httpStatus = (int)status;

		// Drain the body.
		std::string body;
		for (;;)
		{
			DWORD avail = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
				break;
			size_t oldSize = body.size();
			body.resize(oldSize + avail);
			DWORD read = 0;
			if (!WinHttpReadData(hRequest, body.data() + oldSize, avail, &read))
			{
				body.resize(oldSize);
				break;
			}
			body.resize(oldSize + read);
		}
		r.body.set(body.c_str());
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return r;
}

// ── Launch-token → game-token exchange ─────────────────────────

static std::mutex s_tokenMutex;

// Extract the "gameToken" string field from a minimal JSON object. We
// deliberately don't pull in a JSON parser for this one call: the server's
// response shape is fixed as
//   {"gameToken":"g_...","userId":N,"displayName":"...","expiresUtc":"..."}
// so a simple substring search matches the field unambiguously.
static Bool extractStringField(const char *json, const char *fieldName, char *out, int outLen)
{
	if (!json || !fieldName || !out || outLen <= 0) return FALSE;
	char needle[64];
	int nlen = _snprintf(needle, sizeof(needle), "\"%s\"", fieldName);
	if (nlen <= 0 || nlen >= (int)sizeof(needle)) return FALSE;
	const char *p = strstr(json, needle);
	if (!p) return FALSE;
	p += nlen;
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) ++p;
	if (*p != '"') return FALSE;
	++p;
	int i = 0;
	while (*p && *p != '"' && i < outLen - 1)
	{
		out[i++] = *p++;
	}
	out[i] = '\0';
	return i > 0;
}

Bool ensureGameToken()
{
	std::lock_guard<std::mutex> lk(s_tokenMutex);

	if (g_authGameToken[0] != '\0')
		return TRUE; // already have one — nothing to do
	if (g_authLaunchToken[0] == '\0')
		return FALSE; // nothing to exchange — game wasn't launched via launcher
	if (g_relayServerHost[0] == '\0')
		return FALSE; // -relayserver not supplied

	AsciiString base = defaultBaseUrl();
	AsciiString body;
	body.concat("{\"launchToken\":\"");
	body.concat(g_authLaunchToken);
	body.concat("\"}");

	Response r = post(base, AsciiString("/api/game-token/exchange"), body);
	if (!r.ok())
	{
		DEBUG_LOG(("RestClient::ensureGameToken - exchange failed, status=%d", r.httpStatus));
		return FALSE;
	}

	char token[128] = {0};
	if (!extractStringField(r.body.str(), "gameToken", token, (int)sizeof(token)))
	{
		DEBUG_LOG(("RestClient::ensureGameToken - no gameToken in response"));
		return FALSE;
	}

	// Copy into the engine's globals. Both sides of the auth flow (relay
	// TCP AUTH handshake in LANAPI::relayConnect, REST Bearer header in
	// GameTelemetry) read from g_authGameToken, so this is the single
	// fan-out point.
	int len = (int)strlen(token);
	if (len >= 128) len = 127;
	memcpy(g_authGameToken, token, len);
	g_authGameToken[len] = '\0';
	DEBUG_LOG(("RestClient::ensureGameToken - acquired game token (%d bytes)", len));
	return TRUE;
}

} // namespace RestClient
