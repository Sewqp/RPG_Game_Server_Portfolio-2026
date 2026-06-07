#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>

class AsyncLogger {
public:
    static AsyncLogger& GetInstance() {
        static AsyncLogger instance;
        return instance;
    }

    // [LLM 엔드포인트 + Discord 웹훅 URL 설정 — 서버 시작 직후 1회 호출]
    void Configure(const std::string& llmEndpoint, const std::string& discordWebhook);

    // [논블로킹 로그 — 큐에 push하고 즉시 리턴]
    void Log(const std::string& message);

    // [에러 로그 — 파일 기록 + LLM→Discord 파이프라인 트리거]
    void LogError(const std::string& message);

private:
    AsyncLogger();
    ~AsyncLogger();
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    // [로거 스레드 루프 — 로그 기록 + AI 디스패치]
    void ProcessLoop();

    // [LM Studio에 에러 분석 요청 후 Discord 전송 — 로거 스레드에서만 호출]
    void SendToAI(const std::string& errorMsg);

    // [Discord 웹훅으로 에러 + 분석 결과 전송]
    void SendToDiscord(const std::string& error, const std::string& analysis);

    std::string GetTimestamp() const;
    std::string GetLogFileName() const;

    std::queue<std::string>               m_logQueue;
    std::queue<std::string>               m_aiQueue;   // 에러 전용 AI 디스패치 큐
    std::mutex                            m_mutex;
    std::condition_variable               m_cv;
    std::thread                           m_loggerThread;
    std::atomic<bool>                     m_isRunning{ true };

    std::string                           m_llmEndpoint;
    std::string                           m_discordWebhook;
    std::chrono::steady_clock::time_point m_lastAiSendTime{};  // 60초 쿨다운
};