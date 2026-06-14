#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <mutex>
#include "Packet.h"

class RedisManager {
public:
    static RedisManager& GetInstance();

    void Init(const std::string& host, int port = DEFAULT_PORT);

    // [캐릭터 스탯 캐시]
    bool SetCharacterStat(uint64_t characterId, const PKT_CharacterStat& stat);
    bool GetCharacterStat(uint64_t characterId, PKT_CharacterStat& outStat);
    bool DeleteCharacterStat(uint64_t characterId);

    // [세션 캐시]
    bool SetSession(uint64_t sessionId, uint64_t characterId);
    bool GetSession(uint64_t sessionId, uint64_t& outCharacterId);
    bool DeleteSession(uint64_t sessionId);

    std::vector<uint64_t> PopDirtyIds();

private:
    RedisManager();
    ~RedisManager();
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    static constexpr int         DEFAULT_PORT    = 6379;
    static constexpr int         EXPIRE_SECONDS  = 3600;
    static constexpr const char* DIRTY_SET_KEY   = "dirty_characters";

    redisContext* m_context = nullptr;
    std::mutex    m_lock;

    // [키 네이밍 헬퍼]
    std::string MakeCharacterStatKey(uint64_t characterId) const;
    std::string MakeSessionKey(uint64_t sessionId) const;
};
