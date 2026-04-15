#pragma once

// Cosmetic profile sync for multiplayer cosmetic state (team color +
// shader effect). The relay server pushes a RELAY_TYPE_COSMETICS packet
// for every connected user at AUTH time and again whenever anyone's
// profile changes. Clients decode the packet into this cache; the
// render path reads from it per-drawable to apply the correct effect
// to each player's units.
//
// Thread model: LANAPI::relayRecv runs on the game thread during the
// update pump, and render-path reads also run on the game thread, so
// no synchronization is needed today. If future code moves relay recv
// onto a background thread, add an internal mutex — the API is already
// shaped for that (pass-by-value structs).

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include <unordered_map>

struct PlayerCosmetic
{
	AsciiString  displayName;
	UnsignedInt  colorPacked = 0;   // 0x00RRGGBB. 0 = no override.
	Int          shaderId    = 0;   // ShaderEffects catalog id. 0 = stock.
};

class CosmeticsCache
{
public:
	static CosmeticsCache& Instance();

	void Set(Int userId, const PlayerCosmetic& c);
	const PlayerCosmetic* Get(Int userId) const;

	/// Look up a cosmetic by the authoritative display name that the relay
	/// broadcasts. O(N) scan — tiny N (at most ~32 currently-connected users).
	/// Returns nullptr if no match. Case-insensitive.
	const PlayerCosmetic* GetByDisplayName(const AsciiString& name) const;

	UnsignedInt GetColor(Int userId, UnsignedInt fallback = 0) const;
	Int         GetShaderId(Int userId) const;

	void Clear();

private:
	CosmeticsCache() = default;
	std::unordered_map<Int, PlayerCosmetic> m_byUserId;
};
