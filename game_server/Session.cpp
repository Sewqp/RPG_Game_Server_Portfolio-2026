#include "Session.h"
#include "AsyncLogger.h"
#include "PacketHandler.h"

extern std::atomic<uint64_t> GSessionIdAllocator;

Session::Session(SOCKET sock)
    : m_sock(sock)
    , m_sessionId(++GSessionIdAllocator)
{
    ZeroMemory(&m_recvOverlapped, sizeof(m_recvOverlapped));
    ZeroMemory(&m_sendOverlapped, sizeof(m_sendOverlapped));
    AsyncLogger::GetInstance().Log("Session Created. ID: " + std::to_string(m_sessionId));
}

Session::~Session() {
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    AsyncLogger::GetInstance().Log("Session Destroyed. ID: " + std::to_string(m_sessionId));
}

void Session::PostRecv() {
    ZeroMemory(&m_recvOverlapped, sizeof(OVERLAPPED));
    m_recvOverlapped.io_type = IO_TYPE::RECV;
    m_recvOverlapped.keepAlive = shared_from_this();
    m_recvOverlapped.wsa_buf.buf = m_recvBuffer.data();
    m_recvOverlapped.wsa_buf.len = static_cast<ULONG>(m_recvBuffer.size());

    DWORD flags = 0;
    int result = WSARecv(
        m_sock,
        &m_recvOverlapped.wsa_buf,
        1,
        NULL,
        &flags,
        reinterpret_cast<LPWSAOVERLAPPED>(&m_recvOverlapped),
        NULL
    );

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            AsyncLogger::GetInstance().LogError(
                "PostRecv Failed. Error Code: " + std::to_string(error));
        }
    }
}

void Session::PostSend(char* data, size_t size) {
    std::vector<char> sendData(data, data + size);
    {
        std::lock_guard<std::mutex> lock(m_sendLock);
        m_sendQueue.push(std::move(sendData));
    }
    if (m_isSending.exchange(true) == false) {
        RegisterSend();
    }
}

void Session::RegisterSend() {
    std::lock_guard<std::mutex> lock(m_sendLock);

    if (m_sendQueue.empty()) {
        m_isSending = false;
        return;
    }

    std::vector<char>& data = m_sendQueue.front();

    // [base만 초기화 — shared_ptr까지 날리면 안 됨]
    ZeroMemory(&m_sendOverlapped, sizeof(OVERLAPPED));
    m_sendOverlapped.io_type = IO_TYPE::SEND;
    m_sendOverlapped.wsa_buf.buf = data.data();
    m_sendOverlapped.wsa_buf.len = static_cast<ULONG>(data.size());
    m_sendOverlapped.keepAlive = shared_from_this(); // [Overlapped에 수명 연결]

    DWORD sendBytes = 0;
    int result = WSASend(
        m_sock,
        &m_sendOverlapped.wsa_buf,
        1,
        &sendBytes,
        0,
        reinterpret_cast<LPWSAOVERLAPPED>(&m_sendOverlapped),
        NULL
    );

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            m_isSending = false;
            AsyncLogger::GetInstance().LogError(
                "WSASend Failed. Error Code: " + std::to_string(error));
        }
    }
}

void Session::SendCompleted() {
    {
        std::lock_guard<std::mutex> lock(m_sendLock);
        m_sendQueue.pop();
    }
    RegisterSend();
}
void Session::OnRecvCompleted(int bytes) {
    if (!m_ringBuffer.Write(m_recvBuffer.data(), bytes)) {
        AsyncLogger::GetInstance().LogError(
            "RingBuffer 오버플로우. SessionID: " + std::to_string(m_sessionId));
        return;
    }
    while (auto pkt = m_ringBuffer.TryAssemblePacket())
        PacketHandler::GetInstance().Handle(shared_from_this(), *pkt);
}