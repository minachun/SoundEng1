#include "streamingmgr.h"

/*
 * XAudio2���g�����I�[�f�B�I�X�g���[�~���O�}�l�[�W���[
 * 
 * �����ŃX���b�h��������グ�܂��A���̂��߁A�������ƏI�������͑΂ŌĂяo���Ă��������B
 */


enum class SM_RECEIVE : int {
	OK,
	ERR,
};

// �R�}���h�߂�l�\����
struct AS_RETVAL {
	SM_RECEIVE ret;
	int arg0;
	void* arg1;
	void* arg2;

	// �R���X�g���N�^
	AS_RETVAL() : ret(SM_RECEIVE::OK), arg0(0), arg1(nullptr), arg2(nullptr) {}
	AS_RETVAL(SM_RECEIVE _r, int a0, void* a1, void* a2) : ret(_r), arg0(a0), arg1(a1), arg2(a2) {}
};


enum class SM_COMMAND : int {
	NONE,
	INIT,			// ������
	MAKE_CH,		// �`�����l���쐬
	RELEASE_CH,		// �`�����l���j��
	PLAY_CH,		// �`�����l���Đ�
	STOP_CH,		// �`�����l����~
	REBOOT,			// ������čēx������
	QUIT,			// ��� arg0��1�ɂ���ƃX���b�h�܂ŏI������
};

// �R�}���h�󂯓n���\����
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

// �󂯓n���R�}���h�R���e�i�[�N���X��I/F
class AS_ARGS_BASE {
protected:
	SM_COMMAND command;

public:
	AS_ARGS_BASE(SM_COMMAND _c) : command(_c) {}
	SM_COMMAND GetCommand() { return this->command; }
	virtual std::string ToString() = 0;
};

// �������R�}���h
class AS_ARGS_INIT : public AS_ARGS_BASE {
private:
	// ���ɕK�v�Ȉ����Ȃ�

public:
	AS_ARGS_INIT() : AS_ARGS_BASE(SM_COMMAND::INIT) {}
	std::string ToString() { return std::format("INIT command"); };
};

// ����R�}���h
class AS_ARGS_QUIT : public AS_ARGS_BASE {
private:
	bool isTerminated;

public:
	AS_ARGS_QUIT(bool _r) : isTerminated(_r), AS_ARGS_BASE(SM_COMMAND::QUIT) {}
	bool IsTerminated() { return this->isTerminated; }
	std::string ToString() { return std::format("QUIT command"); };
};

// �`�����l���쐬�R�}���h
class AS_ARGS_MAKECH : public AS_ARGS_BASE {
private:
	WAVEFORMATEX format;
	int buffersamples;
	std::function<void(void*, int, void *)> makecallback;		// �o�b�t�@�ւ̃|�C���^, �v������T���v����

public:
	AS_ARGS_MAKECH(WAVEFORMATEX& _format, int _bufsamp, std::function<void(void*, int, void *)>& _callback) : format(_format), buffersamples(_bufsamp), makecallback(_callback), AS_ARGS_BASE(SM_COMMAND::MAKE_CH) {}
	WAVEFORMATEX GetFormat() { return this->format; }
	int GetBufferSamples() { return this->buffersamples; }
	std::function<void(void*, int, void *)> GetCallback() { return this->makecallback; }
	std::string ToString() { return std::format("MAKECH command"); };
};

// �`�����l���Đ��R�}���h
class AS_ARGS_PLAYCH : public AS_ARGS_BASE {
private:
	int ch;

public:
	AS_ARGS_PLAYCH(int _ch) : ch(_ch), AS_ARGS_BASE(SM_COMMAND::PLAY_CH) {}
	int GetChannel() { return this->ch; }
	std::string ToString() { return std::format("PLAYCH({0:d}) command", this->ch); };
};

// �`�����l����~�R�}���h
class AS_ARGS_STOPCH : public AS_ARGS_BASE {
private:
	int ch;

public:
	AS_ARGS_STOPCH(int _ch) : ch(_ch), AS_ARGS_BASE(SM_COMMAND::STOP_CH) {}
	int GetChannel() { return this->ch; }
	std::string ToString() { return std::format("STOPCH({0:d}) command", this->ch); };
};


class VoiceManager
{
public:
	IXAudio2SourceVoice* sourceV;
	std::function<void(std::string)> logfunc;		// ���O�֐�
	std::function<void(void*, int, void *)> wavecallback;		// �g�`�v���R�[���o�b�N�֐�
	int callbacksamples;
	int bufsize;
	XAUDIO2_BUFFER xbuffer[2];
	int next_buffer;
	std::shared_ptr<BYTE> bufbody[2];

	XAUDIO2_BUFFER* NextBuffer() {
		this->next_buffer = 1 - this->next_buffer;
		return &this->xbuffer[1 - this->next_buffer];
	}

	bool BufferCallback()
	{
		// �o�b�t�@�̊Ď�
		XAUDIO2_VOICE_STATE s;
		this->sourceV->GetState(&s, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (s.BuffersQueued < 2) {
			// �o�b�t�@���󂢂��̂Ŕg�`�v�Z
			for (int _c = 0; _c < (int)(2 - s.BuffersQueued); _c++)
			{
				// �o�b�t�@���g���I��������ɃR�[���o�b�N�����
				XAUDIO2_BUFFER* _b = this->NextBuffer();
				// �R�[���o�b�N�Ăяo��
				this->wavecallback((void*)_b->pAudioData, this->callbacksamples, nullptr);
				// �o�b�t�@��submit����
				HRESULT hr = this->sourceV->SubmitSourceBuffer(_b, nullptr);
				if (FAILED(hr)) {
					this->logfunc(std::format("[ERROR] Failed submit buffer: hr={0:x}", hr));
				}
			}
			return true;
		}
		return false;
	}

	VoiceManager(std::function<void(std::string)> _logfunc, std::function<void(void*, int, void *)> _callback, int _blksamples, int _size )
		: sourceV(nullptr), logfunc(_logfunc), wavecallback(_callback), callbacksamples(_blksamples), bufsize(_size)
	{
		int bsize = this->callbacksamples * this->bufsize;
		this->logfunc(std::format("Makebuffer {0:d}bytes.", bsize));
		this->bufbody[0] = std::make_shared<BYTE>(bsize);
		this->logfunc(std::format("Makebuffer {0:d}bytes.", bsize));
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
			xbuffer[_i].pContext = NULL;
		}
		this->next_buffer = 0;
	}
};


// �����Ŏg�p����Ǘ��p�n���h��
class StreamManager : public IXAudio2EngineCallback {
public:
	HANDLE h_thread;					// �X���b�h�n���h��
	std::mutex commt;
	std::queue<AS_ARGS_BASE *> comqueue;		// �R�}���h�L���[
	std::queue<std::promise<AS_RETVAL>> retqueue;		// �����L���[
	std::function<void(std::string)> logfunc;		// ���O�֐�
	void* log_instance;
	// XAudio2��I/F
	IXAudio2* xaudio;
	// XAudio2MasteringVoice��I/F
	IXAudio2MasteringVoice* mvoice;
	// �e�`�����l��
	std::deque<VoiceManager> voices;

	// �R�}���h��񓯊��œ�����i�߂�l�Ȃ��j
	void SetCommandAsync(AS_ARGS_BASE *command)
	{
		std::lock_guard<std::mutex> lock(this->commt);
		this->comqueue.push(std::move(command));
	}

	// �R�}���h��߂�l�t���œ�����
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

	// �R�}���h����M����
	bool GetCommand(AS_ARGS_BASE** command)
	{
		std::lock_guard<std::mutex> lock(this->commt);
		if (this->comqueue.empty()) {
			// ��Ȃ̂Ń_�~�[��Ԃ�
			return false;
		}
		*command = this->comqueue.front();
		this->comqueue.pop();
		return true;
	}

	// ������Ԃ�
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


	// EngineCallback �� I/F �̎���

	void OnProcessingPassEnd()
	{
		// �I�[�f�B�I�����p�X���I����������ɌĂяo�����
	}

	void OnProcessingPassStart()
	{
		// �I�[�f�B�I�����p�X���J�n����钼�O�ɌĂяo�����
	}

	void OnCriticalError(HRESULT Error)
	{
		// XAudio2����čċN������K�v�����鎞�ɌĂяo�����

		// �����Fhttps://learn.microsoft.com/ja-jp/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2enginecallback-oncriticalerror
		// szDeviceId �p�����[�^�[�œ���̃f�o�C�X�� ID �� IXAudio2::CreateMasteringVoice �Ɏw�肷�邩�AXAUDIO2_NO_VIRTUAL_AUDIO_CLIENT �t���O���g�p����ƁA
		// �d��ȃG���[���������A��ɂȂ� WASAPI �����_�����O �f�o�C�X���g�p�ł��Ȃ��Ȃ����ꍇ�� OnCriticalError ���������܂��B 
		// ����́A�w�b�h�Z�b�g��X�s�[�J�[�����O���ꂽ�ꍇ��AUSB �I�[�f�B�I �f�o�C�X�����O���ꂽ�ꍇ�Ȃǂɔ�������\��������܂��B

		
	}


};



// �Ǘ��p�n���h��
std::shared_ptr<StreamManager> mgr_handle = std::make_shared<StreamManager>();


// �I�[�f�B�I�X�g���[������������
bool InitAudioStream()
{
	// �܂��� COM�̏�����
	HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		// COM�������Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : COM�������Ɏ��s hr={0:08X}", hr));
		return false;
	}
	// XAudio2������
	hr = ::XAudio2Create(&(mgr_handle->xaudio), 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(hr)) {
		// XAudio2�̏������Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2�������Ɏ��s hr={0:08X}", hr));
		return false;
	}
	// EngineCallback�̐ݒ�
	hr = mgr_handle->xaudio->RegisterForCallbacks(mgr_handle.get());
	if (FAILED(hr)) {
		// XAudio2��Callback�ݒ�Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2�������Ɏ��s hr={0:08X}", hr));
		return false;
	}
	// XAudio2MasteringVoice������
	hr = mgr_handle->xaudio->CreateMasteringVoice(&(mgr_handle->mvoice), 2, XAUDIO2_DEFAULT_SAMPLERATE, 0, NULL, NULL, AUDIO_STREAM_CATEGORY::AudioCategory_Other);
	if (FAILED(hr)) {
		// XAudio2MasteringVoice�̏������Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] AudioStreamThread can't initialized : XAudio2MasteringVoice�������Ɏ��s hr={0:08X}", hr));
		return false;
	}
	return true;
}

// �`�����l���m�ۏ���
int MakeChannelAudioStream(const WAVEFORMATEX &format, int bufsamples, std::function<void(void*, int, void *)> callback)
{
	HRESULT hr;
	IXAudio2SourceVoice* sourceV;
	mgr_handle->voices.push_back(VoiceManager(mgr_handle->logfunc, callback, bufsamples, format.nBlockAlign));
	int idx = (int)(mgr_handle->voices.size() - 1);

	hr = mgr_handle->xaudio->CreateSourceVoice(&sourceV, &format, 0, 2.0F, nullptr, NULL, NULL);
	if (FAILED(hr)) {
		// SourceVoice�쐬�Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] sourcevoice can't created : SourceVoice�쐬�Ɏ��s hr={0:08X}", hr));
		return -1;
	}

	// �쐬�o����SourceVoice��o�^����index��Ԃ�
	mgr_handle->voices[idx].sourceV = sourceV;

	return idx;
}

// �`�����l���Đ�����
bool PlayChannelAudioStream(int ch)
{
	HRESULT hr;

	// �Đ��̑O�ɂQ��R�[���o�b�N���Ăяo��
	XAUDIO2_BUFFER* _b;
	_b = mgr_handle->voices[ch].NextBuffer();
	mgr_handle->logfunc(std::format("callbacked 1st. buf={0:p},{1:p},{2:p}", (void *)_b, (void *)_b->pAudioData, (void *)_b->pContext));
	mgr_handle->voices[ch].wavecallback((void*)_b->pAudioData, mgr_handle->voices[ch].callbacksamples, nullptr);
	hr = mgr_handle->voices[ch].sourceV->SubmitSourceBuffer(_b);
	if (FAILED(hr)) {
		// �o�b�t�@�[����Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] voice{0}-{1} can't submit buffer : hr={2:08X}", ch, 0, hr));
		return false;
	}
	_b = mgr_handle->voices[ch].NextBuffer();
	mgr_handle->logfunc(std::format("callbacked 2nd. buf={0:p},{1:p},{2:p}", (void *)_b, (void *)_b->pAudioData, (void*)_b->pContext));
	mgr_handle->voices[ch].wavecallback((void*)_b->pAudioData, mgr_handle->voices[ch].callbacksamples, nullptr);
	hr = mgr_handle->voices[ch].sourceV->SubmitSourceBuffer(_b);
	if (FAILED(hr)) {
		// �o�b�t�@�[����Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] voice{0}-{1} can't submit buffer : hr={2:08X}", ch, 1, hr));
		return false;
	}

	// �Đ��J�n
	mgr_handle->logfunc(std::format("play start."));
	hr = mgr_handle->voices[ch].sourceV->Start(0, 0);

	if (FAILED(hr)) {
		// �Đ��J�n�Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] voice{0} can't play : hr={1:08X}", ch, hr));
		return false;
	}

	// �����ɏ�Ԃ�����Č���
	::Sleep(0);
	XAUDIO2_VOICE_STATE state;
	mgr_handle->voices[ch].sourceV->GetState(&state, 0);
	mgr_handle->OutputLog(std::format("[INFO] state pContext={0:p}, Queued={1:d}, playedsample={2:d}", state.pCurrentBufferContext, state.BuffersQueued, state.SamplesPlayed));

	return true;
}

// �`�����l����~����
bool StopChannelAudioStream(int ch)
{
	HRESULT hr;

	hr = mgr_handle->voices[ch].sourceV->Stop(0, 0);
	if (FAILED(hr)) {
		// ��~�Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] sourcevoice{0:d} can't stopped : Stop()�Ɏ��s hr={1:08X}", ch, hr));
		return false;
	}

	return true;
}

// �I�[�f�B�I�X�g���[���������
bool ReleaseAudioStream()
{
	// XAudio2������i����Ɗ֘A���Ă��ׂẴI�u�W�F�N�g����������͗l�j
	if (mgr_handle->xaudio != nullptr) {
		StopChannelAudioStream(0);
		mgr_handle->xaudio->Release();
		mgr_handle->xaudio = nullptr;
		mgr_handle->mvoice = nullptr;
	}
	// COM�̉��
	::CoUninitialize();
	return true;
}


// �I�[�f�B�I�X�g���[���������C���X���b�h
DWORD WINAPI AudioStreamProcThread(void* _)
{
	mgr_handle->OutputLog(std::format("[DEBUG] bootup thread"));

	bool isLooped = true;
	AS_ARGS_INIT* c_init;
	AS_ARGS_QUIT* c_quit;
	AS_ARGS_MAKECH* c_makech;
	AS_ARGS_PLAYCH* c_playch;
	AS_ARGS_STOPCH* c_stopch;
	std::vector<int> playingch;
	bool _r;
	int ch;
	do {
		// �R�}���h�擾
		AS_ARGS_BASE *args;
		_r = mgr_handle->GetCommand(&args);

		if ( _r == false) {
			// �擾�o���Ȃ�����
			// �o�b�t�@�����C�x���g�Ď����������ĕK�v�ɉ����ăR�[���o�b�N���Ăяo��
			if (playingch.empty() == false) {
				_r = mgr_handle->voices[0].BufferCallback();
			}
			if (_r == false) {
				::Sleep(0);
			}
			continue;
		}

		mgr_handle->OutputLog(std::format("[DEBUG] command proc {0}", args->ToString()));

		// �R�}���h�ʏ���
		switch (args->GetCommand()) {
		case SM_COMMAND::INIT:
			// ������
			c_init = static_cast<AS_ARGS_INIT*>(args);
			isLooped = true;
			_r = InitAudioStream();
			mgr_handle->SetRetval(_r ? SM_RECEIVE::OK : SM_RECEIVE::ERR, 0, nullptr, nullptr);
			break;

		case SM_COMMAND::QUIT:
			// �I��
			c_quit = static_cast<AS_ARGS_QUIT*>(args);
			ReleaseAudioStream();
			if (c_quit->IsTerminated()) {
				// arg0 �� 1 �̎��̓X���b�h���I��������
				isLooped = false;
				mgr_handle->SetRetval(SM_RECEIVE::OK, 0, nullptr, nullptr);
			}
			break;

		case SM_COMMAND::REBOOT:
			// �d��ȃG���[�����ɂ���U�I�����čēx������
			ReleaseAudioStream();
			InitAudioStream();
			break;

		case SM_COMMAND::MAKE_CH:
			// �`�����l���쐬
			c_makech = static_cast<AS_ARGS_MAKECH*>(args);
			ch = MakeChannelAudioStream(c_makech->GetFormat(), c_makech->GetBufferSamples(), c_makech->GetCallback());
			mgr_handle->SetRetval((ch >= 0 ) ? SM_RECEIVE::OK : SM_RECEIVE::ERR, ch, nullptr, nullptr);
			if (ch >= 0) {
				// ���t�`�����l����o�^
				playingch.push_back(ch);
			}
			break;

		case SM_COMMAND::PLAY_CH:
			// �`�����l���Đ�
			c_playch = static_cast<AS_ARGS_PLAYCH*>(args);
			_r = PlayChannelAudioStream(c_playch->GetChannel());
			mgr_handle->SetRetval((_r) ? SM_RECEIVE::OK : SM_RECEIVE::ERR, c_playch->GetChannel(), nullptr, nullptr);
			break;

		case SM_COMMAND::STOP_CH:
			// �`�����l����~
			c_stopch = static_cast<AS_ARGS_STOPCH*>(args);
			_r = StopChannelAudioStream(c_stopch->GetChannel());
			mgr_handle->SetRetval((_r) ? SM_RECEIVE::OK : SM_RECEIVE::ERR, c_stopch->GetChannel(), nullptr, nullptr);
			break;

		}


	} while (isLooped);

	mgr_handle->OutputLog(std::format("[DEBUG] shutdown thread..."));
	return 0;
}




// ������
// �����F
//		logfunc : ���O�o�͂̂��߂̊֐����w�肷��
bool Initialize_STMGR(std::function<void(std::string)> logfunc)
{
	// ���ɏ������ς݂Ȃ̂�null��Ԃ�
	if (mgr_handle->h_thread != nullptr)
	{
		if (logfunc != nullptr) {
			logfunc(std::format("[ERROR] already initialized."));
		}
		return false;
	}

	// �܂��̓X���b�h�쐬
	mgr_handle->logfunc = logfunc;
	mgr_handle->h_thread = ::CreateThread(NULL, 0, AudioStreamProcThread, NULL, 0, NULL);
	if (mgr_handle->h_thread == NULL) {
		// �X���b�h�����Ȃ������̂ŃG���[
		if (logfunc != nullptr) {
			logfunc( std::format("[ERROR] can't create thread"));
		}
		return false;
	}
	
	// �������R�}���h���s
	std::shared_ptr<AS_ARGS_INIT> i = std::make_shared<AS_ARGS_INIT>();
	auto _r = mgr_handle->SetCommandSync(i.get());
	
	// �������Ɏ��s������I���������Ă�� nullptr ��Ԃ�
	_r.wait();
	if (_r.get().ret != SM_RECEIVE::OK) {
		// �G���[����
		if (logfunc != nullptr) {
			logfunc(std::format("[ERROR] failed initialized."));
		}
		return false;
	}

	return true;
}



// �I������
// �����F�Ȃ�
void Release_STMGR()
{
	// ���������Ă��Ȃ��Ȃ̂�null��Ԃ�
	if (mgr_handle->h_thread == nullptr)
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return;
	}

	// �I���R�}���h���s
	std::shared_ptr<AS_ARGS_QUIT> q = std::make_shared<AS_ARGS_QUIT>(true);
	auto _r = mgr_handle->SetCommandSync(q.get());

	// �������Ɏ��s������I���������Ă�� nullptr ��Ԃ�
	_r.wait();
	if (_r.get().ret != SM_RECEIVE::OK) {
		// �G���[����
		mgr_handle->OutputLog(std::format("[ERROR] failed released."));
		return;
	}
	// �X���b�h���I������܂ő҂�
	::WaitForSingleObject(mgr_handle->h_thread, INFINITE);
	DWORD _ret;
	do {
		::GetExitCodeThread(mgr_handle->h_thread, &_ret);
	} while (_ret == STILL_ACTIVE);
	// �X���b�h�̃n���h�������
	::CloseHandle(mgr_handle->h_thread);
	mgr_handle->h_thread = nullptr;
}



// �f�o�C�X���X�g�擾

// �f�o�C�X�I��

// �\�͎擾�H

// �\�͂̃Z�b�g

// �`�����l���쐬
// �����Fformat : �o�͂������g�`���
//       buffersamples : �P��̃��N�G�X�g�ɕK�v�ȃo�b�t�@�T�C�Y�i�T���v�����j
// �ߒl : int <0 �ŃG���[�A>=0�ō쐬���ꂽ�`�����l����index
int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int, void *)> callbackf)
{
	// ���������Ă��Ȃ��Ȃ̂�null��Ԃ�
	if (mgr_handle->h_thread == nullptr || mgr_handle->xaudio == nullptr )
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return -1;
	}

	// �`�����l���쐬�R�}���h���s
	std::shared_ptr<AS_ARGS_MAKECH> m = std::make_shared<AS_ARGS_MAKECH>(format, buffersamples, callbackf);
	auto _r = mgr_handle->SetCommandSync(m.get());

	// �������Ɏ��s������I���������Ă�� nullptr ��Ԃ�
	_r.wait();
	auto rr = _r.get();
	if (rr.ret != SM_RECEIVE::OK) {
		// �G���[����
		mgr_handle->OutputLog(std::format("[ERROR] failed make channel."));
		return -1;
	}

	// �m�ۏo�����n���h��index��Ԃ�
	return rr.arg0;
}


// �`�����l���j��

// �Đ��J�n
// �����F ch : �`�����l����index
// �ߒl�F true �ōĐ�����OK
// ���l�F�Đ����n�߂�O�ɁA�n���ꂽ�R�[���o�b�N�֐����Q��Ăяo����āA�o�b�t�@�Q���̔g�`�f�[�^�̍쐬���K�v�ƂȂ�܂��B
bool PlayChannel_STMGR(int ch)
{
	// ���������Ă��Ȃ��Ȃ̂�null��Ԃ�
	if (mgr_handle->h_thread == nullptr || mgr_handle->xaudio == nullptr)
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return false;
	}

	// �`�����l���쐬�R�}���h���s
	std::shared_ptr<AS_ARGS_PLAYCH> m = std::make_shared<AS_ARGS_PLAYCH>(ch);
	auto _r = mgr_handle->SetCommandSync(m.get());

	// �������Ɏ��s������I���������Ă�� nullptr ��Ԃ�
	_r.wait();
	auto rr = _r.get();
	if (rr.ret != SM_RECEIVE::OK) {
		// �G���[����
		mgr_handle->OutputLog(std::format("[ERROR] failed make channel."));
		return false;
	}

	return true;
}


// ��~
void StopChannel_STMGR(int ch)
{
	// ���������Ă��Ȃ��Ȃ̂�null��Ԃ�
	if (mgr_handle->h_thread == nullptr || mgr_handle->xaudio == nullptr)
	{
		mgr_handle->OutputLog(std::format("[ERROR] not initialized AudioStream."));
		return;
	}

	// �`�����l���쐬�R�}���h���s
	std::shared_ptr<AS_ARGS_STOPCH> m = std::make_shared<AS_ARGS_STOPCH>(ch);
	auto _r = mgr_handle->SetCommandSync(m.get());

	// �������Ɏ��s������I���������Ă�� nullptr ��Ԃ�
	_r.wait();
	auto rr = _r.get();
	if (rr.ret != SM_RECEIVE::OK) {
		// �G���[����
		mgr_handle->OutputLog(std::format("[ERROR] failed make channel."));
		return;
	}
}
