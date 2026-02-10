#include "mex.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct MexError : public std::exception {
    const char* id;
    std::string message;

    MexError(const char* errId, std::string msg) : id(errId), message(std::move(msg)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

[[noreturn]] void ThrowMex(const char* id, const std::string& message) {
    throw MexError(id, message);
}

std::string ToNarrow(const std::wstring& w) {
    if (w.empty()) {
        return std::string();
    }
    int len = WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return "<utf16-path>";
    }
    std::string out(static_cast<size_t>(len), '\0');
    const int rc = WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], len, nullptr, nullptr);
    if (rc <= 0) {
        return "<utf16-path>";
    }
    return out;
}

std::string Win32CodeToString(DWORD code) {
    std::ostringstream oss;
    oss << "Win32 error " << static_cast<unsigned long>(code);
    return oss.str();
}

std::wstring MatlabCharToWString(const mxArray* arr) {
    if (!mxIsChar(arr)) {
        ThrowMex("baa_save:type", "filename must be a MATLAB char vector");
    }
    if (mxGetNumberOfDimensions(arr) != 2 || (mxGetM(arr) != 1 && mxGetN(arr) != 1)) {
        ThrowMex("baa_save:type", "filename must be a MATLAB char vector");
    }

    const mwSize nChars = mxGetNumberOfElements(arr);
    const mxChar* chars = mxGetChars(arr);
    if (chars == nullptr && nChars != 0) {
        ThrowMex("baa_save:io", "failed to read filename buffer");
    }

    std::wstring path;
    path.resize(static_cast<size_t>(nChars));
    for (mwSize i = 0; i < nChars; ++i) {
        path[static_cast<size_t>(i)] = static_cast<wchar_t>(chars[i]);
    }
    return path;
}

bool IsSlash(wchar_t c) {
    return c == L'\\' || c == L'/';
}

bool IsDriveRoot(const std::wstring& path) {
    if (path.size() == 2 && path[1] == L':') {
        return true;
    }
    if (path.size() == 3 && path[1] == L':' && IsSlash(path[2])) {
        return true;
    }
    return false;
}

bool PathExistsAsDirectory(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void EnsureDirectoryRecursive(const std::wstring& dirPath) {
    if (dirPath.empty()) {
        return;
    }
    if (IsDriveRoot(dirPath)) {
        return;
    }
    if (PathExistsAsDirectory(dirPath)) {
        return;
    }

    size_t split = dirPath.find_last_of(L"\\/");
    if (split != std::wstring::npos) {
        std::wstring parent = dirPath.substr(0, split);
        if (!parent.empty()) {
            EnsureDirectoryRecursive(parent);
        }
    }

    if (CreateDirectoryW(dirPath.c_str(), nullptr)) {
        return;
    }

    const DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS && PathExistsAsDirectory(dirPath)) {
        return;
    }

    std::ostringstream oss;
    oss << "failed to create directory '" << ToNarrow(dirPath) << "': " << Win32CodeToString(err);
    ThrowMex("baa_save:io", oss.str());
}

struct TypeInfo {
    const char* descr;
    size_t itemSize;
};

TypeInfo MapType(const mxArray* a) {
    if (mxIsComplex(a)) {
        ThrowMex("baa_save:type", "complex arrays are not supported");
    }
    if (mxIsSparse(a)) {
        ThrowMex("baa_save:type", "sparse arrays are not supported");
    }
    if (!mxIsNumeric(a) && !mxIsLogical(a)) {
        ThrowMex("baa_save:type", "input must be numeric or logical");
    }

    switch (mxGetClassID(a)) {
        case mxDOUBLE_CLASS:
            return {"<f8", 8};
        case mxSINGLE_CLASS:
            return {"<f4", 4};
        case mxINT8_CLASS:
            return {"|i1", 1};
        case mxUINT8_CLASS:
            return {"|u1", 1};
        case mxINT16_CLASS:
            return {"<i2", 2};
        case mxUINT16_CLASS:
            return {"<u2", 2};
        case mxINT32_CLASS:
            return {"<i4", 4};
        case mxUINT32_CLASS:
            return {"<u4", 4};
        case mxINT64_CLASS:
            return {"<i8", 8};
        case mxUINT64_CLASS:
            return {"<u8", 8};
        case mxLOGICAL_CLASS:
            return {"|b1", 1};
        default:
            ThrowMex("baa_save:type", "unsupported MATLAB class");
            return {"", 0};
    }
}

std::string BuildShapeTuple(const mxArray* a) {
    const mwSize nd = mxGetNumberOfDimensions(a);
    const mwSize* dims = mxGetDimensions(a);

    std::ostringstream oss;
    oss << "(";
    for (mwSize i = 0; i < nd; ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << static_cast<unsigned long long>(dims[i]);
    }
    if (nd == 1) {
        oss << ",";
    }
    oss << ")";
    return oss.str();
}

std::string BuildAlignedHeader(const std::string& dict, size_t prefixLen) {
    std::string header = dict;
    header.push_back('\n');

    const size_t rem = (prefixLen + header.size()) % 16;
    const size_t pad = (16 - rem) % 16;
    if (pad > 0) {
        header.insert(header.end() - 1, pad, ' ');
    }
    return header;
}

struct HeaderBlock {
    uint8_t major;
    uint8_t minor;
    std::string header;
};

HeaderBlock BuildNpyHeader(const std::string& dict) {
    std::string h1 = BuildAlignedHeader(dict, 10);
    if (h1.size() <= std::numeric_limits<uint16_t>::max()) {
        return {1, 0, std::move(h1)};
    }

    std::string h2 = BuildAlignedHeader(dict, 12);
    if (h2.size() > std::numeric_limits<uint32_t>::max()) {
        ThrowMex("baa_save:io", "header is too large for NPY v2.0");
    }
    return {2, 0, std::move(h2)};
}

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE h = INVALID_HANDLE_VALUE) : handle_(h) {}
    ~ScopedHandle() { Close(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    HANDLE Get() const { return handle_; }

private:
    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE handle_;
};

void WriteAll(HANDLE file, const void* data, uint64_t nBytes) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    constexpr DWORD kChunk = 64u * 1024u * 1024u;

    uint64_t remaining = nBytes;
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<uint64_t>(remaining, static_cast<uint64_t>(kChunk)));
        DWORD written = 0;
        const BOOL ok = WriteFile(file, ptr, chunk, &written, nullptr);
        if (!ok || written != chunk) {
            const DWORD err = ok ? ERROR_WRITE_FAULT : GetLastError();
            std::ostringstream oss;
            oss << "failed while writing file: " << Win32CodeToString(err);
            ThrowMex("baa_save:io", oss.str());
        }
        ptr += written;
        remaining -= written;
    }
}

void SaveAsNpy(const mxArray* a, const std::wstring& filename) {
    const TypeInfo type = MapType(a);
    const std::string shape = BuildShapeTuple(a);
    const std::string dict =
        "{'descr': '" + std::string(type.descr) + "', 'fortran_order': True, 'shape': " + shape +
        ", }";
    const HeaderBlock header = BuildNpyHeader(dict);

    size_t split = filename.find_last_of(L"\\/");
    if (split != std::wstring::npos) {
        const std::wstring parent = filename.substr(0, split);
        if (!parent.empty()) {
            EnsureDirectoryRecursive(parent);
        }
    }

    ScopedHandle file(CreateFileW(
        filename.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr));

    if (file.Get() == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        std::ostringstream oss;
        oss << "failed to open output file '" << ToNarrow(filename) << "': "
            << Win32CodeToString(err);
        ThrowMex("baa_save:io", oss.str());
    }

    const uint8_t magic[6] = {0x93, 'N', 'U', 'M', 'P', 'Y'};
    WriteAll(file.Get(), magic, sizeof(magic));

    const uint8_t version[2] = {header.major, header.minor};
    WriteAll(file.Get(), version, sizeof(version));

    if (header.major == 1) {
        const uint16_t hlen = static_cast<uint16_t>(header.header.size());
        const uint8_t lenBytes[2] = {static_cast<uint8_t>(hlen & 0xFFu),
                                     static_cast<uint8_t>((hlen >> 8) & 0xFFu)};
        WriteAll(file.Get(), lenBytes, sizeof(lenBytes));
    } else {
        const uint32_t hlen = static_cast<uint32_t>(header.header.size());
        const uint8_t lenBytes[4] = {static_cast<uint8_t>(hlen & 0xFFu),
                                     static_cast<uint8_t>((hlen >> 8) & 0xFFu),
                                     static_cast<uint8_t>((hlen >> 16) & 0xFFu),
                                     static_cast<uint8_t>((hlen >> 24) & 0xFFu)};
        WriteAll(file.Get(), lenBytes, sizeof(lenBytes));
    }

    WriteAll(file.Get(), header.header.data(), static_cast<uint64_t>(header.header.size()));

    const uint64_t nElem = static_cast<uint64_t>(mxGetNumberOfElements(a));
    if (type.itemSize != 0 && nElem > std::numeric_limits<uint64_t>::max() / type.itemSize) {
        ThrowMex("baa_save:io", "payload size overflow");
    }
    const uint64_t payloadBytes = nElem * static_cast<uint64_t>(type.itemSize);
    if (payloadBytes > 0) {
        const void* payload = mxGetData(a);
        if (payload == nullptr) {
            ThrowMex("baa_save:io", "input data pointer is null");
        }
        WriteAll(file.Get(), payload, payloadBytes);
    }
}

}  // namespace

void mexFunction(int nlhs, mxArray*[], int nrhs, const mxArray* prhs[]) {
    try {
        if (nlhs != 0) {
            ThrowMex("baa_save:usage", "baa_save does not return outputs");
        }
        if (nrhs != 2) {
            ThrowMex("baa_save:usage", "usage: baa_save(a, filename)");
        }

        const mxArray* a = prhs[0];
        const mxArray* fname = prhs[1];

        const std::wstring filename = MatlabCharToWString(fname);
        SaveAsNpy(a, filename);
    } catch (const MexError& e) {
        mexErrMsgIdAndTxt(e.id, "%s", e.what());
    } catch (const std::exception& e) {
        mexErrMsgIdAndTxt("baa_save:io", "%s", e.what());
    } catch (...) {
        mexErrMsgIdAndTxt("baa_save:io", "unknown error");
    }
}
