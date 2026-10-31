#include "backend.h"
#include <cstdlib>
int main()
{
	float output[8];
	float *out[] = { output };
	sdk_media_audio_start(48000, 0, 1, 8);
	sdk_media_audio_process(nullptr, 0, out, 1, 8, 0);
	for (auto value : output)
		if (value != 0)
			std::abort();
	sdk_audio_packet_t packet{};
	sdk_midi_message_t message{};
	if (sdk_audio_read(&packet) != SDK_MEDIA_UNAVAILABLE ||
	    sdk_audio_write(&packet) != SDK_MEDIA_UNAVAILABLE ||
	    sdk_midi_read(&message) != SDK_MEDIA_UNAVAILABLE ||
	    sdk_midi_send(&message) != SDK_MEDIA_UNAVAILABLE)
		std::abort();
}
