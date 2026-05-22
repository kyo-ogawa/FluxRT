#pragma once
#include "TOP_CPlusPlusBase.h"
#include "FluxRTCtrl.h"
#include "ProcessLauncher.h"
#include <string>
#include <vector>
#include <atomic>

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

    const TD::OP_NodeInfo*  myNodeInfo_;
    TD::TOP_Context*        myContext_;
    ProcessLauncher         launcher_;

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
    bool        lastUseRef_   = false;
    std::string lastRefPath_;

    std::atomic<bool> running_  {false};
    bool              loadRequested_ = false;
    int               execCount_     = 0;
};
