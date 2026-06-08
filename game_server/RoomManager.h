#pragma once
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include "Room.h"

class RoomManager {
public:
    static RoomManager& GetInstance();

    std::shared_ptr<Room> CreateRoom();
    void                  DestroyRoom(uint32_t roomId);
    std::shared_ptr<Room> GetRoom(uint32_t roomId);
    std::shared_ptr<Room> GetOrCreateRoom(uint32_t mapId);

private:
    RoomManager() = default;
    ~RoomManager() = default;
    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    std::unordered_map<uint32_t, std::shared_ptr<Room>> m_rooms;
    mutable std::shared_mutex m_lock;
    std::atomic<uint32_t>     m_roomIdAllocator{ 0 };
};
