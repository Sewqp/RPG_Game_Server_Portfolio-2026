# RPG GAME SERVER PORTFOLIO 2026

## 사용 AI 모델
- **Local LLM**: Meta LLaMA 3.1 8B Instruct → Qwen 2.5 Coder 14B (업그레이드)
- **플랫폼**: LM Studio
- **운용 방식**: 외부 서버 전송 없는 완전 로컬 환경. 코드 유출 없음.

---

## 의존성 (Dependencies)
- Windows IOCP (ws2_32, mswsock)
- MySQL Connector/C++ 8.x (`mysqlcppconn.lib`)
- Redis (hiredis)
- Node.js (Express, mysql2, ioredis, winston)

---

## 커밋 히스토리

### First commit
**2026-03-17**
- MySQL 기반 DB 스키마 및 구조 추가

### UPDATE
**2026-03-18**
- LLM 시스템 프롬프트 추가
- RAG 벡터 파일 (db_schema.txt) 추가

### UPDATE
**2026-03-22**
- IOCP 네트워크 엔진 구축 시작
- Session.h / Session.cpp 구현 (PostSend, RegisterSend, SendCompleted)
- IocpCore.h / IocpCore.cpp 구현 (워커 스레드 풀, GQCS 루프)
- SessionManager.h / SessionManager.cpp 구현 (싱글톤, shared_mutex)
- Packet.h 구현 (enum class + #pragma pack(1) DTO 구조체)

### UPDATE
**2026-03-23**
- AsyncLogger.h / AsyncLogger.cpp 구현 (비동기 로거, AI 에러 전송)
- Session 생성자 / 소멸자 / PostRecv 구현 완료
- RingBuffer.h / RingBuffer.cpp 구현 (패킷 조립용 링 버퍼)

### UPDATE
**2026-04-11**
- Acceptor.h / Acceptor.cpp 구현 (AcceptEx 기반 비동기 Accept)
- IocpCore.h 수정 (SetAcceptor, m_acceptor 추가)
- IO_TYPE에 ACCEPT 추가

### UPDATE
**2026-04-12**
- PacketHandler.h / PacketHandler.cpp 구현 (패킷 ID별 디스패처)
- DBManager.h / DBManager.cpp 구현 (MySQL 커넥션 풀 기반 CRUD)
- MySQL Connector/C++ 연동 완료
- LLM 모델 Qwen 2.5 Coder 14B로 업그레이드

### UPDATE
**2026-04-18**
- RedisManager.h/.cpp 구현 (hiredis 기반 Redis 캐시)
- SyncWorker.h/.cpp 구현 (Redis → MySQL 주기적 동기화)
- Redis(hiredis) 연동 완료

### UPDATE
**2026-04-19**
- Room.h/.cpp 구현 (던전 인스턴스)
- RoomManager.h/.cpp 구현 (룸 풀 관리)
- main.cpp 구현 (서버 진입점)
- IocpCore WorkerThreadMain() 완성 (RECV/SEND/ACCEPT 분기)
- C++ 코드 100% 완성

### UPDATE
**2026-04-29**
- node_server/ 폴더 구성 시작
- server.js / db.js / redis.js / constants.js 구현
- routes/character.js 구현 (캐릭터 조회 + 인벤토리)
- routes/auction.js 구현 (거래소 목록/상세)

### UPDATE
**2026-05-06**
- routes/admin.js 구현 (서버 상태 + 로그 조회)
- routes/ranking.js 구현 (레벨/경매 랭킹)
- Node.js 웹 서버 100% 완성

### UPDATE
**2026-05-07**
- README 및 계획서 업데이트

### UPDATE
**2026-05-26**
- DBManager: MySQL X DevAPI → MySQL C API(libmysql) 전환 (안정성 개선)
- AsyncLogger: 콘솔 출력 추가, AI 에러 전송 구조 완성
- Packethandler.cpp 인코딩 수정 (한글 로그 출력 정상화)
- Node.js: logger.js 모듈 분리 (server.js에서 로거 책임 분리)
- DB 더미 데이터 추가 (dummy_data.sql)
- 프로젝트 구조 재편 및 빌드 아티팩트 정리
- PacketHandler DB 연동 구현 (InsertCharacter / UpdateCharacterStat / InsertInventory / InsertAuction 호출)
- Packet.h 주석 인코딩 수정 (CP949 → UTF-8)
- PacketHandler char 배열 안전 처리 (strnlen 적용)

### UPDATE
**2026-05-27**
- 불필요한 주석 제거 (refactor)
- 버그 수정, 파이프라인 연결, 인코딩 정리

### UPDATE
**2026-05-28**
- 더미 클라이언트 추가 (1000 동시 접속 부하 테스트 시작)
- Acceptor listen backlog `SOMAXCONN`으로 확장, AcceptEx 사전 예약 64개로 증가
- 더미 클라이언트 콘솔 UTF-8 출력 설정
- 더미 클라이언트 배치 연결 스태거 추가 (50개씩 3ms 간격)
- `OnCharacterStat` IOCP 워커 블로킹 제거 — DB 직접 호출을 Redis 캐시 쓰기로 교체
- 더미 클라이언트 연결/전송 페이즈 분리 (`std::latch`)
- 10K 동시 접속 목표 달성 — 스레드 풀 클라이언트, AcceptEx 256개, RingBuffer 16KB
- `NOMINMAX` 추가 — Windows `max` 매크로와 `std::max` 충돌 해결
- 스태거 `sleep_for` → `sleep_until` 변경 — 배치 대기 시간 누적 버그 수정
- 아키텍처 다이어그램 및 스트레스 테스트 결과 문서화

### UPDATE
**2026-06-07**
- AsyncLogger: LLM → Discord 에러 파이프라인 구현 완료
- WinHTTP 기반 LM Studio(localhost:1234) 에러 분석 요청 (OpenAI 호환 API)
- Discord 웹훅 embed 전송 (WinHTTP HTTPS, 추가 DLL 없음)
- `m_aiQueue` 분리 — 에러 디스패치를 로거 스레드에서 처리, IOCP 워커 블로킹 제거
- 60초 쿨다운으로 에러 폭발 시 LLM 과호출 방지
- `Configure()` API 추가 — LLM 엔드포인트 / 웹훅 미설정 시 파이프라인 자동 비활성화

---

## 시스템 아키텍처

```
┌─────────────────────────────────────────────────────────────┐
│                     Game Clients                            │
│          (더미 클라이언트 10,000 동시 접속)                  │
└──────────────────────┬──────────────────────────────────────┘
                       │ TCP :9000
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                C++ IOCP Game Server                         │
│                                                             │
│  Acceptor                                                   │
│  └─ AcceptEx × 256 (비동기 다중 Accept)                     │
│                                                             │
│  IocpCore                                                   │
│  └─ Worker Thread Pool (hw_concurrency × 2)                 │
│       ├─ ACCEPT  → Session 생성 & IOCP 등록                 │
│       ├─ RECV    → RingBuffer → PacketHandler               │
│       └─ SEND    → 송신 큐 처리                             │
│                                                             │
│  PacketHandler  (PacketID 1000~1007)                        │
│  └─ OnCharacterStat → RedisManager::SetCharacterStat        │
│                                                             │
│  SyncWorker  (30초 주기)                                    │
│  └─ Redis SCAN → DBManager::UpdateCharacterStat             │
└───────────┬────────────────────────┬────────────────────────┘
            │                        │
            ▼                        ▼
    ┌───────────────┐       ┌────────────────┐
    │     Redis     │       │     MySQL      │
    │  (Write Cache)│──────▶│  (Persistent) │
    │  캐릭터 스탯  │ Flush │  game_server  │
    │  세션 정보    │       │  _schema       │
    └───────┬───────┘       └───────┬────────┘
            │                       │
            └──────────┬────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              Node.js Admin Server  :3000                    │
│                                                             │
│  GET  /character/:id        캐릭터 + 인벤토리 조회          │
│  GET  /auction              거래소 목록                     │
│  GET  /ranking/level        레벨 랭킹                       │
│  GET  /admin/status         서버 상태 (동접, 세션 수)       │
│  GET  /admin/logs           서버 로그 조회                  │
└─────────────────────────────────────────────────────────────┘
```

---

## 스트레스 테스트 결과

더미 클라이언트 (`dummy_client/`) — C++ TCP 클라이언트, PKT_CharacterStat 패킷 사용

| 항목 | 결과 |
|------|------|
| 동시 접속 | **10,000 / 10,000 (성공률 100%)** |
| 총 패킷 송신 | 1,000,000 |
| 처리량 | **259,336 pkt/s** |
| 전송 데이터 | 36MB |
| 소요 시간 | 3.8초 |

> 로컬루프백(127.0.0.1) 환경 기준. `send()`는 커널 송신 버퍼 기록 시점 기준.

**테스트 과정에서 발견 및 수정한 버그:**

1. `listen(backlog=10)` → `SOMAXCONN` — TCP 큐 부족으로 SYN 드롭
2. `OnCharacterStat`에서 MySQL 동기 호출 → IOCP 워커 스레드 전체 블로킹
3. RECV / AcceptEx 완료 이벤트 경합 → `std::latch`로 연결·전송 페이즈 분리
4. 1스레드=1소켓 구조 → 스레드 풀 32개로 전환 (1K → 10K 확장)
5. `sleep_for` 누적 버그 → `sleep_until` 절대시각 기준으로 수정 (95초 → 3.8초)

---

## 프로젝트 구조

```
C++ 게임 서버 (IOCP)
├── Packet.h                ✅ 완성
├── AsyncLogger.h/.cpp      ✅ 완성
├── IocpCore.h/.cpp         ✅ 완성
├── Session.h/.cpp          ✅ 완성
├── SessionManager.h/.cpp   ✅ 완성
├── RingBuffer.h/.cpp       ✅ 완성
├── Acceptor.h/.cpp         ✅ 완성
├── PacketHandler.h/.cpp    ✅ 완성
├── DBManager.h/.cpp        ✅ 완성
├── RedisManager.h/.cpp     ✅ 완성
├── SyncWorker.h/.cpp       ✅ 완성
├── Room.h/.cpp             ✅ 완성
├── RoomManager.h/.cpp      ✅ 완성
└── main.cpp                ✅ 완성

Node.js 웹 서버
├── server.js               ✅ 완성
├── db.js                   ✅ 완성
├── redis.js                ✅ 완성
├── constants.js            ✅ 완성
└── routes/
    ├── character.js        ✅ 완성
    ├── auction.js          ✅ 완성
    ├── admin.js            ✅ 완성
    └── ranking.js          ✅ 완성
```

**진행률**
```
C++       : 14 / 14 파일 완성 (100%)
Node.js   :  8 /  8 파일 완성 (100%)
전체      : 22 / 22 파일 완성 (100%)
```

---

## 완료된 작업
- [x] 더미 클라이언트 제작 (C++ 스레드 풀 기반)
- [x] 10,000명 동시 접속 테스트 — 성공률 100%, 259,336 pkt/s
- [x] LLM → Discord 에러 파이프라인 (AsyncLogger, WinHTTP)
- [ ] 더미 클라이언트 행동 다양화 (실제 유저 시뮬레이션)
- [ ] 인증 서버 구축 (JWT, bcrypt, Redis 세션)
- [ ] 성능 최적화 (메모리 풀 등)

---

## AI 파이프라인 구조

### 개발 파이프라인 (코드 생성)
```
[Qwen 2.5 Coder 14B — 로컬 폐쇄망]
        ↓
system_prom.txt (코딩 규칙 강제)
        +
vector/db_schema.txt (DB 컨텍스트)
        ↓
C++ / Node.js 코드 생성
        ↓
코드 리뷰 & 버그 수정
        ↓
프로젝트 반영
```

### 런타임 에러 파이프라인
```
게임 서버 에러 발생
        ↓
AsyncLogger::LogError()
        ↓ (m_aiQueue — 로거 스레드, IOCP 워커 블로킹 없음)
SendToAI() — WinHTTP POST → LM Studio localhost:1234
        ↓
LLM 원인 분석 + 해결 방법 생성
        ↓
SendToDiscord() — WinHTTP HTTPS → Discord 웹훅
        ↓
Discord 채널에 에러 embed 알람
```
