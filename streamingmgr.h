#pragma once

#include <Windows.h>
#include <xaudio2.h>

#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <future>
#include <format>
#include <functional>

// ŠO•”ŒöŠJI/F



extern bool Initialize_STMGR(std::function<void(std::string)> logfunc);
extern void Release_STMGR();
extern int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int)> callbackf);

