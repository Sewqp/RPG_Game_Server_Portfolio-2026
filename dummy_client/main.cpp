#pragma comment(lib, "ws2_32.lib")

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <latch>
#include <vector>
#include <chrono>
#include <algorithm>
#include <random>
#include <cstring>
#include <cstdio>

#include "../game_server/Packet.h"
#include "DummyClient.h"

// -------------------------------------------------------
// 설정
// -------------------------------------------------------
constexpr int      TARGET_CONNECTIONS = 10000;
constexpr char     SERVER_IP[]        = "127.0.0.1";
constexpr uint16_t SERVER_PORT        = 9000;

// 100개씩 3ms 간격 — 300ms 램프업
constexpr int BATCH_SIZE     = 100;
constexpr int BATCH_DELAY_MS = 3;

// 시나리오 루프 횟수 (맵 이동 + 아이템 획득)
constexpr int SCENARIO_LOOPS = 25;

// 아이템 획득 확률 (10 중 3 = 30%)
constexpr int ITEM_DROP_CHANCE = 3;

const int WORKER_THREADS = std::max(32, (int)std::thread::hardware_concurrency() * 2);

// -------------------------------------------------------
// 전역 통계 / 동기화
// -------------------------------------------------------
std::atomic<int>    g_connected   { 0 };
std::atomic<int>    g_failed      { 0 };
std::atomic<int>    g_packetsSent { 0 };
std::atomic<int>    g_sendErrors  { 0 };
std::atomic<int64_t> g_bytesSent  { 0 };
std::atomic<bool>   g_done        { false };

std::latch g_connectLatch(TARGET_CONNECTIONS);

// -------------------------------------------------------
// 패킷 전송 헬퍼
// -------------------------------------------------------
template<typename T>
bool Send(DummyClient& client, T& pkt) {
    if (!client.SendPacket(&pkt, sizeof(T))) {
        ++g_sendErrors;
        return false;
    }
    ++g_packetsSent;
    g_bytesSent.fetch_add(sizeof(T), std::memory_order_relaxed);
    return true;
}

// -------------------------------------------------------
// 워커 스레드
//   [페이즈 1] 접속
//   [페이즈 2] 5단계 시나리오 실행
//     1. PKT_Character  — 캐릭터 등록
//     2. PKT_CharacterStat — 초기 스탯 전송
//     3. PKT_EnterRoom  — 맵 기반 룸 입장
//     4. 루프(SCENARIO_LOOPS): 맵 이동 + 30% 확률 아이템 획득
//     5. PKT_LeaveRoom  — 룸 퇴장
// -------------------------------------------------------
void WorkerThread(int workerId, std::chrono::steady_clock::time_point testStart) {
    int slice   = TARGET_CONNECTIONS / WORKER_THREADS;
    int startId = workerId * slice;
    int endId   = (workerId == WORKER_THREADS - 1) ? TARGET_CONNECTIONS : startId + slice;

    std::vector<DummyClient> clients;
    clients.reserve(endId - startId);

    // ---- 페이즈 1: 접속 ----
    for (int clientId = startId; clientId < endId; ++clientId) {
        int batchIndex = clientId / BATCH_SIZE;
        auto batchTime = testStart + std::chrono::milliseconds(batchIndex * BATCH_DELAY_MS);
        std::this_thread::sleep_until(batchTime);

        DummyClient client(clientId);
        if (client.Connect(SERVER_IP, SERVER_PORT)) {
            ++g_connected;
            clients.push_back(std::move(client));
        } else {
            ++g_failed;
        }
        g_connectLatch.count_down();
    }

    g_connectLatch.wait();

    // ---- 페이즈 2: 시나리오 실행 ----
    for (auto& client : clients) {
        std::mt19937 rng(static_cast<unsigned>(client.GetId()));
        std::uniform_int_distribution<int> dropDist(0, 9);

        uint64_t charId = static_cast<uint64_t>(client.GetId() % 10000) + 1;
        uint32_t mapId  = static_cast<uint32_t>((client.GetId() % 20) + 1);

        // 1. 캐릭터 등록
        PKT_Character charPkt{};
        charPkt.header.size  = static_cast<uint16_t>(sizeof(PKT_Character));
        charPkt.header.id    = PacketID::CHARACTER_INFO;
        charPkt.character_id = charId;
        charPkt.adventure_id = charId;
        charPkt.guild_id     = 0;
        charPkt.job_code     = static_cast<JobCode>(1 + (client.GetId() % 4));
        std::snprintf(charPkt.nickname, sizeof(charPkt.nickname), "Player%d", client.GetId());
        charPkt.state_code   = CharacterState::ALIVE;
        if (!Send(client, charPkt)) continue;

        // 2. 초기 스탯
        PKT_CharacterStat statPkt{};
        statPkt.header.size  = static_cast<uint16_t>(sizeof(PKT_CharacterStat));
        statPkt.header.id    = PacketID::CHARACTER_STAT_INFO;
        statPkt.character_id = charId;
        statPkt.level        = 1;
        statPkt.hp_max       = 10000;
        statPkt.hp           = 10000;
        statPkt.mp_max       = 5000;
        statPkt.mp           = 5000;
        statPkt.is_alive     = Status::NORMAL;
        statPkt.last_map_id  = mapId;
        if (!Send(client, statPkt)) continue;

        // 3. 룸 입장
        PKT_EnterRoom enterPkt{};
        enterPkt.header.size  = static_cast<uint16_t>(sizeof(PKT_EnterRoom));
        enterPkt.header.id    = PacketID::ENTER_ROOM;
        enterPkt.character_id = charId;
        enterPkt.map_id       = mapId;
        if (!Send(client, enterPkt)) continue;

        // 4. 행동 루프 — 맵 이동 + 아이템 획득
        bool broken = false;
        for (int i = 0; i < SCENARIO_LOOPS; ++i) {
            statPkt.level       = static_cast<uint32_t>(1 + (i % 100));
            statPkt.hp          = static_cast<uint32_t>(statPkt.hp_max - (i * 100) % statPkt.hp_max);
            statPkt.mp          = static_cast<uint32_t>(statPkt.mp_max - (i * 50)  % statPkt.mp_max);
            statPkt.last_map_id = static_cast<uint32_t>(1 + (i % 20));
            if (!Send(client, statPkt)) { broken = true; break; }

            if (dropDist(rng) < ITEM_DROP_CHANCE) {
                PKT_ItemInstance itemPkt{};
                itemPkt.header.size      = static_cast<uint16_t>(sizeof(PKT_ItemInstance));
                itemPkt.header.id        = PacketID::ITEM_INSTANCE_INFO;
                itemPkt.item_instance_id = static_cast<uint64_t>(client.GetId()) * 100 + i;
                itemPkt.item_dict_id     = static_cast<uint32_t>(1 + (i % 10));
                itemPkt.count            = 1;
                itemPkt.enhance_level    = 0;
                if (!Send(client, itemPkt)) { broken = true; break; }

                PKT_Inventory invPkt{};
                invPkt.header.size      = static_cast<uint16_t>(sizeof(PKT_Inventory));
                invPkt.header.id        = PacketID::INVENTORY_INFO;
                invPkt.character_id     = charId;
                invPkt.tab_type         = TabType::USE;
                invPkt.slot_index       = static_cast<uint32_t>(i % 64);
                invPkt.item_instance_id = itemPkt.item_instance_id;
                if (!Send(client, invPkt)) { broken = true; break; }
            }
        }
        if (broken) continue;

        // 5. 룸 퇴장
        PKT_LeaveRoom leavePkt{};
        leavePkt.header.size  = static_cast<uint16_t>(sizeof(PKT_LeaveRoom));
        leavePkt.header.id    = PacketID::LEAVE_ROOM;
        leavePkt.character_id = charId;
        leavePkt.map_id       = mapId;
        Send(client, leavePkt);
    }
}

// -------------------------------------------------------
// 모니터 스레드
// -------------------------------------------------------
void MonitorThread(std::chrono::steady_clock::time_point testStart) {
    while (!g_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - testStart).count();

        int    sent = g_packetsSent.load();
        double pps  = elapsedMs > 0 ? (sent * 1000.0 / elapsedMs) : 0.0;

        std::cout
            << std::fixed << std::setprecision(1)
            << "[" << std::setw(5) << elapsedMs / 1000.0 << "s]"
            << "  접속: "   << std::setw(5) << g_connected.load()
            << "  실패: "   << std::setw(4) << g_failed.load()
            << "  패킷: "   << std::setw(8) << sent
            << "  pkt/s: " << std::setw(7) << static_cast<int>(pps)
            << "  오류: "   << g_sendErrors.load()
            << "\n";
    }
}

// -------------------------------------------------------
// main
// -------------------------------------------------------
int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup 실패\n";
        return 1;
    }

    std::cout << "=== RPG 게임 서버 더미 클라이언트 (행동 시뮬레이션) ===\n";
    std::cout << "서버           : " << SERVER_IP << ":" << SERVER_PORT << "\n";
    std::cout << "클라이언트     : " << TARGET_CONNECTIONS << "\n";
    std::cout << "워커 스레드    : " << WORKER_THREADS << "\n";
    std::cout << "시나리오 루프  : " << SCENARIO_LOOPS << "회/클라이언트\n";
    std::cout << "시나리오       : 캐릭터등록 -> 스탯 -> 룸입장 -> 이동+아이템(30%) -> 퇴장\n";
    std::cout << "---\n";

    auto testStart = std::chrono::steady_clock::now();

    std::thread monitor(MonitorThread, testStart);

    std::vector<std::thread> workers;
    workers.reserve(WORKER_THREADS);
    for (int i = 0; i < WORKER_THREADS; ++i)
        workers.emplace_back(WorkerThread, i, testStart);

    for (auto& t : workers)
        t.join();

    g_done.store(true);
    monitor.join();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - testStart).count();

    int     totalPkts  = g_packetsSent.load();
    double  pps        = elapsedMs > 0 ? (totalPkts * 1000.0 / elapsedMs) : 0.0;
    int64_t totalBytes = g_bytesSent.load();

    std::cout << "\n=== 최종 결과 ===\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "소요 시간    : " << elapsedMs / 1000.0 << " 초\n";
    std::cout << "접속 성공    : " << g_connected.load() << " / " << TARGET_CONNECTIONS << "\n";
    std::cout << "접속 실패    : " << g_failed.load()    << "\n";
    std::cout << "총 패킷 송신 : " << totalPkts          << "\n";
    std::cout << "송신 오류    : " << g_sendErrors.load() << "\n";
    std::cout << "평균 처리량  : " << static_cast<int>(pps) << " pkt/s\n";
    std::cout << "총 데이터    : " << totalBytes / 1024   << " KB ("
              << totalBytes << " bytes)\n";

    WSACleanup();
    return 0;
}
