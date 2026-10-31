#include "backend.h"
#include "bounded_queue.h"
#include "sdk_config.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>
extern "C" void test_c_audio_process(const sdk_audio_process_block_t *, void *);
static void require(bool value, const char *message)
{
	if (!value) {
		std::fprintf(stderr, "FAIL %s\n", message);
		std::abort();
	}
}
static void test_queue()
{
	sdk_media::Queue<unsigned, 7> queue;
	constexpr unsigned producers = 4, count = 1000;
	std::vector<unsigned> received(producers * count);
	auto deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(10);
	std::vector<std::thread> threads;
	for (unsigned p = 0; p < producers; ++p)
		threads.emplace_back([&, p] {
			for (unsigned i = 0; i < count; ++i)
				while (!queue.push(p * count + i)) {
					require(std::chrono::steady_clock::now() <
							deadline,
						"queue producer makes progress");
					std::this_thread::yield();
				}
		});
	for (unsigned i = 0; i < producers * count;) {
		unsigned value;
		if (queue.pop(value)) {
			require(value < received.size(),
				"queue value in range");
			++received[value];
			++i;
		} else {
			require(std::chrono::steady_clock::now() < deadline,
				"queue consumer makes progress");
			std::this_thread::yield();
		}
	}
	for (auto &thread : threads)
		thread.join();
	for (auto seen : received)
		require(seen == 1,
			"MPMC has no corruption, duplicates or loss");
	std::puts("PASS bounded concurrent queue, non-power-of-two capacity");
}
static void bad_processor(const sdk_audio_process_block_t *b, void *)
{
	b->output[0][0] = std::numeric_limits<float>::infinity();
	b->output[0][1] = 5.0f;
}
static void test_audio()
{
	require(sdk_audio_get_format(nullptr) == SDK_MEDIA_INVALID,
		"reject null format");
	sdk_media_audio_start(48000, 2, 2, 600);
	sdk_audio_format_t f{};
	require(sdk_audio_get_format(&f) == 0, "read stream format");
#ifdef CONFIG_SDK_AUDIO
	require(f.running && f.sample_rate_hz == 48000 &&
			f.output_channels == 2,
		"audio format exposed");
	std::array<float, 600> left{}, right{}, out_l{}, out_r{};
	left.fill(0.25f);
	right.fill(-0.5f);
	out_l.fill(1);
	out_r.fill(1);
	const float *input[]{ left.data(), right.data() };
	float *output[]{ out_l.data(), out_r.data() };
	sdk_audio_capture_enable(1);
	sdk_media_audio_process(input, 2, output, 2, 600, 1000000);
	require(out_l[0] == 0 && out_r[599] == 0, "unbound output is silence");
	sdk_audio_packet_t p{};
	unsigned total = 0;
	while (sdk_audio_read(&p) == 1) {
		require(p.frames <= 256 && p.generation == f.generation &&
				p.channels == 2,
			"input split and format tagged");
		require(p.timestamp_us ==
				1000000 + uint64_t(total) * 1000000 / 48000,
			"split timestamps preserved");
		require(p.samples[0] == 0.25f && p.samples[1] == -0.5f,
			"input PCM interleaved correctly");
		total += p.frames;
	}
	// At least two slots are configurable. Account for deliberate full-queue loss.
	require(total == std::min(600, CONFIG_SDK_AUDIO_QUEUE_BLOCKS * 256),
		"capture is bounded");
	sdk_audio_capture_enable(0);
	p = {};
	p.frames = 4;
	p.channels = 2;
	p.generation = f.generation;
	p.sample_rate_hz = 48000;
	for (unsigned i = 0; i < 8; ++i)
		p.samples[i] = float(i) / 10;
	require(sdk_audio_write(&p) == 0, "CPU0 queued playback");
	sdk_media_audio_process(input, 2, output, 2, 3, 0);
	require(out_l[0] == 0 && out_r[2] == 0.5f, "deinterleaved playback");
	sdk_media_audio_process(input, 2, output, 2, 3, 0);
	require(out_l[0] == 0.6f && out_r[0] == 0.7f && out_l[1] == 0,
		"packet offset retained, underrun silenced");
	require(sdk_audio_stats().output_underrun_frames == 2,
		"underrun reported");
	static float gain = 0.5f;
	static const sdk_audio_processor_t binding{ test_c_audio_process,
						    &gain };
	sdk_audio_set_processor(&binding);
	require(sdk_audio_write(&p) == SDK_MEDIA_BUSY, "processor owns output");
	sdk_media_audio_process(input, 2, output, 2, 8, 0);
	require(out_l[0] == 0.125f && out_r[7] == -0.25f,
		"C processing callback runs correctly");
	sdk_media_audio_process(nullptr, 2, output, 2, 8, 0);
	require(out_l[0] == 0 && out_r[7] == 0,
		"C processor tolerates absent input channels");
	sdk_audio_set_processor(nullptr);
	require(sdk_audio_write(&p) == 0, "queue data before device change");
	sdk_media_audio_start(44100, 2, 2, 128);
	require(sdk_audio_write(&p) == SDK_MEDIA_INVALID,
		"stale generation rejected");
	sdk_audio_get_format(&f);
	p.generation = f.generation;
	p.sample_rate_hz = f.sample_rate_hz;
	require(sdk_audio_write(&p) == 0, "new format accepted");
	sdk_media_audio_process(input, 2, output, 2, 4, 0);
	require(out_r[0] == 0.1f && out_r[3] == 0.7f,
		"stale queued audio discarded");
	unsigned accepted = 0;
	while (sdk_audio_write(&p) == 0)
		++accepted;
	require(accepted == CONFIG_SDK_AUDIO_QUEUE_BLOCKS,
		"playback full rejects new packet");
	p.frames = SDK_AUDIO_BLOCK_FRAMES + 1;
	require(sdk_audio_write(&p) == SDK_MEDIA_INVALID,
		"oversize PCM rejected");
	sdk_media_audio_start(48000, 0, 1, 8);
	static const sdk_audio_processor_t bad{ bad_processor, nullptr };
	sdk_audio_set_processor(&bad);
	sdk_media_audio_process(nullptr, 0, output, 1, 8, 0);
	require(out_l[0] == 0 && out_l[1] == 1,
		"nonfinite/clipped processor outputs bounded");
	sdk_audio_set_processor(nullptr);
	sdk_media_audio_stop();
	sdk_audio_get_format(&f);
	require(!f.running, "device stop published");
	std::puts(
		"PASS PCM capture/playback, formats, C callback, bounds and device restart");
#else
	require(!f.running, "audio disabled configuration remains unavailable");
	std::puts("PASS disabled audio provider");
#endif
}
static void test_midi()
{
	sdk_midi_message_t m{};
	m.size = 3;
	m.data[0] = 0x90;
	m.data[1] = 60;
	m.data[2] = 100;
	require(sdk_midi_send(&m) == SDK_MEDIA_UNAVAILABLE,
		"MIDI missing output reported");
#ifdef CONFIG_SDK_MIDI
	sdk_media_midi_devices(2, 1, 2, 1);
	sdk_media_midi_input(42, m.data, 3, 123456);
	sdk_midi_message_t incoming{};
	require(sdk_midi_read(&incoming) == 1 && incoming.port_id == 42 &&
			incoming.timestamp_us == 123456 &&
			incoming.data[2] == 100,
		"MIDI receive preserves source, time, bytes");
	require(sdk_midi_send(&m) == 0, "MIDI output queues");
	require(sdk_media_midi_output(&incoming) == 1 &&
			incoming.data[0] == 0x90,
		"MIDI output adapter drains");
	sdk_media_midi_delivered(1);
	require(sdk_midi_stats().sent == 1, "MIDI delivery counted");
	m.size = 2;
	require(sdk_midi_send(&m) == SDK_MEDIA_INVALID,
		"truncated MIDI rejected");
	m.size = SDK_MIDI_MAX_BYTES + 1;
	require(sdk_midi_send(&m) == SDK_MEDIA_INVALID,
		"oversized MIDI rejected");
	m.size = 4;
	m.data[0] = 0xf0;
	m.data[1] = 0x7d;
	m.data[2] = 1;
	m.data[3] = 0xf7;
	require(sdk_midi_send(&m) == 0, "bounded SysEx accepted");
	require(sdk_media_midi_output(&incoming) == 1 && incoming.size == 4,
		"SysEx retained");
	sdk_media_midi_delivered(0);
	require(sdk_midi_stats().output_dropped == 1,
		"disconnect delivery failure counted");
	for (int i = 0; i < CONFIG_SDK_MIDI_QUEUE_MESSAGES + 1; ++i)
		sdk_media_midi_input(1, m.data, m.size, 0);
	require(sdk_midi_stats().input_dropped > 0, "MIDI overflow bounded");
	while (sdk_midi_read(&incoming) == 1) {
	}
	sdk_media_midi_devices(0, 0, 0, 0);
	require(sdk_midi_send(&m) == SDK_MEDIA_UNAVAILABLE,
		"MIDI disconnect exposed");
	std::puts(
		"PASS MIDI receive/send, SysEx, bounds, overflow and disconnect");
#else
	require(sdk_midi_read(&m) == SDK_MEDIA_UNAVAILABLE,
		"MIDI disabled provider");
#endif
}
int main()
{
	test_queue();
	test_audio();
	test_midi();
	return 0;
}
