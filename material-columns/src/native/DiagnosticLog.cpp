// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "DiagnosticLog.hpp"
#include <nn/fs.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#ifndef MATERIAL_COLUMNS_LOG_HOST_TEST
#include <lib/log/svc_logger.hpp>
#endif
namespace material_columns::native {
namespace {
constexpr std::size_t queueCapacity=8192, chunkCapacity=2048, fileLimit=512*1024;
std::array<char,queueCapacity> pending{};
std::size_t queued{}, written{};
std::atomic_flag queueLock=ATOMIC_FLAG_INIT, writerLock=ATOMIC_FLAG_INIT;
std::atomic<unsigned> dropped{};
std::atomic<bool> disabled{};
bool ready{};
unsigned ticks{};
constexpr const char* path="mclog:/material-columns/profile.log";
constexpr const char* previous="mclog:/material-columns/profile.previous.log";
void status(const char* operation, unsigned result) {
    char text[160];
    std::snprintf(text,sizeof(text),"[material-columns] SD profile %s result=%08x; debug output remains enabled",operation,result);
#ifndef MATERIAL_COLUMNS_LOG_HOST_TEST
    exl::log::SvcLogger{}.LogRaw(text);
#else
    (void)text;
#endif
}
template<class Result> bool check(const char* operation, Result result) {
    if (!result.IsFailure()) return true;
    disabled.store(true,std::memory_order_relaxed);
    status(operation,result.GetInnerValueForDebug());
    return false;
}
bool initialize() {
    if (ready) return true;
    if (!check("mount",nn::fs::MountSdCard("mclog"))) return false;
    auto result=nn::fs::CreateDirectory("mclog:/material-columns");
    if (result.IsFailure() && result.GetInnerValueForDebug()!=0x402 && !check("directory",result)) return false;
    nn::fs::FileHandle old;
    result=nn::fs::OpenFile(&old,path,nn::fs::OpenMode_Read);
    if (!result.IsFailure()) {
        nn::fs::CloseFile(old);
        result=nn::fs::DeleteFile(previous);
        if (result.IsFailure() && result.GetInnerValueForDebug()!=0x202 && !check("previous",result)) return false;
        if (!check("rotate",nn::fs::RenameFile(path,previous))) return false;
    } else if (result.GetInnerValueForDebug()!=0x202 && !check("existing",result)) return false;
    if (!check("create",nn::fs::CreateFile(path,0))) return false;
    ready=true;
    return true;
}
void drain() {
    char chunk[chunkCapacity];
    if (queueLock.test_and_set(std::memory_order_acquire)) return;
    auto count=std::min(queued,sizeof(chunk));
    while (count && pending[count-1]!='\n') --count;
    std::memcpy(chunk,pending.data(),count);
    queued-=count;
    std::memmove(pending.data(),pending.data()+count,queued);
    queueLock.clear(std::memory_order_release);
    const auto lost=dropped.exchange(0,std::memory_order_relaxed);
    if (!count && !lost) return;
    if (!initialize()) return;
    char header[224]{};
    int headerSize=0;
    if (!written) headerSize=std::snprintf(header,sizeof(header),
        "Material Columns pool-lifecycle-01; queue=8192B drain-stack=2048B file-cap=524288B; UI-detail is nested; parent/pool/cache costs unisolated\n");
    if (lost) headerSize+=std::snprintf(header+headerSize,sizeof(header)-headerSize,"\n[SD profile] dropped-lines=%u\n",lost);
    const auto total=std::size_t(headerSize)+count;
    if (written+total>fileLimit) { disabled.store(true);status("file cap reached",0);return; }
    nn::fs::FileHandle file;
    if (!check("open",nn::fs::OpenFile(&file,path,nn::fs::OpenMode_Write))) return;
    bool success=check("resize",nn::fs::SetFileSize(file,written+total));
    const nn::fs::WriteOption noFlush{0}, flush{nn::fs::WriteOptionFlag_Flush};
    if (success && headerSize) success=check("header",nn::fs::WriteFile(file,written,header,headerSize,noFlush));
    if (success) success=check("write",nn::fs::WriteFile(file,written+headerSize,chunk,count,flush));
    nn::fs::CloseFile(file);
    if (success) written+=total;
}
}
void queueDiagnostic(std::string_view text) {
    if (disabled.load(std::memory_order_relaxed)) return;
    if (queueLock.test_and_set(std::memory_order_acquire)) { ++dropped;return; }
    if (text.size()<chunkCapacity && text.size()+1<=pending.size()-queued) {
        std::memcpy(pending.data()+queued,text.data(),text.size());queued+=text.size();
        pending[queued++]='\n';
    } else ++dropped;
    queueLock.clear(std::memory_order_release);
}
void pumpDiagnostics(bool force) {
    // Only the UI update/close boundary performs I/O; render/load hooks just enqueue.
    if (disabled.load(std::memory_order_relaxed) || writerLock.test_and_set(std::memory_order_acquire)) return;
    bool full=false;
    if (!queueLock.test_and_set(std::memory_order_acquire)) {
        full=queued>=chunkCapacity;
        queueLock.clear(std::memory_order_release);
    }
    if (force || full || ++ticks>=60) {
        ticks=0;
        for(unsigned i=0;i<(force?8u:1u) && !disabled.load();++i) drain();
    }
    writerLock.clear(std::memory_order_release);
}
}
