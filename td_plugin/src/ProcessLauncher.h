#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <atomic>
#include "FluxRTCtrl.h"
#include "WinSharedMem.h"

struct LaunchConfig {
    std::string pythonExe;    // e.g. C:\...\miniconda3\envs\fluxrt\python.exe
    std::string serverScript; // full path to fluxrt_server.py
    std::string workDir;      // FluxRT repo root (where configs/ lives)
    std::string configPath;   // e.g. configs/config_with_reference.json
    int         width  = 576;
    int         height = 320;
    bool        int8   = true;
};

class ProcessLauncher {
public:
    ProcessLauncher() = default;
    ~ProcessLauncher() { forceStop(); }

    // Non-copyable
    ProcessLauncher(const ProcessLauncher&) = delete;
    ProcessLauncher& operator=(const ProcessLauncher&) = delete;

    // Spawn Python server; blocks until FLUXRT_READY (or timeout_ms elapses).
    bool launch(const LaunchConfig& cfg, int timeout_ms = 30000);

    // Write shutdown_flag, wait gracefully, then TerminateProcess.
    void stop();

    // Kill without waiting.
    void forceStop();

    bool isRunning() const;

    // Shared memory accessors (valid after successful launch())
    FluxRTCtrl* ctrl()   const;
    uint8_t*    input()  const;
    uint8_t*    output() const;

    size_t frameBytes() const { return frameBytes_; }

    // Shared memory name accessors for Info DAT
    const std::string& inputShmName()  const { return shmInput_.name();  }
    const std::string& outputShmName() const { return shmOutput_.name(); }
    const std::string& ctrlShmName()   const { return shmCtrl_.name();   }

private:
    HANDLE      hProcess_    = nullptr;
    HANDLE      hStdoutRead_ = nullptr;

    WinSharedMem shmInput_;
    WinSharedMem shmOutput_;
    WinSharedMem shmCtrl_;

    size_t frameBytes_ = 0;

    bool readStartupLines(int timeout_ms,
                          std::string& outInput,
                          std::string& outOutput,
                          std::string& outCtrl);
};
