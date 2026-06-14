#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <optional>
#include "Packet.h"

class RingBuffer {
public:
    static constexpr size_t BUFFER_SIZE    = 16384;
    static constexpr size_t MAX_PACKET_SIZE = 512;

    bool Write(const char* data, size_t len);
    bool Peek(char* dest, size_t len) const;
    bool Read(char* dest, size_t len);
    void Ignore(size_t len);

    size_t GetReadableSize() const;
    size_t GetWritableSize() const;

    std::optional<std::vector<char>> TryAssemblePacket();

private:
    std::array<char, BUFFER_SIZE> m_buffer{};
    size_t m_readPos = 0;
    size_t m_writePos = 0;
    size_t m_dataSize = 0;
};