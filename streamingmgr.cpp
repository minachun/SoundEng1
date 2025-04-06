#include "streamingmgr.h"

/*
 * XAudio2を使ったオーディオストリーミングマネージャー
 * 
 * 内部でスレッドを一つ立ち上げます、そのため、初期化と終了処理は対で呼び出してください。
 */


enum class SM_RECEIVE : int {
	OK,
	ERR,
};

// コマンド戻り値構造体
struct AS_RETVAL {
	SM_RECEIVE ret;
	int arg0;
	void* arg1;
	void* arg2;

	// コンストラクタ
	AS_RETVAL() : ret(SM_RECEIVE::OK), arg0(0), arg1(nullptr), arg2(nullptr) {}
	AS_RETVAL(SM_RECEIVE _r, int a0, void* a1, void* a2) : ret(_r), arg0(a0), arg1(a1), arg2(a2) {}
};


enum class SM_COMMAND : int {
	NONE,
	INIT,			// 初期化
	MAKE_CH,		// チャンネル作成
	RELEASE_CH,		// チャンネル破棄
	PLAY_CH,		// チャンネル再生
	STOP_CH,		// チャンネル停止
	REBOOT,			// 解放して再度初期化
	QUIT,			// 解放 arg0を1にするとスレッドまで終了する
};

// コマンド受け渡し構造体
struct AS_ARGS {
	SM_COMMAND command;
	int arg0;
	void* arg1;
	void* arg2;
	bool isRequiredReceive;

	AS_ARGS(SM_COMMAND _c, int _0, void* _1, void* _2, bool _isReq) : command(_c), arg0(_0), arg1(_1), arg2(_2), isRequiredReceive(_isReq){}
	AS_ARGS() : command(SM_COMMAND::NONE), arg0(0), arg1(nullptr), arg2(nullptr), isRequiredReceive(false) {}
	AS_ARGS& operator=(AS_ARGS& _r) {
		this->command = _r.command;
		this->arg0 = _r.arg0;
		this->arg1 = _r.arg1;
		this->arg2 = _r.arg2;
		this->isRequiredReceive = _r.isRequiredReceive;
		return *this;
	}
};

// 受け渡しコマンドコンテナークラスのI/F
class AS_ARGS_BASE {
protected:
	SM_COMMAND command;

public:
	AS_ARGS_BASE(SM_COMMAND _c) : command(_c) {}
	SM_COMMAND GetCommand() { return this->command; }
	virtual std::string ToString() = 0;
};

// 初期化コマンド
class AS_ARGS_INIT : public AS_ARGS_BASE {
private:
	// 特に必要な引数なし

public:
	AS_ARGS_INIT() : AS_ARGS_BASE(SM_COMMAND::INIT) {}
	std::string ToString() { return std::format("INIT command"); };
};

// 解放コマンド
class AS_ARGS_QUIT : public AS_ARGS_BASE {
private:
	bool isTerminated;

public:
	AS_ARGS_QUIT(bool _r) : isTerminated(_r), AS_ARGS_BASE(SM_COMMAND::QUIT) {}
	bool IsTerminated() { return this->isTerminated; }
	std::string ToString() { return std::format("QUIT command"); };
};

// チャンネル作成コマンド
class AS_ARGS_MAKECH : public AS_ARGS_BASE {
private:
	WAVEFORMATEX format;
	int buffersamples;
	std::function<void(void*, int)> makecallback;		// バッファへのポインタ, 要求するサンプル数

public:
	AS_ARGS_MAKECH(WAVEFORMATEX& _format, int _bufsamp, std::function<void(void*, int)>& _callback) : format(_format), buffersamples(_bufsamp), makecallback(_callback), AS_ARGS_BASE(SM_COMMAND::MAKE_CH) {}
	WAVEFORMATEX GetFormat() { return this->format; }
	int GetBufferSamples() { return this->buffersamples; }
	std::function<void(void*, int)> GetCallback() { return this->makecallback; }
	std::string ToString() { return std::format("MAKECH command"); };
};


class VoiceManager : public IXAudio2VoiceCallback
{
public:
	IXAudio2SourceVoice* sourceV;
	std::function<void(std::string)> logfunc;		// ログ関数

	// VoiceCallback の実装
	void OnStreamEnd()
	{
		//Called when the voice has just finished playing a contiguous audio stream.
	}

	//Unused methods are stubs
	void OnVoiceProcessingPassEnd()
	{
	}

	void OnVoiceProcessingPassStart(UINT32 SamplesRequired)
	{
	}

	void OnBufferEnd(void* pBufferContext)
	{
	}

	void OnBufferStart(void* pBufferContext)
	{
	}

	void OnLoopEnd(void* pBufferContext)
	{
	}

	void OnVoiceError(void* pBufferContext, HRESULT Error)
	{
	}

	VoiceManager(std::function<void(std::string)> _func) : sourceV(nullptr), logfunc(_func) {}
};


// 内部で使用する管理用ハンドル
class StreamManager : public IXAudio2EngineCallback {
public:
	HANDLE h_thread;					// スレッドハンドル
	std::mutex commt;
	std::queue<AS_ARGS_BASE *> comqueue;		// コマンドキュー
	std::queue<std::promise<AS_RETVAL>> retqueue;		// 応答キュー
	std::function<void(std::string)> logfunc;		// ログ関数
	void* log_instance;
	// XAudio2のI/F
	IXAudio2* xaudio;
	// XAudio2MasteringVoiceのI/F
	IXAudio2MasteringVoice* mvoice;
	// 各チャンネル
	std::deque<VoiceManager> voices;

	// コマンドを非同期で投げる（戻り値なし）
	void SetCommandAsync(AS_ARGS_BASE *command)
	{
		std::lock_guard<std::mutex> lock(this->commt);
		this->comqueue.push(std::move(command));
	}

	// コマンドを戻り値付きで投げる
	std::future<AS_RETVAL> SetCommandSync(AS_ARGS_BASE* command)
	{
		std::promise<AS_RETVAL> r;

		auto _r = r.get_future();

		{
			std::lock_guard<std::mutex> lock(this->commt);
			this->comqueue.push(std::move(command));
			this->retqueue.push(std::move(r));
		}

		return _r;
	}

	// コマンドを受信する
	bool GetCommand(AS_ARGS_BASE** command)
	{
		std::lock_guard<std::mutex> lock(this->commt);
		if (this->comqueue.empty()) {
			// 空なのでダミーを返す
			return false;
		}
		*command = this->comqueue.front();
		this->comqueue.pop();
		return true;
	}

	// 応答を返す
	void SetRetval(SM_RECEIVE recv, int arg0, void* arg1, void* arg2)
	{
		std::lock_guard<std::mutex> lock(this->commt);
		this->retqueue.front().set_value(AS_RETVAL(recv, arg0, arg1, arg2));
		this->retqueue.pop();
	}

	void OutputLog(std::string log)
	{
		if (this->logfunc != nullptr) {
			this->logfunc(log);
		}
	}

	StreamManager() : h_thread(nullptr), logfunc(nullptr), log_instance(nullptr), xaudio(nullptr), mvoice(nullptr)
	{
	}


	// EngineCallback の I/F の実装

	void OnProcessingPassEnd()
	{
		// オーディオ処理パスが終了した直後に呼び出される
	}

	void OnProcessingPassStart()
	{
		// オーディオ処理パスが開始される直前に呼び出される
	}

	void OnCriticalError(HRESULT Error)
	{
		// XAudio2を閉じて再起動する必要がある時に呼び出される

		// 抜粋：https://learn.microsoft.com/ja-jp/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-oncriticalerror
		// szDeviceId パラメーターで特定のデバイスの ID を IXAudio2::CreateMasteringVoice に指定するか、XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT フラグを使用すると、
		// 重大なエラーが発生し、基になる WASAPI レンダリング デバイスが使用できなくなった場合は OnCriticalError が発生します。 
		// これは、ヘッドセットやスピーカーが取り外された場合や、USB オーディオ デバイスが取り外された場合などに発生する可能性があります。

		
	}


};



// 管理用ハンドル
std::shared_ptr<StreamManager> mgr_handle = std::make_shared<StreamManager>();


// オーディオストリーム初期化処理
bool InitAudioStream()
{
	// まずは COMの初期化
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		// COM初期化に失敗
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : COM初期化に失敗 hr={0:08X}", hr));
		return false;
	}
	// XAudio2初期化
	hr = ::XAudio2Create(&(mgr_handle->xaudio), 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(hr)) {
		// XAudio2の初期化に失敗
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2初期化に失敗 hr={0:08X}", hr));
		return false;
	}
	// EngineCallbackの設定
	hr = mgr_handle->xaudio->RegisterForCallbacks(mgr_handle.get());
	if (FAILED(hr)) {
		// XAudio2のCallback設定に失敗
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2初期化に失敗 hr={0:08X}", hr));
		return false;
	}
	// XAudio2MasteringVoice初期化
	hr = mgr_handle->xaudio->CreateMasteringVoice(&(mgr_handle->mvoice), 2, XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL, AUDIO_STREAM_CATEGORY::AudioCategory_Other);
	if (FAILED(hr)) {
		// XAudio2MasteringVoiceの初期化に失敗
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2MasteringVoice初期化に失敗 hr={0:08X}", hr));
		return false;
	}
	return true;
}

// オーディオストリーム解放処理
bool ReleaseAudioStream()
{
	// XAudio2を解放（すると関連してすべてのオブジェクトも解放される模様）
	if (mgr_handle->xaudio != nullptr) {
		mgr_handle->xaudio->Release();
		mgr_handle->xaudio = nullptr;
		mgr_handle->mvoice = nullptr;
	}
	// COMの解放
	::CoUninitialize();
	return true;
}

// チャンネル確保処理
int MakeChannelAudioStream(const WAVEFORMATEX &format, int bufsamples, std::function<void(void*, int)> callback)
{
	HRESULT hr;
	IXAudio2SourceVoice* sourceV;
	VoiceManager vm(mgr_handle->logfunc);

	hr = mgr_handle->xaudio->CreateSourceVoice(&sourceV, &format, 0, 2.0F, &vm, NULL, NULL);
	if (FAILED(hr)) {
		// SourceVoice作成に失敗
		mgr_handle->OutputLog(std::format("[ERROR] sourcevoice can't created : SourceVoice作成に失敗 hr={0:08X}", hr));
		return -1;
	}

	// 作成出来たSourceVoiceを登録してindexを返す
	vm.sourceV = sourceV;
	mgr_handle->voices.push_back(std::move(vm));
	int idx = (int)(mgr_handle->voices.size() - 1);

	return idx;
}


// オーディオストリーム処理メインスレッド
DWORD WINAPI AudioStreamProcThread(void* _)
{
	mgr_handle->OutputLog(std::format("[DEBUG] bootup thread"));

	bool isLooped = true;
	AS_ARGS_INIT* c_init;
	AS_ARGS_QUIT* c_quit;
	AS_ARGS_MAKECH* c_makech;
	do {
		// コマンド取得
		AS_ARGS_BASE *args;
		bool _r = mgr_handle->GetCommand(&args);

		if ( _r == false) {
			// 取得出来なかった
			::Sleep(0);
			continue;
		}

		mgr_handle->OutputLog(std::format("[DEBUG] command proc {0}", args->ToString()));

		// コマンド別処理
		switch (args->GetCommand()) {
		case SM_COMMAND::INIT:
			// 初期化
			c_init = static_cast<AS_ARGS_INIT*>(args);
			isLooped = true;
			_r = InitAudioStream();
			mgr_handle->SetRetval(_r ? SM_RECEIVE::OK : SM_RECEIVE::ERR, 0, nullptr, nullptr);
			break;

		case SM_COMMAND::QUIT:
			// 終了
			c_quit = static_cast<AS_ARGS_QUIT*>(args);
			ReleaseAudioStream();
			if (c_quit->IsTerminated()) {
				// arg0 が 1 の時はスレッドも終了させる
				isLooped = false;
				mgr_handle->SetRetval(SM_RECEIVE::OK, 0, nullptr, nullptr);
			}
			break;

		case SM_COMMAND::REBOOT:
			// 重大なエラー発生により一旦終了して再度初期化
			ReleaseAudioStream();
			InitAudioStream();
			break;

		case SM_COMMAND::MAKE_CH:
			// チャンネル作成
			c_makech = static_cast<AS_ARGS_MAKECH*>(args);
			int ch = MakeChannelAudioStream(c_makech->GetFormat(), c_makech->GetBufferSamples(), c_makech->GetCallback());
			mgr_handle->SetRetval((ch >= 0 ) ? SM_RECEIVE::OK : SM_RECEIVE::ERR, ch, nullptr, nullptr);
			break;

		}


	} while (isLooped);

	mgr_handle->OutputLog(std::format("[DEBUG] shutdown thread..."));
	return 0;
}




// 初期化
// 引数：
//		logfunc : ログ出力のための関数を指定する
bool Initialize_STMGR(std::function<void(std::string)> logfunc)
{
	// 既に初期化済みなのでnullを返す
	if (mgr_handle->h_thread != nullptr)
	{
		if (logfunc != nullptr) {
			logfunc(std::format("[ERROR] already initialized."));
		}
		return false;
	}

	// まずはスレッド作成
	mgr_handle->logfunc = logfunc;
	mgr_handle->h_thread = ::CreateThread(NULL, 0, AudioStreamProcThread, NULL, 0, NULL);
	if (mgr_handle->h_thread == NULL) {
		// スレッドが作れなかったのでエラー
		if (logfunc != nullptr) {
			logfunc( std::format("[ERROR] can't create thread"));
		}
		return false;
	}
	
	// 初期化コマンド発行
	std::shared_ptr<AS_ARGS_INIT> i = std::make_shared<AS_ARGS_INIT>();
	auto _r = mgr_handle->SetCommandSync(i.get());
	
	// 初期化に失敗したら終了処理を呼んで nullptr を返す
	_r.wait();
	if (_r.get().ret != SM_RECEIVE::OK) {
		// エラー発生
		if (logfunc != nullptr) {
			logfunc(std::format("[ERROR] failed initialized."));
		}
		return false;
	}

	return true;
}



// 終了処理
// 引数：なし
void Release_STMGR()
{
	// 初期化していないなのでnullを返す
	if (mgr_handle->h_thread == nullptr)
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return;
	}

	// 終了コマンド発行
	std::shared_ptr<AS_ARGS_QUIT> q = std::make_shared<AS_ARGS_QUIT>(true);
	auto _r = mgr_handle->SetCommandSync(q.get());

	// 初期化に失敗したら終了処理を呼んで nullptr を返す
	_r.wait();
	if (_r.get().ret != SM_RECEIVE::OK) {
		// エラー発生
		mgr_handle->OutputLog(std::format("[ERROR] failed released."));
		return;
	}
	// スレッドが終了するまで待つ
	::WaitForSingleObject(mgr_handle->h_thread, INFINITE);
	DWORD _ret;
	do {
		::GetExitCodeThread(mgr_handle->h_thread, &_ret);
	} while (_ret == STILL_ACTIVE);
	// スレッドのハンドルを閉じる
	::CloseHandle(mgr_handle->h_thread);
	mgr_handle->h_thread = nullptr;
}



// デバイスリスト取得

// デバイス選択

// 能力取得？

// 能力のセット

// チャンネル作成
// 引数：format : 出力したい波形情報
//       buffersamples : １回のリクエストに必要なバッファサイズ（サンプル数）
// 戻値 : int <0 でエラー、>=0で作成されたチャンネルのindex
int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int)> callbackf)
{
	// 初期化していないなのでnullを返す
	if (mgr_handle->h_thread == nullptr || mgr_handle->xaudio == nullptr )
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return -1;
	}

	// チャンネル作成コマンド発行
	std::shared_ptr<AS_ARGS_MAKECH> m = std::make_shared<AS_ARGS_MAKECH>(format, buffersamples, callbackf);
	auto _r = mgr_handle->SetCommandSync(m.get());

	// 初期化に失敗したら終了処理を呼んで nullptr を返す
	_r.wait();
	auto rr = _r.get();
	if (rr.ret != SM_RECEIVE::OK) {
		// エラー発生
		mgr_handle->OutputLog(std::format("[ERROR] failed make channel."));
		return -1;
	}

	// 確保出来たハンドルindexを返す
	return rr.arg0;
}


// チャンネル破棄

// 再生開始



// 停止

