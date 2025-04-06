

#include <stdio.h>
#include <string>

#include "streamingmgr.h"



void consolelog(std::string mes)
{
	printf("%s\n", mes.c_str());
}

void wavecallbackfunc(void* buf, int samples)
{

}


int main(int argc, char* argv[])
{
	// ƒeƒXƒg

	std::function<void(std::string)> logf = consolelog;
	bool r = Initialize_STMGR(logf);
	printf("return %d\n", r);

	WAVEFORMATEX w;
	w.wFormatTag = WAVE_FORMAT_PCM;
	w.nChannels = 2;
	w.nSamplesPerSec = 48000;
	w.wBitsPerSample = 16;
	w.nBlockAlign = w.wBitsPerSample * w.nChannels / 8;
	w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
	std::function<void(void*, int)> cbf = wavecallbackfunc;
	int ch = MakeChannel_STMGR(w, w.nSamplesPerSec / 100, cbf);
	printf("ch %d\n", ch);

	Release_STMGR();


	return 0;
}