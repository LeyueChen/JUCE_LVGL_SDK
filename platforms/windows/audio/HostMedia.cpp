#include "HostMedia.h"
#include "sdk_config.h"
namespace
{
constexpr bool audioSupported =
#ifdef CONFIG_SDK_AUDIO
	true;
#else
	false;
#endif
constexpr bool midiSupported =
#ifdef CONFIG_SDK_MIDI
	true;
#else
	false;
#endif
}
class HostMedia::DevicePanel final : public juce::Component,
				     private juce::Timer {
    public:
	explicit DevicePanel(HostMedia &service)
		: host(service)
		, selector(service.manager, 0,
			   audioSupported ? SDK_AUDIO_MAX_CHANNELS : 0, 0,
			   audioSupported ? SDK_AUDIO_MAX_CHANNELS : 0,
			   midiSupported, midiSupported, false, false)
	{
		addAndMakeVisible(title);
		title.setText("Audio / MIDI devices",
			      juce::dontSendNotification);
		title.setFont(juce::FontOptions(22.0f, juce::Font::bold));
		addAndMakeVisible(state);
		state.setJustificationType(juce::Justification::topLeft);
		addAndMakeVisible(toggle);
		toggle.onClick = [&] {
			host.toggleAudio();
			update();
		};
		toggle.setEnabled(audioSupported);
		addAndMakeVisible(selector);
		setSize(700, 740);
		update();
		startTimerHz(4);
	}
	void resized() override
	{
		auto r = getLocalBounds().reduced(18);
		title.setBounds(r.removeFromTop(32));
		r.removeFromTop(8);
		toggle.setBounds(r.removeFromTop(30).removeFromLeft(160));
		r.removeFromTop(8);
		state.setBounds(r.removeFromTop(72));
		r.removeFromTop(8);
		selector.setBounds(r);
	}
	void paint(juce::Graphics &g) override
	{
		g.fillAll(juce::Colour(0xff20242b));
	}

    private:
	void update()
	{
		state.setText(host.summary(), juce::dontSendNotification);
		sdk_audio_format_t f{};
		sdk_audio_get_format(&f);
		toggle.setButtonText(f.running ? "Stop audio" : "Start audio");
	}
	void timerCallback() override
	{
		update();
	}
	HostMedia &host;
	juce::Label title, state;
	juce::TextButton toggle;
	juce::AudioDeviceSelectorComponent selector;
};
class HostMedia::DeviceWindow final : public juce::DocumentWindow {
    public:
	explicit DeviceWindow(HostMedia &host)
		: DocumentWindow("Audio / MIDI settings",
				 juce::Colour(0xff20242b), closeButton)
	{
		setUsingNativeTitleBar(true);
		setContentOwned(new DevicePanel(host), true);
		setResizable(true, false);
		setResizeLimits(640, 650, 1200, 1100);
		centreWithSize(700, 740);
	}
	void closeButtonPressed() override
	{
		setVisible(false);
	}
};
HostMedia::HostMedia() = default;
HostMedia::~HostMedia()
{
	stop();
}
void HostMedia::start(const juce::File &directory, bool forceNoAudio)
{
	jassert(!started);
	stateFile = directory.getChildFile("media-devices.xml");
	auto xml = juce::parseXML(stateFile);
	std::unique_ptr<juce::XmlElement> state;
	if (xml && xml->hasTagName("SDK_MEDIA_STATE") &&
	    xml->getIntAttribute("schema") == 1) {
		restored = true;
		audioEnabled = xml->getBoolAttribute("audioEnabled", true);
		if (auto *child = xml->getChildByName("DEVICESETUP"))
			state = std::make_unique<juce::XmlElement>(*child);
	} else {
		audioEnabled = true;
		if (stateFile.existsAsFile())
			lastError =
				"Invalid saved device settings; using defaults.";
	}
	audioEnabled = audioEnabled && audioSupported && !forceNoAudio;
	if (state)
		rememberedAudio = std::make_unique<juce::XmlElement>(*state);
	if (!audioEnabled && state) {
		state->setAttribute("audioDeviceInChans", "0");
		state->setAttribute("audioDeviceOutChans", "0");
	}
	if (!midiSupported && state) {
		state->deleteAllChildElementsWithTagName("MIDIINPUT");
		state->removeAttribute("defaultMidiOutput");
		state->removeAttribute("defaultMidiOutputDevice");
	}
	auto error =
		manager.initialise(0, audioEnabled ? 2 : 0, state.get(), false);
	if (error.isNotEmpty()) {
		lastError = error;
		manager.closeAudioDevice();
	}
	if (audioSupported)
		manager.addAudioCallback(this);
	if (midiSupported)
		manager.addMidiInputDeviceCallback({}, this);
	manager.addChangeListener(this);
	started = true;
	refreshMidi();
	save();
	startTimerHz(100);
}
void HostMedia::stop()
{
	if (!started)
		return;
	stopTimer();
	settingsWindow.reset();
	save();
	manager.removeChangeListener(this);
	manager.removeMidiInputDeviceCallback({}, this);
	manager.removeAudioCallback(this);
	manager.closeAudioDevice();
	for (const auto &d : juce::MidiInput::getAvailableDevices())
		manager.setMidiInputDeviceEnabled(d.identifier, false);
	manager.setDefaultMidiOutputDevice({});
	sdk_media_midi_devices(0, 0, 0, 0);
	started = false;
}
void HostMedia::showSettings()
{
	if (!settingsWindow)
		settingsWindow = std::make_unique<DeviceWindow>(*this);
	settingsWindow->setVisible(true);
	settingsWindow->toFront(true);
}
void HostMedia::toggleAudio()
{
	sdk_audio_format_t f{};
	sdk_audio_get_format(&f);
	if (f.running) {
		rememberedAudio = deviceState();
		audioEnabled = false;
		manager.closeAudioDevice();
	} else if (audioSupported) {
		audioEnabled = true;
		auto error =
			manager.initialise(0, 2, rememberedAudio.get(), false);
		lastError = error;
	}
	save();
}
std::unique_ptr<juce::XmlElement> HostMedia::deviceState()
{
	auto state = manager.createStateXml();
	if (state)
		return state;
	// JUCE omits XML until the user explicitly changes a device. Snapshot the
	// initial default too, so the first launch has a reproducible configuration.
	state = std::make_unique<juce::XmlElement>("DEVICESETUP");
	auto setup = manager.getAudioDeviceSetup();
	state->setAttribute("deviceType", manager.getCurrentAudioDeviceType());
	state->setAttribute("audioInputDeviceName", setup.inputDeviceName);
	state->setAttribute("audioOutputDeviceName", setup.outputDeviceName);
	state->setAttribute("audioDeviceInChans",
			    setup.inputChannels.toString(2));
	state->setAttribute("audioDeviceOutChans",
			    setup.outputChannels.toString(2));
	if (auto *device = manager.getCurrentAudioDevice()) {
		state->setAttribute("audioDeviceRate",
				    device->getCurrentSampleRate());
		state->setAttribute("audioDeviceBufferSize",
				    device->getCurrentBufferSizeSamples());
	}
	for (const auto &d : juce::MidiInput::getAvailableDevices())
		if (manager.isMidiInputDeviceEnabled(d.identifier)) {
			auto *child = state->createNewChildElement("MIDIINPUT");
			child->setAttribute("name", d.name);
			child->setAttribute("identifier", d.identifier);
		}
	if (auto *output = manager.getDefaultMidiOutput()) {
		state->setAttribute("defaultMidiOutput", output->getName());
		state->setAttribute("defaultMidiOutputDevice",
				    manager.getDefaultMidiOutputIdentifier());
	}
	return state;
}
bool HostMedia::saveSettingsSnapshot(const juce::File &file)
{
	if (!settingsWindow)
		return false;
	auto *panel = settingsWindow->getContentComponent();
	auto image = panel->createComponentSnapshot(panel->getLocalBounds());
	auto stream = file.createOutputStream();
	return stream && stream->openedOk() &&
	       juce::PNGImageFormat().writeImageToStream(image, *stream);
}
bool HostMedia::save()
{
	juce::XmlElement xml("SDK_MEDIA_STATE");
	xml.setAttribute("schema", 1);
	xml.setAttribute("audioEnabled", audioEnabled);
	auto state = deviceState();
	if (state) {
		// Keep the selected audio channels across an explicit stop, but always
		// save MIDI changes made while audio is stopped.
		if (!audioEnabled && rememberedAudio) {
			for (const char *key :
			     { "audioInputDeviceName", "audioOutputDeviceName",
			       "deviceType", "audioDeviceRate",
			       "audioDeviceBufferSize", "audioDeviceInChans",
			       "audioDeviceOutChans" })
				if (rememberedAudio->hasAttribute(key))
					state->setAttribute(
						key,
						rememberedAudio
							->getStringAttribute(
								key));
		}
		xml.addChildElement(state.release());
	}
	auto text = xml.toString();
	if (text == lastSavedState && stateFile.existsAsFile())
		return true;
	if (stateFile.getParentDirectory().createDirectory().failed() ||
	    !stateFile.replaceWithText(text)) {
		saveError = "Cannot save " + stateFile.getFullPathName();
		return false;
	}
	saveError.clear();
	lastSavedState = text;
	return true;
}
void HostMedia::changeListenerCallback(juce::ChangeBroadcaster *)
{
	sdk_audio_format_t f{};
	sdk_audio_get_format(&f);
	if (f.running) {
		audioEnabled = true;
		lastError.clear();
		rememberedAudio = deviceState();
	}
	refreshMidi();
	save();
}
void HostMedia::refreshMidi()
{
	if (!midiSupported)
		return;
	auto inputs = juce::MidiInput::getAvailableDevices();
	auto outputs = juce::MidiOutput::getAvailableDevices();
	unsigned enabled = 0;
	for (const auto &d : inputs)
		if (manager.isMidiInputDeviceEnabled(d.identifier))
			++enabled;
	auto selected = manager.getDefaultMidiOutputIdentifier();
	bool available =
		manager.getDefaultMidiOutput() &&
		std::any_of(outputs.begin(), outputs.end(), [&](const auto &d) {
			return d.identifier == selected;
		});
	sdk_media_midi_devices(unsigned(inputs.size()),
			       unsigned(outputs.size()), enabled, available);
}
void HostMedia::timerCallback()
{
	Error error;
	while (errors.pop(error))
		lastError = juce::String::fromUTF8(error.text);
	if (++refreshTicks >= 100) {
		refreshTicks = 0;
		refreshMidi();
	}
	sdk_midi_message_t message{};
	for (int i = 0; i < CONFIG_SDK_MIDI_QUEUE_MESSAGES &&
			sdk_media_midi_output(&message) == 1;
	     ++i) {
		auto stats = sdk_midi_stats();
		auto *output = manager.getDefaultMidiOutput();
		if (output && stats.output_available) {
			output->sendMessageNow(juce::MidiMessage(
				message.data, int(message.size), 0));
			sdk_media_midi_delivered(1);
		} else
			sdk_media_midi_delivered(0);
	}
}
void HostMedia::audioDeviceIOCallbackWithContext(
	const float *const *inputs, int nInputs, float *const *outputs,
	int nOutputs, int frames, const juce::AudioIODeviceCallbackContext &)
{
	sdk_media_audio_process(
		inputs, unsigned(nInputs), outputs, unsigned(nOutputs),
		unsigned(frames),
		uint64_t(juce::Time::getMillisecondCounterHiRes() * 1000.0));
}
void HostMedia::audioDeviceAboutToStart(juce::AudioIODevice *device)
{
	sdk_media_audio_start(unsigned(device->getCurrentSampleRate()),
			      unsigned(device->getActiveInputChannels()
					       .countNumberOfSetBits()),
			      unsigned(device->getActiveOutputChannels()
					       .countNumberOfSetBits()),
			      unsigned(device->getCurrentBufferSizeSamples()));
}
void HostMedia::audioDeviceStopped()
{
	sdk_media_audio_stop();
}
void HostMedia::audioDeviceError(const juce::String &message)
{
	Error error;
	message.copyToUTF8(error.text, sizeof(error.text));
	errors.push(error);
}
void HostMedia::handleIncomingMidiMessage(juce::MidiInput *source,
					  const juce::MidiMessage &message)
{
	sdk_media_midi_input(
		source ? uint32_t(source->getIdentifier().hashCode()) : 0,
		message.getRawData(), unsigned(message.getRawDataSize()),
		uint64_t(juce::Time::getMillisecondCounterHiRes() * 1000.0));
}
juce::String HostMedia::summary() const
{
	sdk_audio_format_t f{};
	sdk_audio_get_format(&f);
	auto m = sdk_midi_stats();
	auto text = f.running ?
			    juce::String(f.sample_rate_hz) + " Hz | " +
				    juce::String(f.device_block_frames) +
				    " samples | " +
				    juce::String(f.input_channels) + " in / " +
				    juce::String(f.output_channels) + " out" :
			    "Audio stopped / no device";
	text += "\nMIDI: " + juce::String(m.enabled_inputs) + "/" +
		juce::String(m.input_devices) + " inputs | " +
		juce::String(m.output_devices) + " outputs";
	if (lastError.isNotEmpty())
		text += "\n" + lastError;
	if (saveError.isNotEmpty())
		text += "\n" + saveError;
	return text;
}
juce::var HostMedia::report()
{
	juce::DynamicObject::Ptr r = new juce::DynamicObject;
	sdk_audio_format_t f{};
	sdk_audio_get_format(&f);
	auto a = sdk_audio_stats();
	auto m = sdk_midi_stats();
	r->setProperty("audio_supported", audioSupported);
	r->setProperty("midi_supported", midiSupported);
	r->setProperty("audio_running", f.running != 0);
	r->setProperty("sample_rate_hz", int(f.sample_rate_hz));
	r->setProperty("buffer_frames", int(f.device_block_frames));
	r->setProperty("input_channels", int(f.input_channels));
	r->setProperty("output_channels", int(f.output_channels));
	r->setProperty("audio_callbacks", juce::int64(a.callbacks));
	r->setProperty("input_dropped_blocks",
		       juce::int64(a.input_dropped_blocks));
	r->setProperty("output_underrun_frames",
		       juce::int64(a.output_underrun_frames));
	r->setProperty("midi_inputs", int(m.input_devices));
	r->setProperty("midi_outputs", int(m.output_devices));
	r->setProperty("midi_enabled_inputs", int(m.enabled_inputs));
	r->setProperty("midi_output_available", m.output_available != 0);
	r->setProperty("midi_received", juce::int64(m.received));
	r->setProperty("midi_sent", juce::int64(m.sent));
	r->setProperty("midi_input_dropped", juce::int64(m.input_dropped));
	r->setProperty("midi_output_dropped", juce::int64(m.output_dropped));
	r->setProperty("restored", restored);
	r->setProperty("state_file", stateFile.getFullPathName());
	r->setProperty("device_error", lastError);
	r->setProperty("save_error", saveError);
	juce::Array<juce::var> types;
	for (auto *type : manager.getAvailableDeviceTypes()) {
		juce::DynamicObject::Ptr info = new juce::DynamicObject;
		info->setProperty("type", type->getTypeName());
		juce::Array<juce::var> ins, outs;
		for (auto name : type->getDeviceNames(true))
			ins.add(name);
		for (auto name : type->getDeviceNames(false))
			outs.add(name);
		info->setProperty("inputs", ins);
		info->setProperty("outputs", outs);
		types.add(juce::var(info.get()));
	}
	r->setProperty("audio_device_types", types);
	return juce::var(r.get());
}
#ifdef SDK_DIAGNOSTICS
bool HostMedia::selfTest(const juce::File &directory)
{
	auto testDirectory =
		directory.getNonexistentChildFile("devices", {}, false);
	bool ok = true;
	{
		HostMedia host;
		host.start(testDirectory, true);
#ifdef CONFIG_SDK_AUDIO
		// Exercise the actual JUCE adapter callback with deterministic buffers;
		// physical device streaming is validated separately by a normal run.
		sdk_media_audio_start(48000, 0, 2, 16);
		float left[16], right[16];
		std::fill_n(left, 16, 1.0f);
		std::fill_n(right, 16, 1.0f);
		float *output[]{ left, right };
		auto before = sdk_audio_stats().callbacks;
		host.audioDeviceIOCallbackWithContext(nullptr, 0, output, 2, 16,
						      {});
		ok = ok && sdk_audio_stats().callbacks == before + 1 &&
		     left[0] == 0 && right[15] == 0;
		host.audioDeviceStopped();
#endif
#ifdef CONFIG_SDK_MIDI
		auto message =
			juce::MidiMessage::noteOn(1, 60, juce::uint8(100));
		host.handleIncomingMidiMessage(nullptr, message);
		sdk_midi_message_t event{};
		ok = ok && sdk_midi_read(&event) == 1 && event.size == 3 &&
		     event.data[0] == 0x90 && event.data[2] == 100;
#endif
		DevicePanel panel(host);
		panel.setSize(700, 740);
		auto image =
			panel.createComponentSnapshot(panel.getLocalBounds());
		auto stream = testDirectory.getChildFile("settings.png")
				      .createOutputStream();
		ok = ok && stream && stream->openedOk() &&
		     juce::PNGImageFormat().writeImageToStream(image, *stream);
		ok = ok && host.save();
		ok = ok && testDirectory.getChildFile("media-devices.xml")
				   .existsAsFile();
		auto saved = juce::parseXML(
			testDirectory.getChildFile("media-devices.xml"));
		ok = ok && saved && saved->getChildByName("DEVICESETUP");
		testDirectory.getChildFile("enumeration.json")
			.replaceWithText(juce::JSON::toString(host.report()));
	}
	{
		HostMedia host;
		host.start(testDirectory, true);
		auto r = host.report();
		ok = ok && bool(r["restored"]) && !bool(r["audio_running"]);
		ok = ok && r["save_error"].toString().isEmpty();
	}
	{
		HostMedia other;
		auto otherDirectory = directory.getNonexistentChildFile(
			"other-project", {}, false);
		other.start(otherDirectory, true);
		ok = ok && !bool(other.report()["restored"]);
		ok = ok && otherDirectory.getChildFile("media-devices.xml")
				   .existsAsFile();
	}
	juce::DynamicObject::Ptr result = new juce::DynamicObject;
	result->setProperty("passed", ok);
	result->setProperty("physical_midi_loopback_tested", false);
	testDirectory.getChildFile("self-test.json")
		.replaceWithText(juce::JSON::toString(juce::var(result.get())));
	return ok;
}

bool HostMedia::inputTest(const juce::File &directory)
{
	if (!audioSupported)
		return false;
	// Explicit hardware diagnostic. Count transient PCM frames, never save audio.
	HostMedia host;
	host.start(directory, true);
	host.audioEnabled = audioSupported;
	sdk_audio_capture_enable(1);
	auto error = host.manager.initialise(1, 0, nullptr, false);
	if (error.isNotEmpty())
		host.lastError = error;
	uint64_t capturedFrames = 0;
	const auto start = juce::Time::getMillisecondCounterHiRes();
	while (error.isEmpty() &&
	       juce::Time::getMillisecondCounterHiRes() - start < 2000.0) {
		sdk_audio_packet_t packet{};
		for (unsigned i = 0; i < CONFIG_SDK_AUDIO_QUEUE_BLOCKS &&
				     sdk_audio_read(&packet) == 1;
		     ++i)
			capturedFrames += packet.frames;
		juce::Thread::sleep(2);
	}
	sdk_audio_capture_enable(0);
	auto result = host.report();
	const bool ok = error.isEmpty() && capturedFrames > 0;
	result.getDynamicObject()->setProperty("captured_frames",
					       juce::int64(capturedFrames));
	result.getDynamicObject()->setProperty("passed", ok);
	result.getDynamicObject()->setProperty("audio_samples_saved", false);
	directory.getChildFile("input-test.json")
		.replaceWithText(juce::JSON::toString(result));
	return ok;
}
#endif
