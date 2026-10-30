#include "sdk/audio.h"
#include "sdk/midi.h"
/* Compiled as C, proving user code needs no JUCE or C++ declarations. */
void test_c_audio_process(const sdk_audio_process_block_t *block, void *context)
{
	const float gain = *(const float *)context;
	for (unsigned c = 0; c < block->output_channels; ++c)
		for (unsigned i = 0; i < block->frames; ++i)
			if (block->output && block->output[c])
				block->output[c][i] =
					c < block->input_channels &&
							block->input &&
							block->input[c] ?
						block->input[c][i] * gain :
						0.0f;
}
