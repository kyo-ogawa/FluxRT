#pragma once
#include "TOP_CPlusPlusBase.h"
#include "FluxRTCtrl.h"
#include "ProcessLauncher.h"
#include <string>
#include <vector>
#include <atomic>

using namespace TD;

class FluxRTTop : public TOP_CPlusPlusBase {
public:
    FluxRTTop(const OP_NodeInfo* info, TOP_Context* context);
    virtual ~FluxRTTop();

    // ── TOP interface ─────────────────────────────────────────────────────
    void        getGeneralInfo(TOP_GeneralInfo*, const OP_Inputs*, void*) override;
    void        execute(TOP_Output*, const OP_Inputs*, void*) override;

    // ── Info DAT ──────────────────────────────────────────────────────────
    bool        getInfoDATSize(OP_InfoDATSize*, void*) override;
    void        getInfoDATEntries(int32_t, int32_t, OP_InfoDATEntries*, void*) override;

    // ── Parameters ────────────────────────────────────────────────────────
    void        setupParameters(OP_ParameterManager*, void*) override;
    void        pulsePressed(const char* name, void*) override;

private:
    void doLoad(const OP_Inputs* inputs);
    void doUnload();
    void syncParams(const OP_Inputs* inputs);
    std::string statusString() const;

    static void bgraToShm(const uint8_t* bgra, uint8_t* bgr, int w, int h);
    static void shmToBgra(const uint8_t* bgr, uint8_t* bgra, int w, int h);

    const OP_NodeInfo*  myNodeInfo_;
    TOP_Context*        myContext_;
    ProcessLauncher     launcher_;

    int  width_  = 576;
    int  height_ = 320;
    bool int8_   = true;

    std::vector<uint8_t>  inputBGRA_;
    std::vector<uint8_t>  outputBGRA_;

    OP_SmartRef<OP_TOPDownloadResult> prevDownRes_;

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
