#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "backend.h"
#include "queue.h"
#include <memory>
class HostMedia final : private juce::AudioIODeviceCallback,
			private juce::MidiInputCallback,
			private juce::ChangeListener,
			private juce::Timer {
    public:
	HostMedia();
	~HostMedia() override;
	void start(const juce::File &dataDirectory, bool forceNoAudio = false);
	void stop();
	void showSettings();
	void toggleAudio();
	juce::String summary() const;
	juce::var report();
	bool save();
	bool saveSettingsSnapshot(const juce::File &file);
	static bool selfTest(const juce::File &directory);
	static bool inputTest(const juce::File &directory);

    private:
	class DeviceWindow;
	class DevicePanel;
	struct Error {
		char text[512]{};
	};
	void timerCallback() override;
	void changeListenerCallback(juce::ChangeBroadcaster *) override;
	void refreshMidi();
	std::unique_ptr<juce::XmlElement> deviceState();
	void audioDeviceIOCallbackWithContext(
		const float *const *, int, float *const *, int, int,
		const juce::AudioIODeviceCallbackContext &) override;
	void audioDeviceAboutToStart(juce::AudioIODevice *) override;
	void audioDeviceStopped() override;
	void audioDeviceError(const juce::String &) override;
	void handleIncomingMidiMessage(juce::MidiInput *,
				       const juce::MidiMessage &) override;
	juce::AudioDeviceManager manager;
	std::unique_ptr<DeviceWindow> settingsWindow;
	juce::File stateFile;
	juce::String lastError, saveError, lastSavedState;
	std::unique_ptr<juce::XmlElement> rememberedAudio;
	sdk_media::Queue<Error, 8> errors;
	bool started = false, restored = false, audioEnabled = false;
	unsigned refreshTicks = 0;
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HostMedia)
};
