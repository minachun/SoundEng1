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
	std::function<void(void*, int)> makecallback;		// �o�b�t�@�ւ̃|�C���^, �v������T���v����

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
	std::function<void(std::string)> logfunc;		// ���O�֐�

	// VoiceCallback �̎���
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

// �I�[�f�B�I�X�g���[���������
bool ReleaseAudioStream()
{
	// XAudio2������i����Ɗ֘A���Ă��ׂẴI�u�W�F�N�g����������͗l�j
	if (mgr_handle->xaudio != nullptr) {
		mgr_handle->xaudio->Release();
		mgr_handle->xaudio = nullptr;
		mgr_handle->mvoice = nullptr;
	}
	// COM�̉��
	::CoUninitialize();
	return true;
}

// �`�����l���m�ۏ���
int MakeChannelAudioStream(const WAVEFORMATEX &format, int bufsamples, std::function<void(void*, int)> callback)
{
	HRESULT hr;
	IXAudio2SourceVoice* sourceV;
	VoiceManager vm(mgr_handle->logfunc);

	hr = mgr_handle->xaudio->CreateSourceVoice(&sourceV, &format, 0, 2.0F, &vm, NULL, NULL);
	if (FAILED(hr)) {
		// SourceVoice�쐬�Ɏ��s
		mgr_handle->OutputLog(std::format("[ERROR] sourcevoice can't created : SourceVoice�쐬�Ɏ��s hr={0:08X}", hr));
		return -1;
	}

	// �쐬�o����SourceVoice��o�^����index��Ԃ�
	vm.sourceV = sourceV;
	mgr_handle->voices.push_back(std::move(vm));
	int idx = (int)(mgr_handle->voices.size() - 1);

	return idx;
}


// �I�[�f�B�I�X�g���[���������C���X���b�h
DWORD WINAPI AudioStreamProcThread(void* _)
{
	mgr_handle->OutputLog(std::format("[DEBUG] bootup thread"));

	bool isLooped = true;
	AS_ARGS_INIT* c_init;
	AS_ARGS_QUIT* c_quit;
	AS_ARGS_MAKECH* c_makech;
	do {
		// �R�}���h�擾
		AS_ARGS_BASE *args;
		bool _r = mgr_handle->GetCommand(&args);

		if ( _r == false) {
			// �擾�o���Ȃ�����
			::Sleep(0);
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
			int ch = MakeChannelAudioStream(c_makech->GetFormat(), c_makech->GetBufferSamples(), c_makech->GetCallback());
			mgr_handle->SetRetval((ch >= 0 ) ? SM_RECEIVE::OK : SM_RECEIVE::ERR, ch, nullptr, nullptr);
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
int MakeChannel_STMGR(WAVEFORMATEX format, int buffersamples, std::function<void(void*, int)> callbackf)
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



// ��~

