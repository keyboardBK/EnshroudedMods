#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include "ledger.h"
namespace serverboots {
inline std::string read_file(const std::filesystem::path& path) {
    if(!std::filesystem::exists(path))return {};
    if(std::filesystem::file_size(path)>64u*1024u*1024u)throw std::runtime_error("Ledger too large");
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read ledger");
    std::string bytes((std::istreambuf_iterator<char>(in)),{});
    if(in.bad())throw std::runtime_error("Ledger read failed");
    return bytes;
}
inline void atomic_save(const std::filesystem::path& path,const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary=std::filesystem::path(path.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId()));
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create ledger temporary file");
    DWORD written=0;
    const bool ok=bytes.size()<=MAXDWORD && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok) {DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot flush ledger");}
    if(std::filesystem::exists(path)) {
        const auto backup=std::filesystem::path(path.wstring()+L".bak");
        if(!CopyFileW(path.c_str(),backup.c_str(),FALSE)) {
            DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot back up ledger");
        }
    }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot commit ledger");
    }
}
}
