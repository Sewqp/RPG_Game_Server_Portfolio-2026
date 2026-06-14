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
