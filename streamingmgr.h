#pragma once

#include <Windows.h>
#include <xaudio2.h>

#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <future>
#include <functional>

#define FMT_HEADER_ONLY
#include "include/fmt/core.h"

// 外部公開I/F



extern bool Initialize_STMGR(std::function<void(std::string)> logfunc);
extern void Release_STMGR();
extern int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int, void *)> callbackf);
extern bool PlayChannel_STMGR(int ch);
extern void StopChannel_STMGR(int ch);


