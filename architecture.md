# System Architecture

## Overview

```mermaid
flowchart TD
    DC["**Dummy Client** · C++\n10,000 connections / 1,000,000 pkts\n32 worker threads · std::latch sync"]
    Browser["**Browser / REST Client**"]

    subgraph CPP["  C++ Game Server — IOCP  "]
        direction TB
        Acceptor["**Acceptor**\nAcceptEx ×256 pre-posted\nSOMAXCONN backlog"]
        IocpCore["**IOCP Core**\nGetQueuedCompletionStatus\nWorker Threads ×32"]
        Session["**Session**\nRingBuffer 16 KB\nSendQueue · atomic isSending"]
        PH["**PacketHandler**\n10 packet IDs · switch dispatch"]
        DB_MGR["**DBManager**\nConnection Pool ×10\nMYSQL_STMT · 5 s timeout"]
        REDIS_MGR["**RedisManager**\nhiredis · character:stat:{id}\ngame_session:{id} · TTL 3600"]
        ROOM_MGR["**RoomManager**\nGetOrCreate · map_id key\nRoom MAX_PLAYERS = 4\nshared_mutex broadcast"]
        SYNC["**SyncWorker**\n30 s periodic thread\nSMEMBERS dirty_characters → DEL → MySQL UPDATE"]
        LOG["**AsyncLogger**\nDedicated logger thread\n60 s LLM cooldown\nWinHTTP · file + console"]
    end

    subgraph NODE["  Node.js Auth Server — Express 5  "]
        direction TB
        MW["**authMiddleware**\nBearer JWT verify\nRedis session TTL check"]
        AUTH["**POST /auth/login**\nbcrypt.compare · JWT HS256\nRedis auth_session:{id}"]
        CHAR["**GET /api/character/:id**\ncache-aside · Redis → MySQL JOIN"]
        AUCTION["**GET /api/auction**\nLIMIT/OFFSET pagination\ntrade_status filter"]
        RANK["**GET /api/ranking/level**\nTOP 100 · ORDER BY level DESC"]
        ADMIN["**GET /api/admin/status**\nRedis SCAN session count\nGET /api/admin/logs"]
        WLOG["**winston logger**\nDailyRotateFile\nLLM error transport · 60 s cooldown"]
    end

    MySQL[("**MySQL 8.0**\nadventure · guild\ncharacter · character_stat\nitem_dictionary · item_instance\ninventory · auction")]
    RedisDB[("**Redis 6.x**\ncharacter:stat:{id}\nauth_session:{id}\ngame_session:{id}\ndirty_characters (Set)")]
    LM["**LM Studio**\nlocalhost:1234\nOpenAI-compatible API"]
    Discord["**Discord Webhook**\nEmbed — error + LLM analysis"]

    DC -->|"TCP · PKT_Character\nPKT_CharacterStat\nPKT_EnterRoom · LeaveRoom\nPKT_ItemInstance · Inventory\nPKT_Auction · Adventure\nPKT_Guild · ItemDictionary"| Acceptor
    Acceptor -->|"new Session\nIOCP register"| IocpCore
    IocpCore -->|"WSARecv / WSASend\nOverlapped I/O"| Session
    Session -->|"TryAssemblePacket\nsize ≤ 512 B guard"| PH
    PH -->|"INSERT character\ninventory · auction"| DB_MGR
    PH -->|"SET character:stat\nSADD dirty_characters\ngame_session"| REDIS_MGR
    PH -->|"Enter / Leave\nBroadcast"| ROOM_MGR
    SYNC -->|"SMEMBERS dirty_characters\nDEL dirty_characters\nGET → UPDATE"| REDIS_MGR
    SYNC -->|"UpdateCharacterStat\nbatch every 30 s"| DB_MGR
    DB_MGR -->|"libmysql · pool ×10"| MySQL
    REDIS_MGR -->|"hiredis commands"| RedisDB
    LOG -->|"WinHTTP POST\nerror payload"| LM
    LM -->|"JSON analysis\ncontent field"| Discord

    Browser -->|"POST /auth/register\nPOST /auth/login"| AUTH
    Browser -->|"Authorization: Bearer"| MW
    MW --> CHAR
    MW --> AUCTION
    MW --> RANK
    MW --> ADMIN
    AUTH -->|"SET auth_session:{id}\nEX 3600"| RedisDB
    CHAR -->|"GET character:stat:{id}\nfallback JOIN"| RedisDB
    CHAR -->|"character + character_stat\ninventory JOIN"| MySQL
    AUCTION -->|"auction + item_instance\n+ item_dictionary JOIN"| MySQL
    RANK -->|"character + character_stat\nORDER BY level DESC"| MySQL
    ADMIN -->|"SCAN session:*\ncount"| RedisDB
    WLOG -->|"POST error"| LM

    style CPP fill:#0d1b2a,stroke:#4a90d9,color:#cce4ff
    style NODE fill:#0d2a0d,stroke:#4caf50,color:#ccffcc
    style MySQL fill:#2a1a0d,stroke:#ff9800,color:#ffe0b2
    style RedisDB fill:#2a0d0d,stroke:#f44336,color:#ffccbc
    style LM fill:#1a0d2a,stroke:#9c27b0,color:#e1bee7
    style Discord fill:#0d1a2a,stroke:#5865f2,color:#c5cae9
```

---

## Packet ID Table

| ID   | Struct               | Size   | Direction      | Storage       |
|------|----------------------|--------|----------------|---------------|
| 1000 | PKT_Adventure        | 74 B   | Client → Server | MySQL         |
| 1001 | PKT_Guild            | 88 B   | Client → Server | MySQL         |
| 1002 | PKT_Character        | 107 B  | Client → Server | MySQL         |
| 1003 | PKT_CharacterStat    | 37 B   | Client → Server | Redis → MySQL |
| 1004 | PKT_ItemDictionary   | 359 B  | Client → Server | MySQL         |
| 1005 | PKT_ItemInstance     | 64 B   | Client → Server | MySQL         |
| 1006 | PKT_Inventory        | 25 B   | Client → Server | MySQL         |
| 1007 | PKT_Auction          | 73 B   | Client → Server | MySQL         |
| 1008 | PKT_EnterRoom        | 16 B   | Client → Server | RoomManager   |
| 1009 | PKT_LeaveRoom        | 16 B   | Client → Server | RoomManager   |

> `MAX_PACKET_SIZE = 512 B` — RingBuffer rejects any packet header declaring a size above this threshold.

---

## Thread Model

| Thread              | Count | Responsibility                                      |
|---------------------|-------|-----------------------------------------------------|
| IOCP Worker         | ×32   | Accept / Recv / Send completion dispatch            |
| SyncWorker          | ×1    | Redis → MySQL batch flush every 30 s                |
| AsyncLogger         | ×1    | File I/O · LM Studio POST · Discord webhook         |
| Node.js Event Loop  | ×1    | Express routing · mysql2 pool · ioredis             |

---

## Data Flow: CharacterStat (Write Path)

```
Dummy Client
  │  PKT_CharacterStat (37 B)
  ▼
Session::OnRecvCompleted
  │  RingBuffer::TryAssemblePacket
  ▼
PacketHandler::OnCharacterStat
  │  RedisManager::SetCharacterStat
  │    SET  character:stat:{id}  binary  EX 3600
  │    SADD dirty_characters     {id}
  ▼
Redis  ←── hot path (sub-ms)

SyncWorker (every 30 s)
  │  RedisManager::PopDirtyIds
  │    SMEMBERS dirty_characters  → id 목록 수집
  │    DEL      dirty_characters  → Set 초기화
  │  DBManager::UpdateCharacterStat [MYSQL_STMT]
  ▼
MySQL  ←── cold path (변경된 캐릭터만 · O(dirty 수))
```

---

## Data Flow: CharacterStat (Read Path — Node.js)

```
GET /api/character/:id
  │
authMiddleware (JWT verify + Redis session check)
  │
Redis GET "character:stat:{id}"
  ├── HIT  → return JSON  (no MySQL query)
  └── MISS → MySQL SELECT (character JOIN character_stat)
               │
               └── Redis SET "character:stat:{id}" EX 3600
```

---

## Class Diagram

```mermaid
classDiagram
    class IocpCore {
        -HANDLE m_hIocp
        -vector~thread~ m_workerThreads
        -atomic~bool~ m_isRunning
        -Acceptor* m_acceptor
        +Init()
        +Start()
        +SetAcceptor(Acceptor*)
        +GetHandle() HANDLE
        -WorkerThreadMain()
    }

    class Acceptor {
        -IocpCore& m_iocpCore
        -SOCKET m_listenSock
        +PENDING_ACCEPTS = 256
        +Init(port uint16_t)
        +OnAcceptCompleted(AcceptOverlapped*)
        -RegisterAccept()
    }

    class AcceptOverlapped {
        <<struct>>
        +OVERLAPPED base
        +IO_TYPE io_type
        +SOCKET clientSock
        +char addrBuf[128]
    }

    class Session {
        -SOCKET m_sock
        -uint64_t m_sessionId
        -array~char 8192~ m_recvBuffer
        -RingBuffer m_ringBuffer
        -ExOverlapped m_recvOverlapped
        -ExOverlapped m_sendOverlapped
        -mutex m_sendLock
        -queue~vector~char~~ m_sendQueue
        -atomic~bool~ m_isSending
        +GetId() uint64_t
        +PostRecv()
        +PostSend(data, size)
        +OnRecvCompleted(bytes int)
        +SendCompleted()
        -RegisterSend()
    }

    class ExOverlapped {
        <<struct>>
        +OVERLAPPED base
        +IO_TYPE io_type
        +WSABUF wsa_buf
        +shared_ptr~Session~ keepAlive
    }

    class RingBuffer {
        +BUFFER_SIZE = 16384
        +MAX_PACKET_SIZE = 512
        -array~char 16384~ m_buffer
        -size_t m_readPos
        -size_t m_writePos
        -size_t m_dataSize
        +Write(data, len) bool
        +Read(dest, len) bool
        +Peek(dest, len) bool
        +GetReadableSize() size_t
        +GetWritableSize() size_t
        +TryAssemblePacket() optional~vector~char~~
    }

    class SessionManager {
        <<Singleton>>
        -unordered_map~uint64_t, Session~ m_sessions
        -shared_mutex m_lock
        +GetInstance() SessionManager&
        +AddSession(shared_ptr~Session~)
        +RemoveSession(uint64_t)
        +Broadcast(data, size)
    }

    class PacketHandler {
        <<Singleton>>
        +GetInstance() PacketHandler&
        +Handle(session, packet~vector~char~~)
        -OnAdventureInfo(session, PKT_Adventure*)
        -OnGuildInfo(session, PKT_Guild*)
        -OnCharacterInfo(session, PKT_Character*)
        -OnCharacterStat(session, PKT_CharacterStat*)
        -OnItemDictionary(session, PKT_ItemDictionary*)
        -OnItemInstance(session, PKT_ItemInstance*)
        -OnInventory(session, PKT_Inventory*)
        -OnAuction(session, PKT_Auction*)
        -OnEnterRoom(session, PKT_EnterRoom*)
        -OnLeaveRoom(session, PKT_LeaveRoom*)
    }

    class DBManager {
        <<Singleton>>
        +CONNECTION_POOL_SIZE = 10
        -vector~MYSQL*~ m_connectionPool
        -mutex m_poolLock
        -condition_variable m_poolCondition
        +Init(host, user, password, schema)
        +InsertCharacter(PKT_Character) bool
        +UpdateCharacterStat(PKT_CharacterStat) bool
        +SelectCharacter(id, out) bool
        +InsertInventory(PKT_Inventory) bool
        +InsertAuction(PKT_Auction) bool
        +UpdateAuctionStatus(id, TradeStatus) bool
        -GetConnection() MYSQL*
        -ReturnConnection(MYSQL*)
    }

    class RedisManager {
        <<Singleton>>
        +EXPIRE_SECONDS = 3600
        +DIRTY_SET_KEY = "dirty_characters"
        -redisContext* m_context
        -mutex m_lock
        +Init(host, port)
        +SetCharacterStat(id, stat) bool
        +GetCharacterStat(id, out) bool
        +DeleteCharacterStat(id) bool
        +SetSession(sessionId, characterId) bool
        +GetSession(sessionId, out) bool
        +DeleteSession(sessionId) bool
        +PopDirtyIds() vector~uint64_t~
        -MakeCharacterStatKey(id) string
        -MakeSessionKey(id) string
    }

    class SyncWorker {
        <<Singleton>>
        +SYNC_INTERVAL_SECONDS = 30
        -thread m_syncThread
        -atomic~bool~ m_isRunning
        -mutex m_mutex
        -condition_variable m_cv
        +Start()
        +Stop()
        -SyncLoop()
        -FlushAll()
    }

    class Room {
        +MAX_PLAYERS = 4
        -uint32_t m_roomId
        -unordered_map~uint64_t, Session~ m_sessions
        -shared_mutex m_lock
        +Enter(shared_ptr~Session~) bool
        +Leave(uint64_t)
        +Broadcast(data, size)
        +GetRoomId() uint32_t
        +GetPlayerCount() int
    }

    class RoomManager {
        <<Singleton>>
        -unordered_map~uint32_t, Room~ m_rooms
        -shared_mutex m_lock
        -atomic~uint32_t~ m_roomIdAllocator
        +CreateRoom() shared_ptr~Room~
        +DestroyRoom(uint32_t)
        +GetRoom(uint32_t) shared_ptr~Room~
        +GetOrCreateRoom(uint32_t) shared_ptr~Room~
    }

    class AsyncLogger {
        <<Singleton>>
        -queue~string~ m_logQueue
        -queue~string~ m_aiQueue
        -thread m_loggerThread
        -atomic~bool~ m_isRunning
        -string m_llmEndpoint
        -string m_discordWebhook
        -time_point m_lastAiSendTime
        +Configure(llmEndpoint, discordWebhook)
        +Log(message)
        +LogError(message)
        -ProcessLoop()
        -SendToAI(errorMsg)
        -SendToDiscord(error, analysis)
    }

    class PacketHeader {
        <<struct>>
        +uint16_t size
        +PacketID id
    }

    class PacketID {
        <<enumeration>>
        ADVENTURE_INFO = 1000
        GUILD_INFO = 1001
        CHARACTER_INFO = 1002
        CHARACTER_STAT_INFO = 1003
        ITEM_DICTIONARY_INFO = 1004
        ITEM_INSTANCE_INFO = 1005
        INVENTORY_INFO = 1006
        AUCTION_INFO = 1007
        ENTER_ROOM = 1008
        LEAVE_ROOM = 1009
    }

    class IO_TYPE {
        <<enumeration>>
        RECV
        SEND
        ACCEPT
    }

    %% Composition / Aggregation
    IocpCore *-- Acceptor : m_acceptor*
    Acceptor --> IocpCore : m_iocpCore (ref)
    Acceptor ..> AcceptOverlapped : creates / deletes
    Acceptor ..> Session : creates
    Acceptor ..> SessionManager : AddSession
    Session *-- RingBuffer : m_ringBuffer
    Session *-- ExOverlapped : m_recvOverlapped\nm_sendOverlapped
    ExOverlapped --> Session : keepAlive (shared_ptr)
    SessionManager o-- Session : m_sessions (shared_ptr)
    PacketHandler ..> DBManager : uses
    PacketHandler ..> RedisManager : uses
    PacketHandler ..> RoomManager : uses
    PacketHandler ..> AsyncLogger : uses
    Session ..> PacketHandler : OnRecvCompleted→Handle
    RoomManager o-- Room : m_rooms (shared_ptr)
    Room o-- Session : m_sessions (shared_ptr)
    SyncWorker ..> RedisManager : PopDirtyIds
    SyncWorker ..> DBManager : UpdateCharacterStat
    PacketHeader --> PacketID : id
    ExOverlapped --> IO_TYPE : io_type
    AcceptOverlapped --> IO_TYPE : io_type
```

---

## Sequence Diagram — 접속부터 종료까지

```mermaid
sequenceDiagram
    participant C  as DummyClient
    participant A  as Acceptor
    participant W  as IocpCore<br/>WorkerThread
    participant S  as Session
    participant SM as SessionManager
    participant RB as RingBuffer
    participant PH as PacketHandler
    participant RD as RedisManager
    participant DB as DBManager
    participant RM as RoomManager
    participant SW as SyncWorker
    participant LOG as AsyncLogger

    Note over A,W: 서버 시작 — AcceptEx 256개 사전 예약

    %% ── 1. 접속 ──────────────────────────────────────────────
    C  ->> A  : TCP connect
    A  -->> W : IOCP 완료 (IO_TYPE::ACCEPT)
    W  ->> A  : OnAcceptCompleted()
    A  ->> S  : new Session(clientSock)
    A  ->> W  : CreateIoCompletionPort(clientSock, IOCP)
    A  ->> SM : AddSession(session)
    A  ->> S  : PostRecv()
    S  -->> W : WSARecv 비동기 등록
    A  ->> A  : RegisterAccept() — 슬롯 보충

    %% ── 2. CHARACTER_INFO 수신 ────────────────────────────────
    Note over C,DB: PKT_Character (102 B)
    C  ->> W  : 소켓 데이터 도착
    W  ->> S  : OnRecvCompleted(bytes)
    S  ->> RB : Write(recvBuf, bytes)
    RB -->> S : TryAssemblePacket() → packet
    S  ->> PH : Handle(session, packet)
    PH ->> DB : InsertCharacter(PKT_Character)
    DB -->> PH : true
    S  ->> S  : PostRecv()

    %% ── 3. CHARACTER_STAT 수신 (Redis Write-Through) ──────────
    Note over C,RD: PKT_CharacterStat (37 B)
    C  ->> W  : 소켓 데이터 도착
    W  ->> S  : OnRecvCompleted(bytes)
    S  ->> RB : Write → TryAssemblePacket
    S  ->> PH : Handle()
    PH ->> RD : SetCharacterStat(characterId, stat)
    RD ->> RD : SET character:stat:{id}  EX 3600
    RD ->> RD : SADD dirty_characters  {id}
    RD -->> PH : true
    S  ->> S  : PostRecv()

    %% ── 4. ENTER_ROOM ─────────────────────────────────────────
    Note over C,RM: PKT_EnterRoom (16 B)
    C  ->> W  : 소켓 데이터 도착
    W  ->> S  : OnRecvCompleted(bytes)
    S  ->> PH : Handle()
    PH ->> RM : GetOrCreateRoom(map_id)
    alt 기존 룸에 빈 자리
        RM -->> PH : room (기존)
    else 만석 → 새 룸 생성
        RM ->> RM : CreateRoom()
        RM -->> PH : room (신규)
    end
    PH ->> RM : room.Enter(session)
    PH ->> LOG : Log("ENTER_ROOM ...")
    S  ->> S  : PostRecv()

    %% ── 5. SyncWorker 30초 주기 동기화 ───────────────────────
    Note over SW,DB: 백그라운드 — 30 s 주기
    SW ->> RD : PopDirtyIds()
    RD ->> RD : SMEMBERS dirty_characters
    RD ->> RD : DEL dirty_characters
    RD -->> SW : [id₁, id₂, ...]
    loop dirty id 마다
        SW ->> RD : GetCharacterStat(id)
        RD -->> SW : stat
        SW ->> DB : UpdateCharacterStat(stat)
    end

    %% ── 6. LEAVE_ROOM ─────────────────────────────────────────
    Note over C,RM: PKT_LeaveRoom (16 B)
    C  ->> W  : 소켓 데이터 도착
    W  ->> S  : OnRecvCompleted(bytes)
    S  ->> PH : Handle()
    PH ->> RM : GetRoom(map_id)
    PH ->> RM : room.Leave(sessionId)
    alt 룸 인원 = 0
        RM ->> RM : DestroyRoom(roomId)
    end
    S  ->> S  : PostRecv()

    %% ── 7. 에러 발생 시 ───────────────────────────────────────
    Note over LOG: 에러 파이프라인 (비동기)
    PH ->> LOG : LogError("...")
    LOG ->> LOG : m_aiQueue push
    Note right of LOG: 로거 스레드에서 처리
    LOG ->> LOG : SendToAI(LM Studio :1234)
    LOG ->> LOG : SendToDiscord(webhook)

    %% ── 8. 연결 종료 ──────────────────────────────────────────
    Note over C,S: 클라이언트 연결 종료 (TCP FIN)
    C  ->> W  : 소켓 데이터 (bytesTransferred = 0)
    W  ->> SM : RemoveSession(sessionId)
    SM -->> S : shared_ptr refcount → 0
    S  ->> S  : ~Session() — closesocket()
    LOG ->> LOG : Log("Session Destroyed. ID: ...")
```
