#include "SyncWorker.h"
#include "AsyncLogger.h"
#include "DBManager.h"
#include "RedisManager.h"

SyncWorker& SyncWorker::GetInstance() {
    static SyncWorker instance;
    return instance;
}

SyncWorker::~SyncWorker() {
    Stop();
}

void SyncWorker::Start() {
    m_isRunning = true;
    m_syncThread = std::thread(&SyncWorker::SyncLoop, this);
    AsyncLogger::GetInstance().Log("SyncWorker 시작");
}

void SyncWorker::Stop() {
    m_isRunning = false;
    m_cv.notify_all();
    if (m_syncThread.joinable()) {
        m_syncThread.join();
    }
    AsyncLogger::GetInstance().Log("SyncWorker 종료");
}

void SyncWorker::SyncLoop() {
    while (m_isRunning) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::seconds(SYNC_INTERVAL_SECONDS),
            [this] { return !m_isRunning.load(); });

        if (!m_isRunning) {
            FlushAll();
            break;
        }

        FlushAll();
    }
}

void SyncWorker::FlushAll() {
    AsyncLogger::GetInstance().Log("Redis → MySQL 동기화 시작");

    auto ids = RedisManager::GetInstance().PopDirtyIds();
    int successCount = 0;

    for (uint64_t characterId : ids) {
        PKT_CharacterStat stat{};
        if (RedisManager::GetInstance().GetCharacterStat(characterId, stat)) {
            if (DBManager::GetInstance().UpdateCharacterStat(stat))
                ++successCount;
        }
    }

    AsyncLogger::GetInstance().Log(
        "Redis → MySQL 동기화 완료. " + std::to_string(successCount) +
        "/" + std::to_string(ids.size()) + " 성공");
}
