#include "RingBuffer.h"
#include <cstring>
#include <algorithm>

bool RingBuffer::Write(const char* data, size_t len) {
    if (len > GetWritableSize())
        return false;

    size_t tailSpace = BUFFER_SIZE - m_writePos;

    if (len <= tailSpace) {
        std::memcpy(m_buffer.data() + m_writePos, data, len);
    }
    else {
        // [버퍼 끝 + 처음으로 나눠서 씀 — wrap-around]
        std::memcpy(m_buffer.data() + m_writePos, data, tailSpace);
        std::memcpy(m_buffer.data(), data + tailSpace, len - tailSpace);
    }

    m_writePos = (m_writePos + len) % BUFFER_SIZE;
    m_dataSize += len;
    return true;
}

bool RingBuffer::Peek(char* dest, size_t len) const {
    if (len > m_dataSize)
        return false;

    size_t tailSpace = BUFFER_SIZE - m_readPos;

    if (len <= tailSpace) {
        std::memcpy(dest, m_buffer.data() + m_readPos, len);
    }
    else {
        // [wrap-around — 끝 + 처음으로 나눠서 읽음]
        std::memcpy(dest, m_buffer.data() + m_readPos, tailSpace);
        std::memcpy(dest + tailSpace, m_buffer.data(), len - tailSpace);
    }

    return true;
}

bool RingBuffer::Read(char* dest, size_t len) {
    if (!Peek(dest, len))
        return false;

    m_readPos = (m_readPos + len) % BUFFER_SIZE;
    m_dataSize -= len;
    return true;
}

void RingBuffer::Ignore(size_t len) {
    // [데이터보다 많이 버리려 하면 있는 것만 버림]
    len = std::min(len, m_dataSize);
    m_readPos = (m_readPos + len) % BUFFER_SIZE;
    m_dataSize -= len;
}

size_t RingBuffer::GetReadableSize() const {
    return m_dataSize;
}

size_t RingBuffer::GetWritableSize() const {
    return BUFFER_SIZE - m_dataSize;
}

std::optional<std::vector<char>> RingBuffer::TryAssemblePacket() {
    // [헤더 크기만큼 데이터가 쌓였는지 먼저 확인]
    if (m_dataSize < sizeof(PacketHeader))
        return std::nullopt;

    // [헤더만 Peek — 읽기 포인터 움직이지 않음]
    PacketHeader header{};
    Peek(reinterpret_cast<char*>(&header), sizeof(PacketHeader));

    // [헤더의 size = 패킷 전체 크기]
    size_t totalSize = header.size;

    if (totalSize < sizeof(PacketHeader) || totalSize > MAX_PACKET_SIZE) {
        Ignore(sizeof(PacketHeader));
        return std::nullopt;
    }

    if (m_dataSize < totalSize)
        return std::nullopt; // [아직 다 안 모임]

    // [완성된 패킷을 vector로 복사 후 읽기 포인터 전진]
    std::vector<char> packet(totalSize);
    Read(packet.data(), totalSize);
    return packet;
}