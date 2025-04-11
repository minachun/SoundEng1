#include "streamingmgr.h"

/*
 * XAudio2を使ったオーディオストリーミングマネージャー
 * 
 */


class engcallback : public IXAudio2EngineCallback
{
public:

	// EngineCallback の I/F の実装

	void STDMETHODCALLTYPE OnProcessingPassEnd() override;

	void STDMETHODCALLTYPE OnProcessingPassStart() override;

	void STDMETHODCALLTYPE OnCriticalError(HRESULT Error) override;

	engcallback();
};

class StreamManager;

class voicecallback : public IXAudio2VoiceCallback
{

private:
	StreamManager* sm;

public:
	void STDMETHODCALLTYPE OnBufferEnd(void* pBufferContext) override;
	void STDMETHODCALLTYPE OnBufferStart(void* pBufferContext) override;
	void STDMETHODCALLTYPE OnLoopEnd(void* pBufferContext) override;
	void STDMETHODCALLTYPE OnStreamEnd() override;
	void STDMETHODCALLTYPE OnVoiceError(void* pBufferContext, HRESULT Error) override;
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override;
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 BytesRequired) override;
	voicecallback(StreamManager* _s);
};



// 内部で使用する管理用ハンドル
class StreamManager {
public:
	// XAudio2のI/F
	IXAudio2* xaudio;
	// XAudio2MasteringVoiceのI/F
	IXAudio2MasteringVoice* mvoice;
	IXAudio2SourceVoice* sourceV;
	int prepare_index;
	int submit_index;

	std::function<void(void*, int)> wavecallback;		// 波形要求コールバック関数
	int callbacksamples;
	int bufsize;
	XAUDIO2_BUFFER xbuffer[2];
	int next_buffer;
	std::shared_ptr<BYTE> bufbody[2];
	std::atomic<bool> stopflag;

	engcallback* eng;
	voicecallback* vbk;

	StreamManager() : xaudio(nullptr), mvoice(nullptr), sourceV(nullptr), stopflag(false), eng(new engcallback()), bufsize(0), callbacksamples(0), next_buffer(0), prepare_index(0)
	{
		this->vbk = new voicecallback(this);
		this->submit_index = 0;
		this->xbuffer[0] = {};
		this->xbuffer[1] = {};
	}




	void PrepareBuffer(UINT32 samples)
	{
		//LOG_INFO(logger, "PrepareBuffer {0:d}", this->prepare_index);
		XAUDIO2_BUFFER* _b = &this->xbuffer[this->prepare_index];
		// コールバック呼び出し
		this->wavecallback((void*)_b->pAudioData, samples / this->bufsize);
		this->prepare_index = 1 - this->prepare_index;
	}

	void SubmitBuffer()
	{
		if (this->stopflag) return;
		//LOG_INFO(logger, "SubmitBuffer {0:d}", this->submit_index);
		XAUDIO2_BUFFER* _b = &this->xbuffer[this->submit_index];
		// バッファをsubmitする
		HRESULT hr = this->sourceV->SubmitSourceBuffer(_b, nullptr);
		if (FAILED(hr)) {
			//LOG_INFO(logger, "[ERROR] Failed submit buffer: hr={0:x}", hr);
		}
		this->submit_index = 1 - this->submit_index;
	}


	// オーディオストリーム初期化処理
	bool Init()
	{
		// まずは COMの初期化
		HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(hr)) {
			// COM初期化に失敗
			//LOG_INFO(logger, "[ERROR] AudioStreamThread can't initialized : COM初期化に失敗 hr={0:08X}", hr);
			return false;
		}
		// XAudio2初期化
		hr = ::XAudio2Create(&(this->xaudio), 0, XAUDIO2_DEFAULT_PROCESSOR);
		if (FAILED(hr)) {
			// XAudio2の初期化に失敗
			//LOG_INFO(logger, "[ERROR] AudioStreamThread can't initialized : XAudio2初期化に失敗 hr={0:08X}", hr);
			return false;
		}
		// EngineCallbackの設定
		hr = this->xaudio->RegisterForCallbacks(this->eng);
		if (FAILED(hr)) {
			// XAudio2のCallback設定に失敗
			//LOG_INFO(logger, "[ERROR] AudioStreamThread can't initialized : XAudio2初期化に失敗 hr={0:08X}", hr);
			return false;
		}
		// XAudio2MasteringVoice初期化
		hr = this->xaudio->CreateMasteringVoice(&(this->mvoice), 2, XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL, AUDIO_STREAM_CATEGORY::AudioCategory_Other);
		if (FAILED(hr)) {
			// XAudio2MasteringVoiceの初期化に失敗
			//LOG_INFO(logger, "[ERROR] AudioStreamThread can't initialized : XAudio2MasteringVoice初期化に失敗 hr={0:08X}", hr);
			return false;
		}
		return true;
	}

	// チャンネル確保処理
	bool MakeChannel(const WAVEFORMATEX& format, int bufsamples, std::function<void(void*, int)> callback)
	{
		HRESULT hr;
		// mgr_handle->voices.push_back(VoiceManager(callback, bufsamples, format.nBlockAlign));

		hr = this->xaudio->CreateSourceVoice(&(this->sourceV), &format, 0, 2.0F, this->vbk, NULL, NULL);
		if (FAILED(hr)) {
			// SourceVoice作成に失敗
			//LOG_INFO(logger, "[ERROR] sourcevoice can't created : SourceVoice作成に失敗 hr={0:08X}", hr);
			return false;
		}

		// バッファの準備
		this->callbacksamples = bufsamples;
		this->wavecallback = callback;
		this->bufsize = format.nBlockAlign;
		int bsize = this->callbacksamples * this->bufsize;
		//LOG_INFO(logger, "Makebuffer {0:d}bytes.", bsize);
		this->bufbody[0] = std::make_shared<BYTE>(bsize);
		//LOG_INFO(logger, "Makebuffer {0:d}bytes.", bsize);
		this->bufbody[1] = std::make_shared<BYTE>(bsize);
		for (int _i = 0; _i < 2; _i++) {
			xbuffer[_i].Flags = 0;
			xbuffer[_i].AudioBytes = bsize;
			xbuffer[_i].pAudioData = this->bufbody[_i].get();
			xbuffer[_i].PlayBegin = 0;
			xbuffer[_i].PlayLength = 0;
			xbuffer[_i].LoopBegin = 0;
			xbuffer[_i].LoopLength = 0;
			xbuffer[_i].LoopCount = 0;
			xbuffer[_i].pContext = (void*)_i;
		}
		this->next_buffer = 0;
		this->prepare_index = 0;
		this->submit_index = 0;

		return true;
	}

	// チャンネル再生処理
	bool PlayChannel()
	{
		HRESULT hr;

		this->stopflag = false;

		// 再生開始前に、まずバッファ一つ目の準備
		this->PrepareBuffer(this->callbacksamples * this->bufsize);

		// 作ったバッファをキューへ登録
		this->SubmitBuffer();

		// ２つめの準備をする
		// this->PrepareBuffer();

		// 再生開始
		//LOG_INFO(logger, "play start.");
		hr = this->sourceV->Start(0, 0);

		if (FAILED(hr)) {
			// 再生開始に失敗
			//LOG_INFO(logger, "[ERROR] voice can't play : hr={0:08X}", hr);
			return false;
		}

		return true;
	}

	// チャンネル停止処理
	bool StopChannel()
	{
		HRESULT hr;

		this->stopflag = true;
		hr = this->sourceV->Stop(0, 0);
		if (FAILED(hr)) {
			// 停止に失敗
			//LOG_INFO(logger, "[ERROR] sourcevoice can't stopped : Stop()に失敗 hr={0:08X}", hr);
			return false;
		}
		// バッファをリフレッシュする
		hr = this->sourceV->FlushSourceBuffers();
		if (FAILED(hr)) {
			// バッファのフラッシュに失敗
			//LOG_INFO(logger, "[ERROR] sourcevoice can't flushed : FlushSourceBuffers()に失敗 hr={0:08X}", hr);
			return false;
		}
		return true;
	}

	// オーディオストリーム解放処理
	bool Release()
	{
		// XAudio2を解放（すると関連してすべてのオブジェクトも解放される模様）
		if (this->xaudio != nullptr) {
			// this->StopChannel();
			this->sourceV->DestroyVoice();
			this->mvoice->DestroyVoice();
			//this->xaudio->Release();
			this->xaudio = nullptr;
			this->mvoice = nullptr;
		}
		this->bufbody[0].reset();
		this->bufbody[1].reset();
		// COMの解放
		::CoUninitialize();
		delete this->eng;
		delete this->vbk;
		return true;
	}


};



// EngineCallback の I/F の実装
void STDMETHODCALLTYPE engcallback::OnProcessingPassEnd()
{
	// オーディオ処理パスが終了した直後に呼び出される
}

void STDMETHODCALLTYPE engcallback::OnProcessingPassStart()
{
	// オーディオ処理パスが開始される直前に呼び出される
}

void STDMETHODCALLTYPE engcallback::OnCriticalError(HRESULT Error)
{
	// XAudio2を閉じて再起動する必要がある時に呼び出される

	// 抜粋：https://learn.microsoft.com/ja-jp/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-oncriticalerror
	// szDeviceId パラメーターで特定のデバイスの ID を IXAudio2::CreateMasteringVoice に指定するか、XAUDIO2_NO_VIRTUAL_AUDIO_CLIENT フラグを使用すると、
	// 重大なエラーが発生し、基になる WASAPI レンダリング デバイスが使用できなくなった場合は OnCriticalError が発生します。 
	// これは、ヘッドセットやスピーカーが取り外された場合や、USB オーディオ デバイスが取り外された場合などに発生する可能性があります。
}

engcallback::engcallback()
{
}

void STDMETHODCALLTYPE voicecallback::OnBufferEnd(void* pBufferContext)
{

}
void STDMETHODCALLTYPE voicecallback::OnBufferStart(void* pBufferContext)
{
	// 準備済みを登録
	// this->sm->SubmitBuffer();

	// 次のバッファの準備を通知する
	//this->logfunc("SetEvent");
	//::SetEvent(this->notifyPrepared);
	//this->sm->PrepareBuffer();
}
void STDMETHODCALLTYPE voicecallback::OnLoopEnd(void* pBufferContext)
{

}

void STDMETHODCALLTYPE voicecallback::OnStreamEnd()
{
}

void STDMETHODCALLTYPE voicecallback::OnVoiceError(void* pBufferContext, HRESULT Error)
{
}

void STDMETHODCALLTYPE voicecallback::OnVoiceProcessingPassEnd()
{
}

void STDMETHODCALLTYPE voicecallback::OnVoiceProcessingPassStart(UINT32 BytesRequired)
{
	if (BytesRequired > 0)
	{
		this->sm->PrepareBuffer(BytesRequired);
		this->sm->SubmitBuffer();
		//LOG_INFO(logger, "OnVoiceProcessingPassStart {0:d}", BytesRequired);
	}
}

voicecallback::voicecallback(StreamManager* _s) : sm(_s) {}



std::unique_ptr<StreamManager> mgr_handle = nullptr;




// 初期化
// 引数：
//		logfunc : ログ出力のための関数を指定する
bool Initialize_STMGR()
{
	if (mgr_handle != nullptr) {
		//LOG_INFO(logger, "[ERROR] already initialized.");
		return false;
	}
	mgr_handle = std::make_unique<StreamManager>();

	// 初期化コマンド発行
	if (mgr_handle->Init() == false) {
		// エラー発生
		//LOG_INFO(logger, "[ERROR] failed initialized.");
		return false;
	}

	return true;
}



// 終了処理
// 引数：なし
void Release_STMGR()
{
	// 初期化していないなのでnullを返す
	if (mgr_handle == nullptr)
	{
		//LOG_INFO(logger, "[ERROR] not initialized AudioStream.");
		return;
	}

	// 終了コマンド発行
	mgr_handle->Release();
	mgr_handle.release();
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
	if (mgr_handle == nullptr )
	{
		//LOG_INFO(logger, "[ERROR] not initialized AudioStream.");
		return -1;
	}

	// チャンネル作成コマンド発行
	mgr_handle->MakeChannel(format, buffersamples, callbackf);

	// 確保出来たハンドルindexを返す
	return 0;
}


// チャンネル破棄

// 再生開始
// 引数： ch : チャンネルのindex
// 戻値： true で再生処理OK
// 備考：再生を始める前に、渡されたコールバック関数が２回呼び出されて、バッファ２つ分の波形データの作成が必要となります。
bool PlayChannel_STMGR(int ch)
{
	// 初期化していないなのでnullを返す
	if (mgr_handle == nullptr)
	{
		//LOG_INFO(logger, "[ERROR] not initialized AudioStream.");
		return false;
	}

	// チャンネル作成コマンド発行
	mgr_handle->PlayChannel();

	return true;
}


// 停止
void StopChannel_STMGR(int ch)
{
	// 初期化していないなのでnullを返す
	if (mgr_handle == nullptr )
	{
		//LOG_INFO(logger, "[ERROR] not initialized AudioStream.");
		return;
	}

	// チャンネル作成コマンド発行
	mgr_handle->StopChannel();
}
