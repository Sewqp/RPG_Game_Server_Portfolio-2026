#include <atomic>
#include <thread>
#include <winsock2.h>
#include "AsyncLogger.h"
#include "IocpCore.h"
#include "Acceptor.h"
#include "SessionManager.h"
#include "DBManager.h"
#include "RedisManager.h"
#include "SyncWorker.h"

// [전역 세션 ID 발급기 — Session.cpp에서 extern으로 참조]
std::atomic<uint64_t> GSessionIdAllocator{ 0 };

static constexpr uint16_t    SERVER_PORT = 9000;
static constexpr const char* DB_HOST     = "127.0.0.1";
static constexpr const char* DB_USER     = "root";
static constexpr const char* DB_PASS     = "";
static constexpr const char* DB_SCHEMA   = "game_server_schema";
static constexpr const char* REDIS_HOST  = "127.0.0.1";

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // [Winsock 초기화]
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        AsyncLogger::GetInstance().LogError("Winsock 초기화 실패");
        return -1;
    }

    // [IOCP 초기화 및 시작]
    IocpCore iocpCore;
    iocpCore.Init();
    iocpCore.Start();

    // [Acceptor 초기화]
    Acceptor acceptor(iocpCore);
    acceptor.Init(SERVER_PORT);
    iocpCore.SetAcceptor(&acceptor);

    // [DB / Redis / SyncWorker 초기화]
    DBManager::GetInstance().Init(DB_HOST, DB_USER, DB_PASS, DB_SCHEMA);
    RedisManager::GetInstance().Init(REDIS_HOST);
    SyncWorker::GetInstance().Start();

    AsyncLogger::GetInstance().Log(
        "서버 시작. Port: " + std::to_string(SERVER_PORT));

    // [메인 루프]
    try {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& e) {
        AsyncLogger::GetInstance().LogError(
            "예외 발생: " + std::string(e.what()));
    }

    // [종료 처리]
    SyncWorker::GetInstance().Stop();
    WSACleanup();

    return 0;
}
