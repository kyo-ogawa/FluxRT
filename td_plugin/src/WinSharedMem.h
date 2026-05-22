#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>
#include <string>

/// RAII wrapper for attaching to an existing Windows named shared memory block.
/// The block must already be created (by Python) before open() is called.
class WinSharedMem {
public:
    WinSharedMem() = default;
    ~WinSharedMem() { close(); }

    WinSharedMem(const WinSharedMem&) = delete;
    WinSharedMem& operator=(const WinSharedMem&) = delete;

    void open(const std::string& name, size_t size) {
        hMap_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!hMap_)
            throw std::runtime_error("OpenFileMapping failed for: " + name +
                                     " (error " + std::to_string(GetLastError()) + ")");
        pData_ = MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!pData_) {
            CloseHandle(hMap_); hMap_ = nullptr;
            throw std::runtime_error("MapViewOfFile failed for: " + name);
        }
        size_  = size;
        name_  = name;
    }

    void close() {
        if (pData_) { UnmapViewOfFile(pData_); pData_ = nullptr; }
        if (hMap_)  { CloseHandle(hMap_);      hMap_  = nullptr; }
    }

    bool   isOpen()       const { return pData_ != nullptr; }
    void*  data()         const { return pData_; }
    size_t size()         const { return size_;  }
    const std::string& name() const { return name_; }

private:
    HANDLE      hMap_  = nullptr;
    void*       pData_ = nullptr;
    size_t      size_  = 0;
    std::string name_;
};
