#pragma once
#include <cstdint>
#include <cstring>

// Status codes — must match fluxrt_server.py
enum FluxRTStatus : uint8_t {
    FLUXRT_STATUS_LOADING   = 0,
    FLUXRT_STATUS_COMPILING = 1,
    FLUXRT_STATUS_RUNNING   = 2,
    FLUXRT_STATUS_ERROR     = 3,
};

#pragma pack(push, 1)
struct FluxRTCtrl {
    char    prompt[1024];               // bytes 0–1023
    char    reference_image_path[512];  // bytes 1024–1535
    int32_t steps;                      // bytes 1536–1539
    int32_t seed;                       // bytes 1540–1543
    float   dynamic_area;               // bytes 1544–1547
    uint8_t use_reference;              // byte  1548
    uint8_t shutdown_flag;              // byte  1549  C++→Python
    uint8_t input_ready;                // byte  1550  C++→Python
    uint8_t output_ready;               // byte  1551  Python→C++
    uint8_t status;                     // byte  1552  Python→C++
    char    error_msg[256];             // bytes 1553–1808
    uint8_t lip_transfer_enable;        // byte  1809  C++→Python
    uint8_t _pad[2286];                 // bytes 1810–4095
};
#pragma pack(pop)
static_assert(sizeof(FluxRTCtrl) == 4096, "FluxRTCtrl must be exactly 4096 bytes");

// Helper: write a null-terminated string into a fixed char array.
template<size_t N>
inline void ctrl_set_str(char (&dst)[N], const char* src) {
    strncpy_s(dst, N, src ? src : "", _TRUNCATE);
}
