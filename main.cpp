

#include <stdio.h>
#include <string>

#include "streamingmgr.h"




// 16bit整数のPSG
class PSG16 {
private:
	// 波形の精度を上げる為の嵩まし分
	static const int freq_multiple = 128;

	// 音量 最大が 1.0f
	float left_volume;
	float right_volume;
	int16_t cur_lvol;
	int16_t cur_rvol;

	int base_hz;
	int base_hz_mult;
	int wavetype;		// 波形タイプ（On/Off比）　1-15

	float freq;
	int cur_freq;

	int wave_cnt;
	int wave_cnt_h;		// h区間のカウンタ(コレを超えるとlになる)
	int waveindex;		// 波形index 1-16

	bool isKeyOn;

public:
	// 引数：outputhz = 出力周波数
	PSG16(int outputhz) : base_hz(outputhz), left_volume(0.0f), right_volume(0.0f), wavetype(8), freq(440.0f), wave_cnt(0), waveindex(1), isKeyOn(false)
	{
		this->base_hz_mult = base_hz * PSG16::freq_multiple;
		this->cur_lvol = this->left_volume * 32767.0f;
		this->cur_rvol = this->right_volume * 32767.0f;
		this->cur_freq = this->freq * PSG16::freq_multiple;
	}
	
	// 周波数設定
	void SetFreq(float hz)
	{
		if (hz * 2 >= base_hz) {
			// 値域オーバーなので何もしない
			return;
		}
		this->freq = hz;
		this->cur_freq = this->freq;
	}

	// 音量設定
	void SetVolume(float leftv, float rightv)
	{
		this->left_volume = leftv;
		this->right_volume = rightv;
		this->cur_lvol = this->left_volume * 32767.0f;
		this->cur_rvol = this->right_volume * 32767.0f;
	}

	// KeyOn
	void SetKeyOn()
	{
		this->waveindex = 1;		// 波形位置を先頭にリセット
		this->wave_cnt = 0;
		this->isKeyOn = true;
	}

	// KeyOff
	void SetKeyOff()
	{
		this->isKeyOn = false;
	}

	// 波形指定
	// 引数　: wtype 1-15 
	void SetWaveType(int wtype)
	{
		if (wtype < 1 || wtype > 15) return;
		this->wavetype = wtype;
		this->wave_cnt_h = this->base_hz * this->wavetype / 16;
	}

	// 波形生成
	void MakeWave(int16_t* buffer, int samples)
	{
		if (isKeyOn == false) {
			// キーオンしてないので無音
			for (int _i = 0; _i < samples; _i++) {
				buffer[_i * 2 + 0] = 0x0000;
				buffer[_i * 2 + 1] = 0x0000;
			}
		}
		else {
			for (int _i = 0; _i < samples; _i++) {
				this->wave_cnt += this->cur_freq;
				if (this->wave_cnt >= this->base_hz) this->wave_cnt -= this->base_hz;
				// 出力
				if (this->wave_cnt < this->wave_cnt_h)
				{
					buffer[_i * 2 + 0] = this->cur_lvol;
					buffer[_i * 2 + 1] = this->cur_rvol;
				}
				else {
					buffer[_i * 2 + 0] = 0 - this->cur_lvol;
					buffer[_i * 2 + 1] = 0 - this->cur_rvol;
				}
			}
		}
	}
};


PSG16* psg;

void wavecallbackfunc(void* buf, int samples, void *ptr)
{
	if (ptr != nullptr) {
		XAUDIO2_BUFFER* p = (XAUDIO2_BUFFER*)ptr;
		// LOG_INFO(logger, "wavecallback {0:d}samples. ptr={1:p} audiobytes={2:d} buffer={3:p} {4:d}", samples, buf, (int)p->AudioBytes, (void *)p->pAudioData, (int)p->PlayBegin);
		LOG_INFO(logger, "wavecallback {0:d}samples. audiobytes={1:d}", samples, (int)p->AudioBytes);
	}
	else {
		LOG_INFO(logger, "wavecallback {0:d}samples.", samples);
	}
	int16_t* pbuf = (int16_t*)buf;
	psg->MakeWave(pbuf,samples);
}

quill::Logger* logger;


int main()
{
	quill::Backend::start();
	auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("sink_id_1");
	logger = quill::Frontend::create_or_get_logger("root", std::move(console_sink));

	// テスト
	bool r = Initialize_STMGR();
	LOG_INFO(logger, "return {}", r);

	psg = new PSG16(48000);

	WAVEFORMATEX w{};
	w.wFormatTag = WAVE_FORMAT_PCM;
	w.nChannels = 2;
	w.nSamplesPerSec = 48000;
	w.wBitsPerSample = 16;
	w.nBlockAlign = w.wBitsPerSample * w.nChannels / 8;
	w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
	std::function<void(void*, int, void *)> cbf = wavecallbackfunc;
	int ch = MakeChannel_STMGR(w, w.nSamplesPerSec / 100, cbf);
	LOG_INFO(logger, "ch {}", ch);
	psg->SetFreq(440.0);
	psg->SetVolume(0.7f, 0.3f);
	psg->SetWaveType(8);
	psg->SetKeyOn();

	r = PlayChannel_STMGR(ch);
	LOG_INFO(logger, "play = {}", r);
	Sleep(1000);
	psg->SetFreq(440.0 + 20.0);
	psg->SetVolume(0.3f, 0.7f);
	Sleep(1000);
	psg->SetFreq(440.0 + 40.0);
	psg->SetVolume(0.7f, 0.3f);
	Sleep(1000);
	psg->SetFreq(440.0 + 60.0);
	psg->SetVolume(0.3f, 0.7f);
	Sleep(1000);
	psg->SetFreq(440.0 + 80.0);
	psg->SetVolume(0.7f, 0.3f);
	Sleep(1000);
	psg->SetFreq(440.0 + 100.0);
	psg->SetVolume(0.3f, 0.7f);
	Sleep(1000);
	psg->SetFreq(440.0 + 120.0);
	psg->SetVolume(0.7f, 0.3f);
	Sleep(1000);
	psg->SetFreq(440.0 + 140.0);
	psg->SetVolume(0.3f, 0.7f);
	Sleep(1000);
	psg->SetFreq(440.0 + 160.0);
	psg->SetVolume(0.7f, 0.3f);
	Sleep(1000);
	psg->SetFreq(440.0 + 180.0);
	psg->SetVolume(0.3f, 0.7f);
	Sleep(1000);
	StopChannel_STMGR(ch);

	Release_STMGR();

	delete psg;

	return 0;
}