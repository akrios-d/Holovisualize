#include "GestureClient.h"

#include <cstring>
#include <iostream>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {
// Reads a value out of a tiny, non-nested JSON object — good enough for the
// sidecar's fixed-shape response, avoids pulling in a JSON dependency for
// one small message. Same spirit as server/src/main.cpp's getJsonFloat().
bool findString(const std::string& json, const std::string& key, std::string& out) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return false;
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return false;
    out = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool findFloat(const std::string& json, const std::string& key, float& out) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < json.size() && (json[pos] == ' ')) pos++;
    try {
        size_t consumed = 0;
        out = std::stof(json.substr(pos), &consumed);
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

HandGesture parseGesture(const std::string& name) {
    if (name == "Fist")        return HandGesture::Fist;
    if (name == "OpenHand")    return HandGesture::OpenHand;
    if (name == "Pinch")       return HandGesture::Pinch;
    if (name == "ThumbsUp")    return HandGesture::ThumbsUp;
    if (name == "PointFinger") return HandGesture::PointFinger;
    if (name == "Peace")       return HandGesture::Peace;
    return HandGesture::None;
}

#ifdef _WIN32
// capture.exe's own directory — the sidecar lives at a fixed path relative
// to it (capture/gesture_sidecar/), regardless of the process's current
// working directory when launched.
std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                         s.data(), len, nullptr, nullptr);
    return s;
}
#endif
} // namespace

GestureClient::GestureClient() = default;

GestureClient::~GestureClient() {
    killChild();
}

void GestureClient::killChild() {
#ifdef _WIN32
    if (stdinWrite_)  { CloseHandle(reinterpret_cast<HANDLE>(stdinWrite_));  stdinWrite_  = nullptr; }
    if (stdoutRead_)  { CloseHandle(reinterpret_cast<HANDLE>(stdoutRead_));  stdoutRead_  = nullptr; }
    if (childProcess_) {
        // Closing stdin (above) signals EOF to the child's read loop — give
        // it a moment to exit gracefully before forcing it.
        HANDLE proc = reinterpret_cast<HANDLE>(childProcess_);
        if (WaitForSingleObject(proc, 500) != WAIT_OBJECT_0)
            TerminateProcess(proc, 0);
        CloseHandle(proc);
        childProcess_ = nullptr;
    }
    if (childThread_) { CloseHandle(reinterpret_cast<HANDLE>(childThread_)); childThread_ = nullptr; }
#endif
    running_ = false;
}

bool GestureClient::ensureChildRunning() {
    if (running_) return true;

#ifdef _WIN32
    auto now = std::chrono::steady_clock::now();
    if (now < nextSpawnAttempt_) return false;
    nextSpawnAttempt_ = now + std::chrono::seconds(5);

    std::wstring base = exeDir() + L"\\..\\..\\gesture_sidecar"; // capture/build/Release -> capture/gesture_sidecar
    std::wstring pythonPath = base + L"\\.venv\\Scripts\\python.exe";
    std::wstring scriptPath = base + L"\\sidecar.py";

    if (GetFileAttributesW(pythonPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[GestureClient] sidecar venv not found at " << narrow(pythonPath)
                  << " — see capture/gesture_sidecar/README.md to set it up\n";
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childStdinRead = nullptr, parentStdinWrite = nullptr;
    HANDLE parentStdoutRead = nullptr, childStdoutWrite = nullptr;
    if (!CreatePipe(&childStdinRead, &parentStdinWrite, &sa, 0) ||
        !CreatePipe(&parentStdoutRead, &childStdoutWrite, &sa, 0)) {
        std::cerr << "[GestureClient] CreatePipe failed\n";
        return false;
    }
    // The parent's own ends must not be inherited by the child, or the
    // pipes never see EOF when we close our copy.
    SetHandleInformation(parentStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parentStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = childStdinRead;
    si.hStdOutput = childStdoutWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE); // model-load logs stay visible in capture's own console

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + pythonPath + L"\" \"" + scriptPath + L"\"";
    // CreateProcessW may write into cmdLine's buffer — it's not a literal.
    std::vector<wchar_t> cmdLineBuf(cmdLine.begin(), cmdLine.end());
    cmdLineBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(pythonPath.c_str(), cmdLineBuf.data(), nullptr, nullptr,
                              /*bInheritHandles=*/TRUE, /*creationFlags=*/0,
                              nullptr, base.c_str(), &si, &pi);

    // These are the child's copies now (or failed) — the parent doesn't need them.
    CloseHandle(childStdinRead);
    CloseHandle(childStdoutWrite);

    if (!ok) {
        std::cerr << "[GestureClient] CreateProcess failed (" << GetLastError() << ")\n";
        CloseHandle(parentStdinWrite);
        CloseHandle(parentStdoutRead);
        return false;
    }

    childProcess_ = pi.hProcess;
    childThread_  = pi.hThread;
    stdinWrite_   = parentStdinWrite;
    stdoutRead_   = parentStdoutRead;
    running_      = true;
    std::cout << "[GestureClient] spawned gesture sidecar (pid " << pi.dwProcessId << ")\n";
    return true;
#else
    return false; // sidecar spawning only implemented for Windows
#endif
}

bool GestureClient::writeAll(const uint8_t* data, size_t len) {
#ifdef _WIN32
    size_t sent = 0;
    while (sent < len) {
        DWORD n = 0;
        if (!WriteFile(reinterpret_cast<HANDLE>(stdinWrite_), data + sent,
                        static_cast<DWORD>(len - sent), &n, nullptr) || n == 0)
            return false;
        sent += n;
    }
    return true;
#else
    (void)data; (void)len;
    return false;
#endif
}

bool GestureClient::readAll(uint8_t* data, size_t len) {
#ifdef _WIN32
    size_t got = 0;
    while (got < len) {
        DWORD n = 0;
        if (!ReadFile(reinterpret_cast<HANDLE>(stdoutRead_), data + got,
                       static_cast<DWORD>(len - got), &n, nullptr) || n == 0)
            return false;
        got += n;
    }
    return true;
#else
    (void)data; (void)len;
    return false;
#endif
}

bool GestureClient::detect(const uint8_t* rgb, int w, int h, HandDetection& out) {
    if (!ensureChildRunning()) return false;

    const uint32_t payloadLen = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 3;
    uint8_t lenPrefix[4];
    std::memcpy(lenPrefix, &payloadLen, 4);

    if (!writeAll(lenPrefix, 4) || !writeAll(rgb, payloadLen)) {
        std::cerr << "[GestureClient] sidecar pipe write failed — respawning\n";
        killChild();
        return false;
    }

    uint8_t respLenBuf[4];
    if (!readAll(respLenBuf, 4)) {
        std::cerr << "[GestureClient] sidecar pipe read failed — respawning\n";
        killChild();
        return false;
    }
    uint32_t respLen;
    std::memcpy(&respLen, respLenBuf, 4);
    if (respLen == 0 || respLen > 1'000'000) { // sanity bound
        std::cerr << "[GestureClient] bogus response length " << respLen << " — respawning\n";
        killChild();
        return false;
    }

    std::string json(respLen, '\0');
    if (!readAll(reinterpret_cast<uint8_t*>(json.data()), respLen)) {
        std::cerr << "[GestureClient] sidecar pipe read (body) failed — respawning\n";
        killChild();
        return false;
    }

    // Response shape: {"hands":[{"gesture":"Fist","confidence":0.85,"wrist_x":0.51,"wrist_y":0.43}]}
    // or {"hands":[]} when no hand is visible — only the first hand is used.
    auto handsPos = json.find("\"hands\"");
    if (handsPos == std::string::npos) return false;
    auto arrStart = json.find('[', handsPos);
    auto arrEnd   = json.find(']', arrStart);
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart + 1)
        return false; // empty array — no hand detected this frame

    std::string obj = json.substr(arrStart, arrEnd - arrStart);
    std::string gestureName;
    float confidence = 0.f, wristU = 0.f, wristV = 0.f;
    if (!findString(obj, "gesture", gestureName)) return false;
    findFloat(obj, "confidence", confidence);
    findFloat(obj, "wrist_x", wristU);
    findFloat(obj, "wrist_y", wristV);

    HandGesture g = parseGesture(gestureName);
    if (g == HandGesture::None) return false;

    out.gesture    = g;
    out.confidence = confidence;
    out.wristU     = wristU;
    out.wristV     = wristV;
    return true;
}
