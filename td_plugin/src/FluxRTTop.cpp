#include "FluxRTTop.h"
#include <cstring>
#include <cstdio>

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
    info->customOPInfo.maxInputs          = 2;
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
    outputBGRA_.resize((size_t)width_ * height_ * 4, 0);
}

FluxRTTop::~FluxRTTop() {
    doUnload();
}

// ── General info ──────────────────────────────────────────────────────────────

void FluxRTTop::getGeneralInfo(TOP_GeneralInfo* ginfo, const OP_Inputs*, void*) {
    ginfo->cookEveryFrameIfAsked = true;
}

// ── Stubs — implemented in later tasks ────────────────────────────────────────

void FluxRTTop::execute(TOP_Output* output, const OP_Inputs* inputs, void*) {
    ++execCount_;

    // ── 1. Deferred Load / Unload (processed on render thread to avoid races) ─
    if (unloadRequested_.exchange(false)) {
        doUnload();
    }
    if (loadRequested_.exchange(false)) {
        doLoad(inputs);
    }

    // ── 2. Sync params ───────────────────────────────────────────────────────
    if (running_) {
        syncParams(inputs);
    }

    // ── 3. Check if process died ─────────────────────────────────────────────
    if (running_ && !launcher_.isRunning()) {
        running_ = false;
        launcher_.forceStop();  // close handles/mappings left by crashed process
    }

    FluxRTCtrl* ctrl = running_ ? launcher_.ctrl() : nullptr;

    // ── 4. Input pipeline ────────────────────────────────────────────────────
    // We use a one-frame-delayed download to avoid a CPU stall:
    //   Frame N: issue downloadTexture() → result will be ready by Frame N+1
    //   Frame N+1: read last frame's result, write BGR into shared-mem input
    const OP_TOPInput* topInput = inputs->getInputTOP(0);
    if (topInput && running_ && ctrl) {
        // Consume the *previous* download result (ready by now),
        // but only if Python has already cleared the previous frame (input_ready==0).
        // Skipping when input_ready==1 prevents torn frames under slow inference.
        if (prevDownRes_ && ctrl->input_ready == 0) {
            void* rawData = prevDownRes_->getData();
            // Validate that the downloaded resolution matches our configured frame size
            // to prevent out-of-bounds reads if the input TOP has a different resolution.
            const bool dimOk = (prevDownRes_->textureDesc.width  == (uint32_t)width_ &&
                                 prevDownRes_->textureDesc.height == (uint32_t)height_);
            lastInputW_ = (int)prevDownRes_->textureDesc.width;
            lastInputH_ = (int)prevDownRes_->textureDesc.height;
            lastInputAccepted_ = (rawData && dimOk);
            if (lastInputAccepted_) {
                bgraToShm(static_cast<const uint8_t*>(rawData),
                          launcher_.input(), width_, height_);
                ctrl->input_ready = 1;
            }
        }

        // Issue a new download; result will be consumed next frame.
        // verticalFlip=true gives top-down row order (row 0 = top) which
        // matches what OpenCV-based models expect.
        OP_TOPInputDownloadOptions opts;
        opts.pixelFormat  = OP_PixelFormat::BGRA8Fixed;
        opts.verticalFlip = true;
        prevDownRes_ = topInput->downloadTexture(opts, nullptr);
    }

    // ── 4b. Reference TOP input (input 1) ───────────────────────────────────
    // If a TOP is wired to input 1, it overrides the "Reference Image" file param.
    // Uses the same 1-frame-delayed download pattern as the main input.
    const OP_TOPInput* refTop = inputs->getInputTOP(1);
    if (running_ && ctrl) {
        if (refTop && ctrl->use_reference) {
            // Consume previous reference download
            if (prevRefDownRes_) {
                void* rawData = prevRefDownRes_->getData();
                if (rawData) {
                    int rw = (int)prevRefDownRes_->textureDesc.width;
                    int rh = (int)prevRefDownRes_->textureDesc.height;
                    if (rw > 0 && rh > 0) {
                        char tmpPath[MAX_PATH];
                        sprintf_s(tmpPath, "%s\\fluxrt_ref_%d.bmp",
                                  workDir_.c_str(), refGeneration_);
                        if (saveBgr24Bmp(
                                static_cast<const uint8_t*>(rawData),
                                rw, rh, tmpPath)) {
                            if (!prevRefBmpPath_.empty())
                                DeleteFileA(prevRefBmpPath_.c_str());
                            prevRefBmpPath_ = tempRefBmpPath_;
                            tempRefBmpPath_ = tmpPath;
                            ctrl_set_str(ctrl->reference_image_path, tmpPath);
                            ++refGeneration_;
                        }
                    }
                }
                prevRefDownRes_ = OP_SmartRef<OP_TOPDownloadResult>();
            }

            // Issue new download when reference TOP updates
            if (refTop->totalCooks != lastRefTotalCooks_) {
                lastRefTotalCooks_ = refTop->totalCooks;
                OP_TOPInputDownloadOptions refOpts;
                refOpts.pixelFormat  = OP_PixelFormat::BGRA8Fixed;
                refOpts.verticalFlip = false;  // bottom-up is fine for BMP
                prevRefDownRes_ = refTop->downloadTexture(refOpts, nullptr);
            }
        } else if (!refTop) {
            // TOP disconnected — reset state so file path param takes over
            if (lastRefTotalCooks_ != -1) {
                lastRefTotalCooks_  = -1;
                prevRefDownRes_     = OP_SmartRef<OP_TOPDownloadResult>();
                // Re-sync the file path param on next syncParams call
                lastRefPath_.clear();
            }
        }
    }

    // ── 5. Output pipeline ───────────────────────────────────────────────────
    if (running_ && ctrl && ctrl->output_ready) {
        shmToBgra(launcher_.output(), outputBGRA_.data(), width_, height_);
        ctrl->output_ready = 0;
    }

    // ── 6. Upload outputBGRA_ to TD texture (black until first frame arrives) ─
    const uint64_t byteSize = (uint64_t)width_ * height_ * 4;

    TOP_UploadInfo info;
    info.textureDesc.width       = (uint32_t)width_;
    info.textureDesc.height      = (uint32_t)height_;
    info.textureDesc.texDim      = OP_TexDim::e2D;
    info.textureDesc.pixelFormat = OP_PixelFormat::BGRA8Fixed;
    info.firstPixel              = TOP_FirstPixel::TopLeft;  // model outputs top-down
    info.colorBufferIndex        = 0;

    OP_SmartRef<TOP_Buffer> buf =
        myContext_->createOutputBuffer(byteSize, TOP_BufferFlags::None, nullptr);
    if (buf) {
        memcpy(buf->data, outputBGRA_.data(), byteSize);
        output->uploadBuffer(&buf, info, nullptr);
    }
}

bool FluxRTTop::getInfoDATSize(OP_InfoDATSize* infoSize, void*) {
    infoSize->rows     = 7;
    infoSize->cols     = 2;
    infoSize->byColumn = false;
    return true;
}

void FluxRTTop::getInfoDATEntries(int32_t index, int32_t,
                                   OP_InfoDATEntries* entries, void*) {
    char buf[256];
    switch (index) {
    case 0:
        entries->values[0]->setString("status");
        entries->values[1]->setString(statusString().c_str());
        break;
    case 1:
        sprintf_s(buf, "%d x %d", width_, height_);
        entries->values[0]->setString("resolution");
        entries->values[1]->setString(buf);
        break;
    case 2:
        entries->values[0]->setString("shm_input");
        entries->values[1]->setString(
            launcher_.isRunning() ? launcher_.inputShmName().c_str() : "");
        break;
    case 3:
        sprintf_s(buf, "%d", execCount_);
        entries->values[0]->setString("exec_count");
        entries->values[1]->setString(buf);
        break;
    case 4:
        entries->values[0]->setString("shm_output");
        entries->values[1]->setString(
            launcher_.isRunning() ? launcher_.outputShmName().c_str() : "");
        break;
    case 5:
        if (lastInputW_ > 0)
            sprintf_s(buf, "%d x %d", lastInputW_, lastInputH_);
        else
            buf[0] = '\0';
        entries->values[0]->setString("input_size");
        entries->values[1]->setString(buf);
        break;
    case 6:
        entries->values[0]->setString("input_accepted");
        entries->values[1]->setString(lastInputAccepted_ ? "yes" : "no");
        break;
    }
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
    {
        OP_NumericParameter np("Liptransfer");
        np.label            = "Lip Transfer";
        np.page             = "Inference";
        np.defaultValues[0] = 0.0;
        manager->appendToggle(np);
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
        unloadRequested_ = true;
    }
}

void FluxRTTop::doLoad(const OP_Inputs* inputs) {
    if (launcher_.isRunning()) doUnload();

    const char* pyExe      = inputs->getParString("Pythonexe");
    const char* cfgPath    = inputs->getParString("Configpath");
    const char* resStr     = inputs->getParString("Resolution");
    const char* workDirPar = inputs->getParString("Workdir");
    int8_ = inputs->getParInt("Int8mode") != 0;

    // Parse resolution "WxH"
    width_ = 576; height_ = 320;
    if (resStr) {
        int w = 0, h = 0;
        if (sscanf_s(resStr, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            width_ = w; height_ = h;
        }
    }
    inputBGRA_.resize((size_t)width_ * height_ * 4);
    outputBGRA_.resize((size_t)width_ * height_ * 4, 0);

    // Server script lives next to the DLL
    char dllPath[MAX_PATH] = {};
    HMODULE hMod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&FillTOPPluginInfo), &hMod);
    GetModuleFileNameA(hMod, dllPath, MAX_PATH);
    std::string dllDir = dllPath;
    dllDir = dllDir.substr(0, dllDir.find_last_of("\\/"));
    std::string serverScript = dllDir + "\\fluxrt_server.py";

    workDir_ = (workDirPar && workDirPar[0]) ? workDirPar : dllDir;
    std::string workDir = workDir_;

    LaunchConfig cfg;
    cfg.pythonExe    = pyExe    ? pyExe    : "";
    cfg.serverScript = serverScript;
    cfg.workDir      = workDir;
    cfg.configPath   = cfgPath  ? cfgPath  : "configs/config_with_reference.json";
    cfg.width        = width_;
    cfg.height       = height_;
    cfg.int8         = int8_;

    // 10-minute timeout for first compile
    running_ = launcher_.launch(cfg, 600000);
    if (running_) syncParams(inputs);
}

void FluxRTTop::doUnload() {
    running_ = false;
    launcher_.stop();
    prevDownRes_    = TD::OP_SmartRef<TD::OP_TOPDownloadResult>();
    prevRefDownRes_ = TD::OP_SmartRef<TD::OP_TOPDownloadResult>();
    lastRefTotalCooks_ = -1;
    if (!tempRefBmpPath_.empty()) {
        DeleteFileA(tempRefBmpPath_.c_str());
        tempRefBmpPath_.clear();
    }
    if (!prevRefBmpPath_.empty()) {
        DeleteFileA(prevRefBmpPath_.c_str());
        prevRefBmpPath_.clear();
    }
}

void FluxRTTop::syncParams(const OP_Inputs* inputs) {
    if (!launcher_.isRunning()) return;
    FluxRTCtrl* ctrl = launcher_.ctrl();
    if (!ctrl) return;

    const char* prompt = inputs->getParString("Prompt");
    if (prompt && std::string(prompt) != lastPrompt_) {
        ctrl_set_str(ctrl->prompt, prompt);
        lastPrompt_ = prompt;
    }

    int steps = inputs->getParInt("Steps");
    if (steps != lastSteps_) {
        ctrl->steps = steps;
        lastSteps_  = steps;
    }

    int seed = inputs->getParInt("Seed");
    if (seed != lastSeed_) {
        ctrl->seed = seed;
        lastSeed_  = seed;
    }

    float dynArea = static_cast<float>(inputs->getParDouble("Dynamicarea"));
    if (dynArea != lastDynArea_) {
        ctrl->dynamic_area = dynArea;
        lastDynArea_ = dynArea;
    }

    bool useRef = inputs->getParInt("Usereference") != 0;
    if (useRef != lastUseRef_) {
        ctrl->use_reference = useRef ? 1 : 0;
        lastUseRef_ = useRef;
    }

    bool lipTransfer = inputs->getParInt("Liptransfer") != 0;
    if (lipTransfer != lastLipTransfer_) {
        ctrl->lip_transfer_enable = lipTransfer ? 1 : 0;
        lastLipTransfer_ = lipTransfer;
    }

    // Only use file param path when no reference TOP is wired to input 1
    if (lastRefTotalCooks_ == -1) {
        const char* refPath = inputs->getParString("Referenceimage");
        if (refPath && std::string(refPath) != lastRefPath_) {
            ctrl_set_str(ctrl->reference_image_path, refPath);
            lastRefPath_ = refPath;
        }
    }
}

std::string FluxRTTop::statusString() const {
    if (!launcher_.isRunning()) return "Unloaded";
    const FluxRTCtrl* ctrl = launcher_.ctrl();
    if (!ctrl) return "Error: no ctrl";
    switch (ctrl->status) {
        case FLUXRT_STATUS_LOADING:   return "Loading...";
        case FLUXRT_STATUS_COMPILING: return "Compiling (first run ~3 min)...";
        case FLUXRT_STATUS_RUNNING:   return "Running";
        case FLUXRT_STATUS_ERROR: {
            size_t len = strnlen_s(ctrl->error_msg, sizeof(ctrl->error_msg));
            return std::string("Error: ") + std::string(ctrl->error_msg, len);
        }
        default:                      return "Unknown";
    }
}

/*static*/ bool FluxRTTop::saveBgr24Bmp(const uint8_t* bgra, int w, int h,
                                         const std::string& path) {
    // BMP rows must be padded to a multiple of 4 bytes
    const int rowPitch = ((w * 3 + 3) / 4) * 4;
    const int dataSize = rowPitch * h;
    const int fileSize = 54 + dataSize;

    uint8_t hdr[54] = {};
    auto w32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
    auto w16 = [&](int off, uint16_t v) { memcpy(hdr + off, &v, 2); };

    // BITMAPFILEHEADER
    hdr[0] = 'B'; hdr[1] = 'M';
    w32(2,  (uint32_t)fileSize);
    w32(10, 54);
    // BITMAPINFOHEADER
    w32(14, 40);
    w32(18, (uint32_t)w);
    w32(22, (uint32_t)h);   // positive = bottom-up (matches verticalFlip=false download)
    w16(26, 1);
    w16(28, 24);
    w32(34, (uint32_t)dataSize);

    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;

    fwrite(hdr, 1, 54, f);

    std::vector<uint8_t> row((size_t)rowPitch, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = bgra + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0]; // B
            row[x * 3 + 1] = src[x * 4 + 1]; // G
            row[x * 3 + 2] = src[x * 4 + 2]; // R
        }
        fwrite(row.data(), 1, (size_t)rowPitch, f);
    }
    fclose(f);
    return true;
}

/*static*/ void FluxRTTop::bgraToShm(const uint8_t* bgra, uint8_t* bgr, int w, int h) {
    const size_t pixels = (size_t)w * h;
    for (size_t i = 0; i < pixels; ++i) {
        bgr[i * 3 + 0] = bgra[i * 4 + 0]; // B
        bgr[i * 3 + 1] = bgra[i * 4 + 1]; // G
        bgr[i * 3 + 2] = bgra[i * 4 + 2]; // R
    }
}

/*static*/ void FluxRTTop::shmToBgra(const uint8_t* bgr, uint8_t* bgra, int w, int h) {
    const size_t pixels = (size_t)w * h;
    for (size_t i = 0; i < pixels; ++i) {
        bgra[i * 4 + 0] = bgr[i * 3 + 0]; // B
        bgra[i * 4 + 1] = bgr[i * 3 + 1]; // G
        bgra[i * 4 + 2] = bgr[i * 3 + 2]; // R
        bgra[i * 4 + 3] = 255;             // A
    }
}
