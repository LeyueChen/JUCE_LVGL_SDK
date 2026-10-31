#include "backend.h"
#include "bounded_queue.h"
#include "sdk_config.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace
{
#ifdef CONFIG_SDK_AUDIO
sdk_media::Queue<sdk_audio_packet_t, CONFIG_SDK_AUDIO_QUEUE_BLOCKS> capture,
	playback;
#endif
#ifdef CONFIG_SDK_MIDI
sdk_media::Queue<sdk_midi_message_t, CONFIG_SDK_MIDI_QUEUE_MESSAGES> midi_input,
	midi_output;
#endif
std::atomic<const sdk_audio_processor_t *> processor{ nullptr };
std::atomic<bool> capture_enabled{ false }, playback_enabled{ false };
std::atomic<uint32_t> sequence{ 0 }, rate{ 0 }, block_size{ 0 };
std::atomic<unsigned> input_count{ 0 }, output_count{ 0 };
std::atomic<bool> running{ false };
std::atomic<uint64_t> callbacks{ 0 }, input_drops{ 0 }, underruns{ 0 };
std::atomic<unsigned> midi_inputs{ 0 }, midi_outputs{ 0 }, midi_enabled{ 0 };
std::atomic<bool> midi_available{ false };
std::atomic<uint64_t> received{ 0 }, sent{ 0 }, midi_input_drops{ 0 },
	midi_output_drops{ 0 };
#ifdef CONFIG_SDK_AUDIO
sdk_audio_packet_t
	current_output{}; // Audio callback owns this packet and offset.
unsigned output_offset = 0;
#endif
std::atomic<bool> muted{ false };
std::atomic<bool> clear_playback{ false };
bool midi_valid(const sdk_midi_message_t &m)
{
	if (m.size == 0 || m.size > SDK_MIDI_MAX_BYTES || m.data[0] < 0x80)
		return false;
	const auto status = m.data[0];
	if (status == 0xf0) {
		if (m.size < 2 || m.data[m.size - 1] != 0xf7)
			return false;
		for (unsigned i = 1; i + 1 < m.size; ++i)
			if (m.data[i] >= 0x80)
				return false;
		return true;
	}
	unsigned size =
		status < 0xf0 ?
			((status & 0xe0) == 0xc0 ? 2 : 3) :
			(status == 0xf1 || status == 0xf3 ? 2 :
			 status == 0xf2			  ? 3 :
			 status == 0xf6 || status == 0xf8 || status == 0xfa ||
					 status == 0xfb || status == 0xfc ||
					 status == 0xfe || status == 0xff ?
					  1 :
					  0);
	if (m.size != size)
		return false;
	for (unsigned i = 1; i < m.size; ++i)
		if (m.data[i] >= 0x80)
			return false;
	return true;
}
}
extern "C" void sdk_audio_set_processor(const sdk_audio_processor_t *binding)
{
	processor.store(binding, std::memory_order_release);
}
extern "C" void sdk_audio_playback_mute(int value)
{
	muted.store(value != 0);
}
extern "C" void sdk_audio_playback_clear(void)
{
	clear_playback.store(true);
}
extern "C" void sdk_audio_capture_enable(int enabled)
{
	capture_enabled.store(enabled != 0);
}
extern "C" int sdk_audio_get_format(sdk_audio_format_t *format)
{
	if (!format)
		return SDK_MEDIA_INVALID;
	for (int attempt = 0; attempt < 4; ++attempt) {
		auto before = sequence.load(std::memory_order_acquire);
		if (before & 1)
			continue;
		sdk_audio_format_t value{ before,
					  rate.load(),
					  block_size.load(),
					  input_count.load(),
					  output_count.load(),
					  running.load() ? 1 : 0 };
		if (before == sequence.load(std::memory_order_acquire)) {
			*format = value;
			return 0;
		}
	}
	return SDK_MEDIA_BUSY;
}
extern "C" sdk_audio_stats_t sdk_audio_stats(void)
{
	return { callbacks.load(), input_drops.load(), underruns.load() };
}
extern "C" int sdk_audio_read(sdk_audio_packet_t *packet)
{
	if (!packet)
		return SDK_MEDIA_INVALID;
#ifndef CONFIG_SDK_AUDIO
	return SDK_MEDIA_UNAVAILABLE;
#else
	return capture.pop(*packet) ? 1 : 0;
#endif
}
extern "C" int sdk_audio_write(const sdk_audio_packet_t *packet)
{
#ifndef CONFIG_SDK_AUDIO
	(void)packet;
	return SDK_MEDIA_UNAVAILABLE;
#else
	if (!packet || !packet->frames ||
	    packet->frames > SDK_AUDIO_BLOCK_FRAMES || !packet->channels ||
	    packet->channels > SDK_AUDIO_MAX_CHANNELS)
		return SDK_MEDIA_INVALID;
	sdk_audio_format_t f{};
	if (sdk_audio_get_format(&f))
		return SDK_MEDIA_BUSY;
	if (!f.running || !f.output_channels)
		return SDK_MEDIA_UNAVAILABLE;
	if (processor.load())
		return SDK_MEDIA_BUSY;
	if (packet->generation != f.generation ||
	    packet->sample_rate_hz != f.sample_rate_hz ||
	    packet->channels != f.output_channels)
		return SDK_MEDIA_INVALID;
	for (unsigned i = 0; i < packet->frames * packet->channels; ++i)
		if (!std::isfinite(packet->samples[i]) ||
		    std::abs(packet->samples[i]) > 1.0f)
			return SDK_MEDIA_INVALID;
	if (!playback.push(*packet))
		return SDK_MEDIA_FULL;
	playback_enabled.store(true);
	return 0;
#endif
}
extern "C" void sdk_media_audio_start(unsigned sample_rate, unsigned inputs,
				      unsigned outputs, unsigned frames)
{
	sequence.fetch_add(1);
#ifdef CONFIG_SDK_AUDIO
	bool valid = sample_rate > 0 && (inputs || outputs) &&
		     inputs <= SDK_AUDIO_MAX_CHANNELS &&
		     outputs <= SDK_AUDIO_MAX_CHANNELS && frames > 0;
#else
	bool valid = false;
#endif
	rate.store(sample_rate);
	input_count.store(inputs);
	output_count.store(outputs);
	block_size.store(frames);
	running.store(valid);
	playback_enabled.store(false);
#ifdef CONFIG_SDK_AUDIO
	current_output.frames = output_offset = 0;
#endif
	sequence.fetch_add(1, std::memory_order_release);
}
extern "C" void sdk_media_audio_stop(void)
{
	sequence.fetch_add(1);
	running.store(false);
	playback_enabled.store(false);
#ifdef CONFIG_SDK_AUDIO
	current_output.frames = output_offset = 0;
#endif
	sequence.fetch_add(1, std::memory_order_release);
}
extern "C" void sdk_media_audio_process(const float *const *input,
					unsigned inputs, float *const *output,
					unsigned outputs, unsigned frames,
					uint64_t timestamp)
{
	for (unsigned c = 0; c < outputs; ++c)
		if (output && output[c])
			std::fill_n(output[c], frames, 0.0f);
#ifdef CONFIG_SDK_AUDIO
	if (clear_playback.exchange(false)) {
		current_output.frames = output_offset = 0;
		sdk_audio_packet_t discarded{};
		for (unsigned i = 0; i < CONFIG_SDK_AUDIO_QUEUE_BLOCKS &&
				     playback.pop(discarded);
		     ++i) {
		}
	}
	sdk_audio_format_t f{};
	if (sdk_audio_get_format(&f) || !f.running ||
	    inputs != f.input_channels || outputs != f.output_channels)
		return;
	++callbacks;
	if (capture_enabled.load() && inputs) {
		for (unsigned offset = 0; offset < frames;
		     offset += SDK_AUDIO_BLOCK_FRAMES) {
			sdk_audio_packet_t packet{};
			packet.generation = f.generation;
			packet.sample_rate_hz = f.sample_rate_hz;
			packet.channels = inputs;
			packet.frames = std::min<unsigned>(
				frames - offset, SDK_AUDIO_BLOCK_FRAMES);
			packet.timestamp_us =
				timestamp +
				uint64_t(offset) * 1000000 / f.sample_rate_hz;
			for (unsigned i = 0; i < packet.frames; ++i)
				for (unsigned c = 0; c < inputs; ++c)
					packet.samples[i * inputs + c] =
						input && input[c] ?
							input[c][offset + i] :
							0.0f;
			if (!capture.push(packet))
				++input_drops;
		}
	}
	if (auto *binding = processor.load(std::memory_order_acquire);
	    binding && binding->process) {
		const sdk_audio_process_block_t b{ input,  output,
						   inputs, outputs,
						   frames, f.sample_rate_hz };
		binding->process(&b, binding->context);
	} else if (muted.load()) {
		current_output.frames = output_offset = 0;
		sdk_audio_packet_t discarded{};
		for (unsigned i = 0; i < CONFIG_SDK_AUDIO_QUEUE_BLOCKS &&
				     playback.pop(discarded);
		     ++i) {
		}
	} else if (playback_enabled.load()) {
		unsigned written = 0;
		for (unsigned tries = 0;
		     written < frames &&
		     tries < CONFIG_SDK_AUDIO_QUEUE_BLOCKS + 1;
		     ++tries) {
			if (output_offset >= current_output.frames ||
			    current_output.generation != f.generation) {
				if (!playback.pop(current_output))
					break;
				output_offset = 0;
				if (current_output.generation != f.generation) {
					current_output.frames = 0;
					continue;
				}
			}
			auto count =
				std::min(frames - written,
					 current_output.frames - output_offset);
			for (unsigned i = 0; i < count; ++i)
				for (unsigned c = 0; c < outputs; ++c)
					if (output && output[c])
						output[c][written + i] =
							current_output.samples
								[(output_offset +
								  i) * outputs +
								 c];
			output_offset += count;
			written += count;
		}
		underruns.fetch_add(frames - written);
	}
	// Non-finite or out-of-range user processor results never reach the device.
	for (unsigned c = 0; c < outputs; ++c)
		if (output && output[c])
			for (unsigned i = 0; i < frames; ++i)
				output[c][i] = std::isfinite(output[c][i]) ?
						       std::clamp(output[c][i],
								  -1.0f, 1.0f) :
						       0.0f;
#else
	(void)input;
	(void)inputs;
	(void)timestamp;
#endif
}
extern "C" void sdk_media_midi_devices(unsigned inputs, unsigned outputs,
				       unsigned enabled, int available)
{
#ifdef CONFIG_SDK_MIDI
	midi_inputs.store(inputs);
	midi_outputs.store(outputs);
	midi_enabled.store(enabled);
	midi_available.store(available != 0);
#else
	(void)inputs;
	(void)outputs;
	(void)enabled;
	(void)available;
#endif
}
extern "C" void sdk_media_midi_input(uint32_t port, const uint8_t *data,
				     unsigned size, uint64_t timestamp)
{
#ifdef CONFIG_SDK_MIDI
	sdk_midi_message_t message{};
	if (!data || !size || size > SDK_MIDI_MAX_BYTES) {
		++midi_input_drops;
		return;
	}
	message.timestamp_us = timestamp;
	message.port_id = port;
	message.size = size;
	std::memcpy(message.data, data, size);
	if (!midi_valid(message) || !midi_input.push(message))
		++midi_input_drops;
	else
		++received;
#else
	(void)port;
	(void)data;
	(void)size;
	(void)timestamp;
#endif
}
extern "C" int sdk_midi_read(sdk_midi_message_t *message)
{
	if (!message)
		return SDK_MEDIA_INVALID;
#ifndef CONFIG_SDK_MIDI
	return SDK_MEDIA_UNAVAILABLE;
#else
	return midi_input.pop(*message) ? 1 : 0;
#endif
}
extern "C" int sdk_midi_send(const sdk_midi_message_t *message)
{
#ifndef CONFIG_SDK_MIDI
	(void)message;
	return SDK_MEDIA_UNAVAILABLE;
#else
	if (!message || message->port_id != 0 || !midi_valid(*message))
		return SDK_MEDIA_INVALID;
	if (!midi_available.load())
		return SDK_MEDIA_UNAVAILABLE;
	if (!midi_output.push(*message)) {
		++midi_output_drops;
		return SDK_MEDIA_FULL;
	}
	return 0;
#endif
}
extern "C" int sdk_media_midi_output(sdk_midi_message_t *message)
{
#ifdef CONFIG_SDK_MIDI
	return message && midi_output.pop(*message) ? 1 : 0;
#else
	(void)message;
	return SDK_MEDIA_UNAVAILABLE;
#endif
}
extern "C" void sdk_media_midi_delivered(int success)
{
	if (success)
		++sent;
	else
		++midi_output_drops;
}
extern "C" sdk_midi_stats_t sdk_midi_stats(void)
{
	return { midi_inputs.load(),	  midi_outputs.load(),
		 midi_enabled.load(),	  midi_available.load() ? 1 : 0,
		 received.load(),	  sent.load(),
		 midi_input_drops.load(), midi_output_drops.load() };
}
