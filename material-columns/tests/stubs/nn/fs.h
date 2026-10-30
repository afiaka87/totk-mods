#pragma once
#include <cstdint>
using s64=std::int64_t;
using u64=std::uint64_t;
namespace nn::fs {
struct Result { bool good; unsigned code=0x202; unsigned GetInnerValueForDebug() const { return good?0:code; } bool IsSuccess() const { return good; } bool IsFailure() const { return !good; } };
struct FileHandle { unsigned index; };
inline constexpr int OpenMode_Read=1, OpenMode_Write=2, WriteOptionFlag_Flush=1;
struct WriteOption { int flags; };
Result MountSdCard(const char*);
Result CreateDirectory(const char*);
Result DeleteFile(const char*);
Result RenameFile(const char*,const char*);
Result CreateFile(const char*,s64);
Result SetFileSize(FileHandle,s64);
Result WriteFile(FileHandle,s64,const void*,u64,const WriteOption&);
Result OpenFile(FileHandle*,const char*,int);
Result GetFileSize(s64*,FileHandle);
Result ReadFile(FileHandle,std::int64_t,void*,std::uint64_t);
void CloseFile(FileHandle);
}
