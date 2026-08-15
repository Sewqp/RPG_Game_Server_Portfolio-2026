#include "IocpCore.h"
#include "AsyncLogger.h"
#include "SessionManager.h"
#include "Session.h"
#include "Acceptor.h"

IocpCore::IocpCore() : m_hIocp(INVALID_HANDLE_VALUE), m_isRunning(false) {}

IocpCore::~IocpCore() {
    m_isRunning = false;

    // GQCS에서 블로킹 중인 워커를 깨우기 위해 스레드 수만큼 가짜 완료 통보 전송
    if (m_hIocp != INVALID_HANDLE_VALUE) {
        for (size_t i = 0; i < m_workerThreads.size(); ++i) {
            PostQueuedCompletionStatus(m_hIocp, 0, 0, NULL);
        }
    }

    for (auto& thread : m_workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (m_hIocp != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hIocp);
    }
}

void IocpCore::Init() {
    m_hIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (m_hIocp == INVALID_HANDLE_VALUE) {
        AsyncLogger::GetInstance().LogError("IOCP 생성 실패");
    }
}

void IocpCore::Start() {
    m_isRunning = true;

    const int numThreads = std::thread::hardware_concurrency() * 2;
    for (int i = 0; i < numThreads; ++i) {
        m_workerThreads.emplace_back(&IocpCore::WorkerThreadMain, this);
    }
}

void IocpCore::WorkerThreadMain() {
    while (m_isRunning) {
        DWORD      bytesTransferred = 0;
        ULONG_PTR  completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL ret = GetQueuedCompletionStatus(
            m_hIocp,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );

        if (!m_isRunning) break;

        try {
            if (overlapped == nullptr) {
                AsyncLogger::GetInstance().LogError("overlapped가 nullptr입니다.");
                continue;
            }

            ExOverlapped* exOver = reinterpret_cast<ExOverlapped*>(overlapped);

            if (exOver->io_type == IO_TYPE::ACCEPT) {
                AcceptOverlapped* acceptOver = reinterpret_cast<AcceptOverlapped*>(exOver);
                m_acceptor->OnAcceptCompleted(acceptOver);
                continue;
            }

            // bytesTransferred == 0 은 TCP 연결 종료를 의미
            if (bytesTransferred == 0) {
                std::shared_ptr<Session> session = exOver->keepAlive;
                if (session) {
                    SessionManager::GetInstance().RemoveSession(session->GetId());
                }
                continue;
            }

            std::shared_ptr<Session> session = exOver->keepAlive;
            if (!session) continue;

            switch (exOver->io_type) {
            case IO_TYPE::RECV:
                session->OnRecvCompleted(static_cast<int>(bytesTransferred));
                session->PostRecv();
                break;
            case IO_TYPE::SEND:
                session->SendCompleted();
                break;
            default:
                AsyncLogger::GetInstance().LogError("알 수 없는 IO_TYPE입니다.");
                break;
            }
        }
        catch (const std::exception& e) {
            AsyncLogger::GetInstance().LogError(
                "WorkerThread 예외 발생: " + std::string(e.what()));
        }
    }
}