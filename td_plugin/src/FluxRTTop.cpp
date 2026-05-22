#include "FluxRTTop.h"
#include <cstring>
#include <sstream>

using namespace TD;

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

void FluxRTTop::setupParameters(OP_ParameterManager* manager, void*) {
    // ── Page: Setup ───────────────────────────────────────────────────────────

    {
        OP_NumericParameter np("Load");
        np.label = "Load";
        np.page  = "Setup";
        manager->appendPulse(np);
    }
    {
        OP_NumericParameter np("Unload");
        np.label = "Unload";
        np.page  = "Setup";
        manager->appendPulse(np);
    }
    {
        OP_StringParameter sp("Status");
        sp.label        = "Status";
        sp.page         = "Setup";
        sp.defaultValue = "Unloaded";
        manager->appendString(sp);
    }
    {
        OP_StringParameter sp("Pythonexe");
        sp.label        = "Python Exe";
        sp.page         = "Setup";
        sp.defaultValue = "C:\\Users\\ogawa\\miniconda3\\envs\\fluxrt\\python.exe";
        manager->appendFile(sp);
    }
    {
        OP_StringParameter sp("Configpath");
        sp.label        = "Config Path";
        sp.page         = "Setup";
        sp.defaultValue = "configs/config_with_reference.json";
        manager->appendFile(sp);
    }
    {
        OP_StringParameter sp("Workdir");
        sp.label        = "Work Dir";
        sp.page         = "Setup";
        sp.defaultValue = "C:\\Users\\ogawa\\Work\\Demo\\FluxRT";
        manager->appendFolder(sp);
    }
    {
        OP_StringParameter sp("Resolution");
        sp.label        = "Resolution";
        sp.page         = "Setup";
        sp.defaultValue = "576x320";
        const char* names[]  = { "576x320",    "512x512",    "640x360"    };
        const char* labels[] = { "576 x 320",  "512 x 512",  "640 x 360"  };
        manager->appendStringMenu(sp, 3, names, labels);
    }
    {
        OP_NumericParameter np("Int8mode");
        np.label            = "Int8 Mode";
        np.page             = "Setup";
        np.defaultValues[0] = 1.0;
        manager->appendToggle(np);
    }

    // ── Page: Inference ───────────────────────────────────────────────────────

    {
        OP_StringParameter sp("Prompt");
        sp.label        = "Prompt";
        sp.page         = "Inference";
        sp.defaultValue = "Turn this into oil on canvas art";
        manager->appendString(sp);
    }
    {
        OP_NumericParameter np("Steps");
        np.label            = "Steps";
        np.page             = "Inference";
        np.defaultValues[0] = 2.0;
        np.minValues[0]     = 1.0;
        np.maxValues[0]     = 8.0;
        np.clampMins[0]     = true;
        np.clampMaxes[0]    = true;
        np.minSliders[0]    = 1.0;
        np.maxSliders[0]    = 8.0;
        manager->appendInt(np);
    }
    {
        OP_NumericParameter np("Seed");
        np.label            = "Seed";
        np.page             = "Inference";
        np.defaultValues[0] = 52.0;
        manager->appendInt(np);
    }
    {
        OP_NumericParameter np("Dynamicarea");
        np.label            = "Dynamic Area";
        np.page             = "Inference";
        np.defaultValues[0] = 0.5;
        np.minValues[0]     = 0.0;
        np.maxValues[0]     = 1.0;
        np.clampMins[0]     = true;
        np.clampMaxes[0]    = true;
        np.minSliders[0]    = 0.0;
        np.maxSliders[0]    = 1.0;
        manager->appendFloat(np);
    }

    // ── Page: Reference ───────────────────────────────────────────────────────

    {
        OP_NumericParameter np("Usereference");
        np.label            = "Use Reference";
        np.page             = "Reference";
        np.defaultValues[0] = 0.0;
        manager->appendToggle(np);
    }
    {
        OP_StringParameter sp("Referenceimage");
        sp.label        = "Reference Image";
        sp.page         = "Reference";
        sp.defaultValue = "";
        manager->appendFile(sp);
    }
}

void FluxRTTop::pulsePressed(const char* name, void*) {
    if (!strcmp(name, "Load")) {
        loadRequested_ = true;
    }
    else if (!strcmp(name, "Unload")) {
        doUnload();
    }
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
