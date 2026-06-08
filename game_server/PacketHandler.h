#pragma once
#include <memory>
#include <vector>
#include "Session.h"
#include "Packet.h"

class PacketHandler {
public:
    static PacketHandler& GetInstance();
    void Handle(std::shared_ptr<Session> session, std::vector<char>& packet);
private:
    PacketHandler() = default;
    ~PacketHandler() = default;
    PacketHandler(const PacketHandler&) = delete;
    PacketHandler& operator=(const PacketHandler&) = delete;

    void OnAdventureInfo(std::shared_ptr<Session>, PKT_Adventure*);
    void OnGuildInfo(std::shared_ptr<Session>, PKT_Guild*);
    void OnCharacterInfo(std::shared_ptr<Session>, PKT_Character*);
    void OnCharacterStat(std::shared_ptr<Session>, PKT_CharacterStat*);
    void OnItemDictionary(std::shared_ptr<Session>, PKT_ItemDictionary*);
    void OnItemInstance(std::shared_ptr<Session>, PKT_ItemInstance*);
    void OnInventory(std::shared_ptr<Session>, PKT_Inventory*);
    void OnAuction(std::shared_ptr<Session>, PKT_Auction*);
    void OnEnterRoom(std::shared_ptr<Session>, PKT_EnterRoom*);
    void OnLeaveRoom(std::shared_ptr<Session>, PKT_LeaveRoom*);
};
