#pragma once

#define NOMINMAX
#include <Windows.h>
#include <xaudio2.h>

#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <future>
#include <functional>

#define FMT_HEADER_ONLY
#include "fmt/core.h"

//#include "quill/Backend.h"
//#include "quill/Frontend.h"
//#include "quill/LogMacros.h"
//#include "quill/Logger.h"
//#include "quill/sinks/ConsoleSink.h"
#include <string_view>

// 外部公開I/F

//extern quill::Logger* logger;


extern bool Initialize_STMGR();
extern void Release_STMGR();
extern int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int)> callbackf);
extern bool PlayChannel_STMGR(int ch);
extern void StopChannel_STMGR(int ch);


