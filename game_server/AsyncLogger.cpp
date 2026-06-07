#include "AsyncLogger.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <vector>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// ── WinHTTP 헬퍼 ────────────────────────────────────────────────────────────

// JSON 문자열 이스케이프 (payload 조립용)
static std::string JsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 32);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
                else r += static_cast<char>(c);
                break;
        }
    }
    return r;
}

// OpenAI-호환 JSON 응답에서 content 필드 추출
static std::string ParseLlmResponse(const std::string& json) {
    const std::string key = "\"content\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();

    std::string result;
    bool esc = false;
    for (size_t i = pos; i < json.size(); ++i) {
        if (esc) {
            switch (json[i]) {
                case '"':  result += '"';  break;
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case '\\': result += '\\'; break;
                default:   result += json[i]; break;
            }
            esc = false;
        } else if (json[i] == '\\') {
            esc = true;
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i];
        }
    }
    return result;
}

// HTTP / HTTPS POST — 실패 시 빈 문자열 반환
static std::string WinHttpPost(const std::string& url, const std::string& body) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wUrl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wUrl.data(), wlen);

    wchar_t szHost[256] = {}, szPath[1024] = {};
    URL_COMPONENTS uc      = {};
    uc.dwStructSize        = sizeof(uc);
    uc.lpszHostName        = szHost;
    uc.dwHostNameLength    = _countof(szHost);
    uc.lpszUrlPath         = szPath;
    uc.dwUrlPathLength     = _countof(szPath);
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) return "";

    HINTERNET hSes = WinHttpOpen(L"GameServer/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (!hSes) return "";

    HINTERNET hCon = WinHttpConnect(hSes, szHost, uc.nPort, 0);
    if (!hCon) { WinHttpCloseHandle(hSes); return ""; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hCon, L"POST", szPath,
        NULL, NULL, NULL, flags);
    if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return ""; }

    BOOL ok = WinHttpSendRequest(hReq,
        L"Content-Type: application/json\r\n", (DWORD)-1,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);

    std::string response;
    if (ok && WinHttpReceiveResponse(hReq, NULL)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::vector<char> buf(avail + 1, '\0');
            DWORD read = 0;
            WinHttpReadData(hReq, buf.data(), avail, &read);
            response.append(buf.data(), read);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hCon);
    WinHttpCloseHandle(hSes);
    return response;
}

// ── AsyncLogger 구현 ────────────────────────────────────────────────────────

AsyncLogger::AsyncLogger() {
    m_loggerThread = std::thread(&AsyncLogger::ProcessLoop, this);
}

AsyncLogger::~AsyncLogger() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isRunning = false;
    }
    m_cv.notify_all();
    if (m_loggerThread.joinable())
        m_loggerThread.join();
}

void AsyncLogger::Configure(const std::string& llmEndpoint,
                              const std::string& discordWebhook) {
    m_llmEndpoint    = llmEndpoint;
    m_discordWebhook = discordWebhook;
}

void AsyncLogger::Log(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logQueue.push(GetTimestamp() + message);
    }
    m_cv.notify_one();
}

void AsyncLogger::LogError(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logQueue.push(GetTimestamp() + "[ERROR] " + message);
        m_aiQueue.push(message);  // AI 파이프라인용 별도 큐
    }
    m_cv.notify_one();
}

void AsyncLogger::ProcessLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_logQueue.empty() || !m_aiQueue.empty() || !m_isRunning;
        });

        if (!m_isRunning && m_logQueue.empty() && m_aiQueue.empty()) break;

        std::string logMsg, aiMsg;
        if (!m_logQueue.empty()) { logMsg = std::move(m_logQueue.front()); m_logQueue.pop(); }
        if (!m_aiQueue.empty())  { aiMsg  = std::move(m_aiQueue.front());  m_aiQueue.pop();  }
        lock.unlock();

        if (!logMsg.empty()) {
            std::cout << logMsg << "\n";
            std::filesystem::create_directories("logs");
            std::ofstream f(GetLogFileName(), std::ios_base::app);
            if (f.is_open()) f << logMsg << "\n";
        }

        // AI 디스패치는 로거 스레드에서만 실행 — IOCP 워커 블로킹 없음
        if (!aiMsg.empty()) SendToAI(aiMsg);
    }
}

void AsyncLogger::SendToAI(const std::string& errorMsg) {
    if (m_llmEndpoint.empty() || m_discordWebhook.empty()) return;

    // 60초 쿨다운 — 에러 폭발 시 LLM 과호출 방지
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(
            now - m_lastAiSendTime).count() < 60) return;
    m_lastAiSendTime = now;

    std::string body =
        "{\"model\":\"local-model\",\"max_tokens\":512,\"messages\":"
        "[{\"role\":\"user\",\"content\":\""
        "게임 서버 에러가 발생했습니다. 원인과 해결 방법을 간략히 분석해주세요.\\n\\n"
        + JsonEscape(errorMsg) + "\"}]}";

    std::string llmResponse = WinHttpPost(m_llmEndpoint, body);
    std::string analysis    = ParseLlmResponse(llmResponse);
    if (analysis.empty()) analysis = "(LLM 분석 실패)";

    SendToDiscord(errorMsg, analysis);
}

void AsyncLogger::SendToDiscord(const std::string& error,
                                 const std::string& analysis) {
    std::string e = error.size()    > 500 ? error.substr(0, 500)    + "..." : error;
    std::string a = analysis.size() > 800 ? analysis.substr(0, 800) + "..." : analysis;

    std::string payload =
        "{\"embeds\":[{"
        "\"title\":\"\\u26a0\\ufe0f \\uac8c\\uc784 \\uc11c\\ubc84 \\uc5d0\\ub7ec \\uac10\\uc9c0\","
        "\"description\":\"**\\uc5d0\\ub7ec:**\\n" + JsonEscape(e) +
        "\\n\\n**LLM \\ubd84\\uc11d:**\\n" + JsonEscape(a) + "\","
        "\"color\":15548997}]}";

    WinHttpPost(m_discordWebhook, payload);
}

std::string AsyncLogger::GetTimestamp() const {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ";
    return oss.str();
}

std::string AsyncLogger::GetLogFileName() const {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::ostringstream oss;
    oss << "logs/server_" << std::put_time(&tm, "%Y%m%d") << ".log";
    return oss.str();
}
