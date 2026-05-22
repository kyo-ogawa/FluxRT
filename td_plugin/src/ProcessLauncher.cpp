#include "ProcessLauncher.h"
#include <sstream>
#include <vector>
#include <chrono>
#include <stdexcept>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool ProcessLauncher::launch(const LaunchConfig& cfg, int timeout_ms) {
    frameBytes_ = (size_t)cfg.width * cfg.height * 3;

    // Build command line
    std::ostringstream cmd;
    cmd << "\"" << cfg.pythonExe << "\" "
        << "\"" << cfg.serverScript << "\" "
        << "--width "  << cfg.width  << " "
        << "--height " << cfg.height << " "
        << "--config \"" << cfg.configPath << "\"";
    if (cfg.int8) cmd << " --int8";
    std::string cmdStr = cmd.str();
    std::vector<char> cmdBuf(cmdStr.begin(), cmdStr.end());
    cmdBuf.push_back('\0');

    // Create anonymous pipe for stdout
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdoutWrite = nullptr;
    if (!CreatePipe(&hStdoutRead_, &hStdoutWrite, &sa, 0))
        return false;
    SetHandleInformation(hStdoutRead_, HANDLE_FLAG_INHERIT, 0);

    // Spawn process
    STARTUPINFOA si = {};
    si.cb         = sizeof(si);
    si.hStdOutput = hStdoutWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, cmdBuf.data(), nullptr, nullptr,
        TRUE,           // inherit handles
        CREATE_NO_WINDOW,
        nullptr,
        cfg.workDir.empty() ? nullptr : cfg.workDir.c_str(),
        &si, &pi);

    CloseHandle(hStdoutWrite);
    if (!ok) {
        CloseHandle(hStdoutRead_); hStdoutRead_ = nullptr;
        return false;
    }
    hProcess_ = pi.hProcess;
    CloseHandle(pi.hThread);

    // Read startup protocol lines
    std::string nameIn, nameOut, nameCtrl;
    if (!readStartupLines(timeout_ms, nameIn, nameOut, nameCtrl)) {
        forceStop();
        return false;
    }

    // Attach shared memory
    try {
        shmInput_.open(nameIn,   frameBytes_);
        shmOutput_.open(nameOut, frameBytes_);
        shmCtrl_.open(nameCtrl,  4096);
    } catch (const std::exception&) {
        forceStop();
        return false;
    }

    return true;
}

bool ProcessLauncher::readStartupLines(int timeout_ms,
                                        std::string& outIn,
                                        std::string& outOut,
                                        std::string& outCtrl)
{
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    std::string lineBuf;
    char ch;
    DWORD bytesRead;
    bool gotIn = false, gotOut = false, gotCtrl = false;

    auto hasKey = [](const std::string& line, const char* key, std::string& val) {
        std::string prefix = std::string(key) + "=";
        if (line.size() > prefix.size() &&
            line.substr(0, prefix.size()) == prefix) {
            val = line.substr(prefix.size());
            return true;
        }
        return false;
    };

    while (std::chrono::steady_clock::now() < deadline) {
        DWORD avail = 0;
        PeekNamedPipe(hStdoutRead_, nullptr, 0, nullptr, &avail, nullptr);
        if (avail == 0) {
            // Check if process died
            DWORD code = 0;
            if (GetExitCodeProcess(hProcess_, &code) && code != STILL_ACTIVE)
                return false;
            Sleep(10);
            continue;
        }
        if (!ReadFile(hStdoutRead_, &ch, 1, &bytesRead, nullptr) || bytesRead == 0)
            break;
        if (ch == '\n') {
            std::string line = trim(lineBuf);
            lineBuf.clear();
            std::string val;
            if      (hasKey(line, "FLUXRT_INPUT",  val)) { outIn   = val; gotIn   = true; }
            else if (hasKey(line, "FLUXRT_OUTPUT", val)) { outOut  = val; gotOut  = true; }
            else if (hasKey(line, "FLUXRT_CTRL",   val)) { outCtrl = val; gotCtrl = true; }
            else if (line == "FLUXRT_READY")
                return gotIn && gotOut && gotCtrl;
        } else {
            lineBuf += ch;
        }
    }
    return false;
}

void ProcessLauncher::stop() {
    if (!hProcess_) return;
    if (shmCtrl_.isOpen()) {
        auto* ctrl = reinterpret_cast<FluxRTCtrl*>(shmCtrl_.data());
        ctrl->shutdown_flag = 1;
    }
    if (WaitForSingleObject(hProcess_, 4000) != WAIT_OBJECT_0)
        TerminateProcess(hProcess_, 1);
    CloseHandle(hProcess_); hProcess_ = nullptr;
    if (hStdoutRead_) { CloseHandle(hStdoutRead_); hStdoutRead_ = nullptr; }
    shmInput_.close(); shmOutput_.close(); shmCtrl_.close();
}

void ProcessLauncher::forceStop() {
    if (hProcess_) {
        TerminateProcess(hProcess_, 1);
        WaitForSingleObject(hProcess_, 1000);
        CloseHandle(hProcess_); hProcess_ = nullptr;
    }
    if (hStdoutRead_) { CloseHandle(hStdoutRead_); hStdoutRead_ = nullptr; }
    shmInput_.close(); shmOutput_.close(); shmCtrl_.close();
}

bool ProcessLauncher::isRunning() const {
    if (!hProcess_) return false;
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(hProcess_, &code);
    return code == STILL_ACTIVE;
}

FluxRTCtrl* ProcessLauncher::ctrl()   const {
    return reinterpret_cast<FluxRTCtrl*>(shmCtrl_.data());
}
uint8_t* ProcessLauncher::input()  const {
    return reinterpret_cast<uint8_t*>(shmInput_.data());
}
uint8_t* ProcessLauncher::output() const {
    return reinterpret_cast<uint8_t*>(shmOutput_.data());
}
