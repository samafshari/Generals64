#include "Common/CosmeticsCache.h"

CosmeticsCache& CosmeticsCache::Instance()
{
	static CosmeticsCache s_instance;
	return s_instance;
}

void CosmeticsCache::Set(Int userId, const PlayerCosmetic& c)
{
	if (userId == 0) return;  // 0 is the "unknown / AI / not-authenticated" sentinel
	m_byUserId[userId] = c;
}

const PlayerCosmetic* CosmeticsCache::Get(Int userId) const
{
	auto it = m_byUserId.find(userId);
	return it == m_byUserId.end() ? nullptr : &it->second;
}

const PlayerCosmetic* CosmeticsCache::GetByDisplayName(const AsciiString& name) const
{
	if (name.isEmpty()) return nullptr;
	for (const auto& kv : m_byUserId)
	{
		if (kv.second.displayName.compareNoCase(name) == 0)
			return &kv.second;
	}
	return nullptr;
}

UnsignedInt CosmeticsCache::GetColor(Int userId, UnsignedInt fallback) const
{
	const PlayerCosmetic* c = Get(userId);
	if (c == nullptr || c->colorPacked == 0) return fallback;
	return c->colorPacked;
}

Int CosmeticsCache::GetShaderId(Int userId) const
{
	const PlayerCosmetic* c = Get(userId);
	return c == nullptr ? 0 : c->shaderId;
}

void CosmeticsCache::Clear()
{
	m_byUserId.clear();
}
