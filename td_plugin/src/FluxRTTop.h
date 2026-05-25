#pragma once
#include "TOP_CPlusPlusBase.h"
#include "FluxRTCtrl.h"
#include "ProcessLauncher.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

class FluxRTTop : public TD::TOP_CPlusPlusBase {
public:
    FluxRTTop(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
    virtual ~FluxRTTop();

    // ── TOP interface ─────────────────────────────────────────────────────
    void        getGeneralInfo(TD::TOP_GeneralInfo*, const TD::OP_Inputs*, void*) override;
    void        execute(TD::TOP_Output*, const TD::OP_Inputs*, void*) override;

    // ── Info DAT ──────────────────────────────────────────────────────────
    bool        getInfoDATSize(TD::OP_InfoDATSize*, void*) override;
    void        getInfoDATEntries(int32_t, int32_t, TD::OP_InfoDATEntries*, void*) override;

    // ── Parameters ────────────────────────────────────────────────────────
    void        setupParameters(TD::OP_ParameterManager*, void*) override;
    void        pulsePressed(const char* name, void*) override;

private:
    void doLoad(const TD::OP_Inputs* inputs);
    void doUnload();
    void syncParams(const TD::OP_Inputs* inputs);
    std::string statusString() const;

    static void bgraToShm(const uint8_t* bgra, uint8_t* bgr, int w, int h);
    static void shmToBgra(const uint8_t* bgr, uint8_t* bgra, int w, int h);
    static bool saveBgr24Bmp(const uint8_t* bgra, int w, int h,
                              const std::string& path);

    const TD::OP_NodeInfo*  myNodeInfo_;
    TD::TOP_Context*        myContext_;
    ProcessLauncher         launcher_;

    std::string workDir_;  // stored in doLoad() for temp file paths

    int  width_  = 576;
    int  height_ = 320;
    bool int8_   = true;

    std::vector<uint8_t>  inputBGRA_;
    std::vector<uint8_t>  outputBGRA_;

    TD::OP_SmartRef<TD::OP_TOPDownloadResult> prevDownRes_;

    std::string lastPrompt_;
    int         lastSteps_    = -1;
    int         lastSeed_     = -1;
    float       lastDynArea_  = -1.f;
    bool        lastUseRef_      = false;
    bool        lastLipTransfer_ = false;
    std::string lastRefPath_;

    std::atomic<bool> running_         {false};
    std::atomic<bool> loadRequested_   {false};
    std::atomic<bool> unloadRequested_ {false};
    int               execCount_       = 0;

    // Input diagnostic (updated each frame)
    int  lastInputW_        = 0;
    int  lastInputH_        = 0;
    bool lastInputAccepted_ = false;

    // Install state
    void        doInstall(const TD::OP_Inputs* inputs);
    static DWORD WINAPI installReaderThread(LPVOID param);

    HANDLE              installProcess_    = nullptr;
    HANDLE              installThread_     = nullptr;
    HANDLE              installStdoutRead_ = nullptr;
    mutable std::mutex  installMutex_;
    std::string         installStatus_;
    std::atomic<bool>   installing_        {false};
    std::atomic<bool>   installRequested_  {false};

    // Reference TOP (input 1)
    int64_t lastRefTotalCooks_ = -1;
    int     refGeneration_     = 0;
    TD::OP_SmartRef<TD::OP_TOPDownloadResult> prevRefDownRes_;
    std::string tempRefBmpPath_;
    std::string prevRefBmpPath_;
};
