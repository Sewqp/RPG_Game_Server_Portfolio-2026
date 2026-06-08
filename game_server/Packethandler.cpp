#include "PacketHandler.h"
#include "AsyncLogger.h"
#include "DBManager.h"
#include "RedisManager.h"
#include "RoomManager.h"
#include <cstring>

PacketHandler& PacketHandler::GetInstance() {
    static PacketHandler instance;
    return instance;
}

void PacketHandler::Handle(std::shared_ptr<Session> session, std::vector<char>& packet) {
    if (packet.size() < sizeof(PacketHeader)) {
        AsyncLogger::GetInstance().LogError(
            "패킷 크기 오류. Size: " + std::to_string(packet.size()));
        return;
    }

    PacketHeader* header = reinterpret_cast<PacketHeader*>(packet.data());

    switch (header->id) {
    case PacketID::ADVENTURE_INFO:
        OnAdventureInfo(session, reinterpret_cast<PKT_Adventure*>(packet.data()));
        break;
    case PacketID::GUILD_INFO:
        OnGuildInfo(session, reinterpret_cast<PKT_Guild*>(packet.data()));
        break;
    case PacketID::CHARACTER_INFO:
        OnCharacterInfo(session, reinterpret_cast<PKT_Character*>(packet.data()));
        break;
    case PacketID::CHARACTER_STAT_INFO:
        OnCharacterStat(session, reinterpret_cast<PKT_CharacterStat*>(packet.data()));
        break;
    case PacketID::ITEM_DICTIONARY_INFO:
        OnItemDictionary(session, reinterpret_cast<PKT_ItemDictionary*>(packet.data()));
        break;
    case PacketID::ITEM_INSTANCE_INFO:
        OnItemInstance(session, reinterpret_cast<PKT_ItemInstance*>(packet.data()));
        break;
    case PacketID::INVENTORY_INFO:
        OnInventory(session, reinterpret_cast<PKT_Inventory*>(packet.data()));
        break;
    case PacketID::AUCTION_INFO:
        OnAuction(session, reinterpret_cast<PKT_Auction*>(packet.data()));
        break;
    case PacketID::ENTER_ROOM:
        OnEnterRoom(session, reinterpret_cast<PKT_EnterRoom*>(packet.data()));
        break;
    case PacketID::LEAVE_ROOM:
        OnLeaveRoom(session, reinterpret_cast<PKT_LeaveRoom*>(packet.data()));
        break;
    default:
        AsyncLogger::GetInstance().LogError(
            "알 수 없는 PacketID: " + std::to_string(static_cast<uint16_t>(header->id)));
    }
}

void PacketHandler::OnAdventureInfo(std::shared_ptr<Session> session, PKT_Adventure* packet) {
    AsyncLogger::GetInstance().Log(
        "ADVENTURE_INFO 수신. adventure_id=" + std::to_string(packet->adventure_id) +
        " name=" + std::string(packet->adventure_name, strnlen(packet->adventure_name, sizeof(packet->adventure_name))));
}

void PacketHandler::OnGuildInfo(std::shared_ptr<Session> session, PKT_Guild* packet) {
    AsyncLogger::GetInstance().Log(
        "GUILD_INFO 수신. guild_id=" + std::to_string(packet->guild_id) +
        " name=" + std::string(packet->guild_name, strnlen(packet->guild_name, sizeof(packet->guild_name))));
}

void PacketHandler::OnCharacterInfo(std::shared_ptr<Session> session, PKT_Character* packet) {
    AsyncLogger::GetInstance().Log(
        "CHARACTER_INFO 수신. character_id=" + std::to_string(packet->character_id) +
        " nickname=" + std::string(packet->nickname, strnlen(packet->nickname, sizeof(packet->nickname))));

    if (!DBManager::GetInstance().InsertCharacter(*packet)) {
        AsyncLogger::GetInstance().LogError(
            "CHARACTER_INFO DB 저장 실패. character_id=" + std::to_string(packet->character_id));
    }
}

void PacketHandler::OnCharacterStat(std::shared_ptr<Session> session, PKT_CharacterStat* packet) {
    // Redis 캐시에 먼저 기록 — IOCP 워커 블로킹 방지
    // MySQL 반영은 SyncWorker가 30초 주기로 처리
    if (!RedisManager::GetInstance().SetCharacterStat(packet->character_id, *packet)) {
        AsyncLogger::GetInstance().LogError(
            "CHARACTER_STAT_INFO Redis 저장 실패. character_id=" + std::to_string(packet->character_id));
    }
}

void PacketHandler::OnItemDictionary(std::shared_ptr<Session> session, PKT_ItemDictionary* packet) {
    AsyncLogger::GetInstance().Log(
        "ITEM_DICTIONARY_INFO 수신. item_dict_id=" + std::to_string(packet->item_dict_id) +
        " name=" + std::string(packet->item_name, strnlen(packet->item_name, sizeof(packet->item_name))));
}

void PacketHandler::OnItemInstance(std::shared_ptr<Session> session, PKT_ItemInstance* packet) {
    AsyncLogger::GetInstance().Log(
        "ITEM_INSTANCE_INFO 수신. item_instance_id=" + std::to_string(packet->item_instance_id) +
        " count=" + std::to_string(packet->count) +
        " enhance=" + std::to_string(packet->enhance_level));
}

void PacketHandler::OnInventory(std::shared_ptr<Session> session, PKT_Inventory* packet) {
    AsyncLogger::GetInstance().Log(
        "INVENTORY_INFO 수신. character_id=" + std::to_string(packet->character_id) +
        " slot=" + std::to_string(packet->slot_index));

    if (!DBManager::GetInstance().InsertInventory(*packet)) {
        AsyncLogger::GetInstance().LogError(
            "INVENTORY_INFO DB 저장 실패. character_id=" + std::to_string(packet->character_id) +
            " slot=" + std::to_string(packet->slot_index));
    }
}

void PacketHandler::OnAuction(std::shared_ptr<Session> session, PKT_Auction* packet) {
    AsyncLogger::GetInstance().Log(
        "AUCTION_INFO 수신. auction_id=" + std::to_string(packet->auction_id) +
        " seller_id=" + std::to_string(packet->seller_id) +
        " price=" + std::to_string(packet->price));

    if (!DBManager::GetInstance().InsertAuction(*packet)) {
        AsyncLogger::GetInstance().LogError(
            "AUCTION_INFO DB 저장 실패. auction_id=" + std::to_string(packet->auction_id));
    }
}

void PacketHandler::OnEnterRoom(std::shared_ptr<Session> session, PKT_EnterRoom* packet) {
    auto room = RoomManager::GetInstance().GetOrCreateRoom(packet->map_id);
    if (!room->Enter(session)) {
        // 만석 — 새 룸 생성
        room = RoomManager::GetInstance().CreateRoom();
        room->Enter(session);
    }
    AsyncLogger::GetInstance().Log(
        "ENTER_ROOM 처리. character_id=" + std::to_string(packet->character_id) +
        " map_id=" + std::to_string(packet->map_id) +
        " room_id=" + std::to_string(room->GetRoomId()) +
        " players=" + std::to_string(room->GetPlayerCount()));
}

void PacketHandler::OnLeaveRoom(std::shared_ptr<Session> session, PKT_LeaveRoom* packet) {
    auto room = RoomManager::GetInstance().GetRoom(packet->map_id);
    if (!room) return;

    room->Leave(session->GetId());
    if (room->GetPlayerCount() == 0) {
        RoomManager::GetInstance().DestroyRoom(room->GetRoomId());
    }
    AsyncLogger::GetInstance().Log(
        "LEAVE_ROOM 처리. character_id=" + std::to_string(packet->character_id) +
        " map_id=" + std::to_string(packet->map_id));
}
