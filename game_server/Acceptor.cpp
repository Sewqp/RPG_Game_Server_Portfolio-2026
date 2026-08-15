#include "Acceptor.h"
#include "IocpCore.h"
#include "Session.h"
#include "SessionManager.h"
#include "AsyncLogger.h"
#include <mswsock.h>

Acceptor::Acceptor(IocpCore& iocpCore)
    : m_iocpCore(iocpCore)
{
}

Acceptor::~Acceptor() {
    if (m_listenSock != INVALID_SOCKET) {
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
    }
}

void Acceptor::Init(uint16_t port) {
    // [리스닝 소켓 생성 — WSA_FLAG_OVERLAPPED 필수]
    m_listenSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
        nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (m_listenSock == INVALID_SOCKET) {
        AsyncLogger::GetInstance().LogError(
            "WSASocket Failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }

    // [SO_REUSEADDR — 포트 즉시 재사용]
    int optval = 1;
    setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<char*>(&optval), sizeof(optval));

    // [bind]
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(m_listenSock, reinterpret_cast<SOCKADDR*>(&addr),
        sizeof(addr)) == SOCKET_ERROR) {
        AsyncLogger::GetInstance().LogError(
            "bind Failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }

    // [listen]
    if (listen(m_listenSock, LISTEN_BACKLOG) == SOCKET_ERROR) {
        AsyncLogger::GetInstance().LogError(
            "listen Failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }

    // [리스닝 소켓을 IOCP에 등록 — completionKey = 0]
    CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(m_listenSock),
        m_iocpCore.GetHandle(),
        0,
        0
    );

    AsyncLogger::GetInstance().Log(
        "Acceptor Init. Port: " + std::to_string(port));

    for (int i = 0; i < PENDING_ACCEPTS; ++i) {
        RegisterAccept();
    }
}

void Acceptor::RegisterAccept() {
    // [클라이언트 소켓 미리 생성]
    SOCKET clientSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
        nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (clientSock == INVALID_SOCKET) {
        AsyncLogger::GetInstance().LogError(
            "WSASocket (client) Failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }

    // [AcceptOverlapped 생성 — unique_ptr로 생성 후 release()로 OS에 전달]
    auto over = std::make_unique<AcceptOverlapped>();
    ZeroMemory(&over->base, sizeof(OVERLAPPED));
    over->io_type = IO_TYPE::ACCEPT;
    over->clientSock = clientSock;

    DWORD bytesReceived = 0;
    BOOL result = AcceptEx(
        m_listenSock,
        clientSock,
        over->addrBuf,              // [주소 저장 버퍼]
        0,                          // [즉시 데이터 없음]
        ADDR_BUFFER_SIZE,           // [로컬 주소 크기]
        ADDR_BUFFER_SIZE,           // [원격 주소 크기]
        &bytesReceived,
        reinterpret_cast<LPOVERLAPPED>(over.get())
    );

    if (result == FALSE && WSAGetLastError() != WSA_IO_PENDING) {
        AsyncLogger::GetInstance().LogError(
            "AcceptEx Failed. Error: " + std::to_string(WSAGetLastError()));
        closesocket(clientSock);
        return;
    }

    // [OS에 전달 완료 — unique_ptr 소유권 포기. 완료 후 OnAcceptCompleted에서 delete]
    over.release();
}

void Acceptor::OnAcceptCompleted(AcceptOverlapped* over) {
    // [SO_UPDATE_ACCEPT_CONTEXT — 필수. 없으면 클라이언트 소켓이 제대로 동작 안 함]
    setsockopt(over->clientSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<char*>(&m_listenSock), sizeof(m_listenSock));

    // [Session 생성]
    auto session = std::make_shared<Session>(over->clientSock);

    // [클라이언트 소켓을 IOCP에 등록 — completionKey = Session 포인터]
    CreateIoCompletionPort(
        reinterpret_cast<HANDLE>(over->clientSock),
        m_iocpCore.GetHandle(),
        reinterpret_cast<ULONG_PTR>(session.get()),
        0
    );

    // [SessionManager에 등록]
    SessionManager::GetInstance().AddSession(session);

    // [수신 시작]
    session->PostRecv();

    AsyncLogger::GetInstance().Log(
        "Session Accepted. ID: " + std::to_string(session->GetId()));

    // [AcceptOverlapped 정리 — release()했던 raw pointer 삭제]
    delete over;

    // [빈 슬롯 보충 — 다시 비동기 Accept 예약]
    RegisterAccept();
}