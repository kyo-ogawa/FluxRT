#include "FluxRTTop.h"
#include <cstring>
#include <sstream>

extern "C" {

DLLEXPORT void FillTOPPluginInfo(TOP_PluginInfo* info) {
    info->apiVersion                      = TOPCPlusPlusAPIVersion;
    info->executeMode                     = TOP_ExecuteMode::CPUMem;
    info->customOPInfo.opType->setString("Fluxrt");
    info->customOPInfo.opLabel->setString("FluxRT");
    info->customOPInfo.opIcon->setString("FRT");
    info->customOPInfo.authorName->setString("FluxRT Project");
    info->customOPInfo.authorEmail->setString("ogawa@bassdrum.org");
    info->customOPInfo.minInputs          = 0;
    info->customOPInfo.maxInputs          = 1;
}

DLLEXPORT TOP_CPlusPlusBase* CreateTOPInstance(const OP_NodeInfo* info, TOP_Context* ctx) {
    return new FluxRTTop(info, ctx);
}

DLLEXPORT void DestroyTOPInstance(TOP_CPlusPlusBase* instance, TOP_Context*) {
    delete static_cast<FluxRTTop*>(instance);
}

} // extern "C"

// ── Constructor / Destructor ──────────────────────────────────────────────────

FluxRTTop::FluxRTTop(const OP_NodeInfo* info, TOP_Context* context)
    : myNodeInfo_(info), myContext_(context)
{
}

FluxRTTop::~FluxRTTop() {
    doUnload();
}

// ── General info ──────────────────────────────────────────────────────────────

void FluxRTTop::getGeneralInfo(TOP_GeneralInfo* ginfo, const OP_Inputs*, void*) {
    ginfo->cookEveryFrameIfAsked = true;
}

// ── Stubs — implemented in later tasks ────────────────────────────────────────

void FluxRTTop::execute(TOP_Output*, const OP_Inputs*, void*) {
    ++execCount_;
}

bool FluxRTTop::getInfoDATSize(OP_InfoDATSize*, void*) {
    return false;
}

void FluxRTTop::getInfoDATEntries(int32_t, int32_t, OP_InfoDATEntries*, void*) {
}

void FluxRTTop::setupParameters(OP_ParameterManager*, void*) {
}

void FluxRTTop::pulsePressed(const char*, void*) {
}

void FluxRTTop::doLoad(const OP_Inputs*) {
}

void FluxRTTop::doUnload() {
    if (launcher_.isRunning()) {
        launcher_.stop();
    }
    running_ = false;
}

void FluxRTTop::syncParams(const OP_Inputs*) {
}

std::string FluxRTTop::statusString() const {
    return "idle";
}

/*static*/ void FluxRTTop::bgraToShm(const uint8_t* bgra, uint8_t* bgr, int w, int h) {
    const int pixels = w * h;
    for (int i = 0; i < pixels; ++i) {
        bgr[i * 3 + 0] = bgra[i * 4 + 0]; // B
        bgr[i * 3 + 1] = bgra[i * 4 + 1]; // G
        bgr[i * 3 + 2] = bgra[i * 4 + 2]; // R
    }
}

/*static*/ void FluxRTTop::shmToBgra(const uint8_t* bgr, uint8_t* bgra, int w, int h) {
    const int pixels = w * h;
    for (int i = 0; i < pixels; ++i) {
        bgra[i * 4 + 0] = bgr[i * 3 + 0]; // B
        bgra[i * 4 + 1] = bgr[i * 3 + 1]; // G
        bgra[i * 4 + 2] = bgr[i * 3 + 2]; // R
        bgra[i * 4 + 3] = 255;             // A
    }
}
