#pragma once
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <windows.h>
#include "Acceptor.h"

class IocpCore {
public:
    IocpCore();
    ~IocpCore();

    void Init();
    void Start();
    void SetAcceptor(Acceptor* acceptor) { m_acceptor = acceptor; }

    HANDLE GetHandle() { return m_hIocp; }

private:
    HANDLE m_hIocp;
    std::vector<std::thread> m_workerThreads;
    std::atomic<bool> m_isRunning;

    void WorkerThreadMain();

    Acceptor* m_acceptor = nullptr;
};