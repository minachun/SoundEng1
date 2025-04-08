

#include <stdio.h>
#include <string>

#include "streamingmgr.h"



void consolelog(std::string mes)
{
	printf("%s\n", mes.c_str());
}



// 16bit������PSG
class PSG16 {
private:
	// �g�`�̐��x���グ��ׂ̐��܂���
	static const int freq_multiple = 128;

	// ���� �ő傪 1.0f
	float left_volume;
	float right_volume;
	int16_t cur_lvol;
	int16_t cur_rvol;

	int base_hz;
	int base_hz_mult;
	int wavetype;		// �g�`�^�C�v�iOn/Off��j�@1-15

	float freq;
	int cur_freq;

	int wave_cnt;
	int wave_cnt_h;		// h��Ԃ̃J�E���^(�R���𒴂����l�ɂȂ�)
	int waveindex;		// �g�`index 1-16

	bool isKeyOn;

public:
	// �����Foutputhz = �o�͎��g��
	PSG16(int outputhz) : base_hz(outputhz), left_volume(0.0f), right_volume(0.0f), wavetype(8), freq(440.0f), wave_cnt(0), waveindex(1), isKeyOn(false)
	{
		this->base_hz_mult = base_hz * PSG16::freq_multiple;
		this->cur_lvol = this->left_volume * 32767.0f;
		this->cur_rvol = this->right_volume * 32767.0f;
		this->cur_freq = this->freq * PSG16::freq_multiple;
	}
	
	// ���g���ݒ�
	void SetFreq(float hz)
	{
		if (hz * 2 >= base_hz) {
			// �l��I�[�o�[�Ȃ̂ŉ������Ȃ�
			return;
		}
		this->freq = hz;
		this->cur_freq = this->freq;
	}

	// ���ʐݒ�
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
		this->waveindex = 1;		// �g�`�ʒu��擪�Ƀ��Z�b�g
		this->wave_cnt = 0;
		this->isKeyOn = true;
	}

	// KeyOff
	void SetKeyOff()
	{
		this->isKeyOn = false;
	}

	// �g�`�w��
	// �����@: wtype 1-15 
	void SetWaveType(int wtype)
	{
		if (wtype < 1 || wtype > 15) return;
		this->wavetype = wtype;
		this->wave_cnt_h = this->base_hz * this->wavetype / 16;
	}

	// �g�`����
	void MakeWave(int16_t* buffer, int samples)
	{
		if (isKeyOn == false) {
			// �L�[�I�����ĂȂ��̂Ŗ���
			for (int _i = 0; _i < samples; _i++) {
				buffer[_i * 2 + 0] = 0x0000;
				buffer[_i * 2 + 1] = 0x0000;
			}
		}
		else {
			for (int _i = 0; _i < samples; _i++) {
				this->wave_cnt += this->cur_freq;
				if (this->wave_cnt >= this->base_hz) this->wave_cnt -= this->base_hz;
				// �o��
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
		consolelog(std::format("wavecallback {0:d}samples. ptr={1:p},{2:p} audiobytes={3:d} buffer={4:p} {5:d}", samples, buf, ptr, p->AudioBytes, (void *)p->pAudioData, p->PlayBegin));
	}
	else {
		consolelog(std::format("wavecallback {0:d}samples. ptr={1:p},{2:p}", samples, buf, ptr));
	}
	int16_t* pbuf = (int16_t*)buf;
	psg->MakeWave(pbuf,samples);
}


int main(int argc, char* argv[])
{
	// �e�X�g

	std::function<void(std::string)> logf = consolelog;
	bool r = Initialize_STMGR(logf);
	printf("return %d\n", r);

	psg = new PSG16(48000);

	WAVEFORMATEX w;
	w.wFormatTag = WAVE_FORMAT_PCM;
	w.nChannels = 2;
	w.nSamplesPerSec = 48000;
	w.wBitsPerSample = 16;
	w.nBlockAlign = w.wBitsPerSample * w.nChannels / 8;
	w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
	std::function<void(void*, int, void *)> cbf = wavecallbackfunc;
	int ch = MakeChannel_STMGR(w, w.nSamplesPerSec / 50, cbf);
	printf("ch %d\n", ch);
	psg->SetFreq(440.0);
	psg->SetVolume(0.7f, 0.3f);
	psg->SetWaveType(8);
	psg->SetKeyOn();

	r = PlayChannel_STMGR(ch);
	printf("play = %d\n", r);
	Sleep(1000);
	psg->SetFreq(440.0 + 20.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 40.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 60.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 80.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 100.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 120.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 140.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 160.0);
	Sleep(1000);
	psg->SetFreq(440.0 + 180.0);
	Sleep(1000);
	StopChannel_STMGR(ch);

	Release_STMGR();

	delete psg;

	return 0;
}