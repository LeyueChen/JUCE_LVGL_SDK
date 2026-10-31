#include <juce_gui_basics/juce_gui_basics.h>
#include "sdk/host_bridge.h"
#include "sdk_config.h"
#include "sdk_project.h"
#include "HostMedia.h"
#include "DisplayCanvas.h"
#include "KeyboardInput.h"
#ifdef SDK_DIAGNOSTICS
#include "viewport.h"
extern "C" int sdk_rtos_self_test_passed(void);
#endif
#include <vector>
#include <cstdio>

class SimulatorView final : public DisplayCanvas, private juce::Timer {
    public:
	explicit SimulatorView(int seconds, HostMedia &service, bool probe)
		: DisplayCanvas(CONFIG_SDK_SCREEN_WIDTH,
				CONFIG_SDK_SCREEN_HEIGHT,
				[](const DisplayCanvas::Input &input) {
					if (input.kind ==
					    DisplayCanvas::InputKind::cancel)
						sdk_host_cancel_pointer();
					else
						sdk_host_set_pointer(
							input.x, input.y,
							input.pressed ? 1 : 0);
				})
		, smokeSeconds(seconds)
		, media(service)
		, viewportProbe(probe)
		, pixels(CONFIG_SDK_SCREEN_WIDTH * CONFIG_SDK_SCREEN_HEIGHT *
			 4ull)
	{
		setUserInputEnabled(seconds == 0);
		start = juce::Time::getMillisecondCounterHiRes();
		startTimerHz(60);
	}
	std::function<void(int, int)> resizeHost;
	std::function<bool()> maximizeHost;
	void finish()
	{
		stopTimer();
		cancelPointer();
		if (smokeSeconds)
			media.saveSettingsSnapshot(
				juce::File::getCurrentWorkingDirectory()
					.getChildFile("device-settings.png"));
		auto mediaReport = media.report();
		media.stop();
		sdk_runtime_request_stop();
		const bool stopped = sdk_runtime_join(5000) == 0;
		sdk_host_storage_stop();
		auto stats = sdk_host_stats();
		juce::DynamicObject::Ptr report = new juce::DynamicObject;
		report->setProperty("primary_entries",
				    int(stats.primary_entries));
		report->setProperty("secondary_entries",
				    int(stats.secondary_entries));
		report->setProperty("bound_threads", int(stats.bound_threads));
		report->setProperty("affinity_failures",
				    int(stats.affinity_failures));
		report->setProperty("host_group", int(stats.group));
		report->setProperty("host_processor", int(stats.processor));
		report->setProperty("frames", juce::int64(stats.frames));
		report->setProperty("heartbeats",
				    juce::int64(stats.heartbeats));
		report->setProperty("scheduler_stopped", stopped);
		report->setProperty("media", mediaReport);
		report->setProperty("ui_events", int(stats.ui_events));
		report->setProperty("pointer_pressed_reads",
				    juce::int64(stats.pointer_pressed_reads));
		report->setProperty("pointer_cancelled_reads",
				    juce::int64(stats.pointer_cancelled_reads));
		const auto fitted = placement();
		report->setProperty("viewport_width", getWidth());
		report->setProperty("viewport_height", getHeight());
		report->setProperty("viewport_scale", fitted.scale);
		report->setProperty("viewport_left", fitted.bounds.getX());
		report->setProperty("viewport_top", fitted.bounds.getY());
		if (viewportProbe) {
			const bool passed = probeStage == 8 && probeNoClicks &&
					    probeMaximized &&
					    stats.pointer_cancelled_reads >= 3;
			report->setProperty("viewport_probe_passed", passed);
			report->setProperty("viewport_probe_maximized",
					    probeMaximized);
		}
		report->setProperty("storage_completed",
				    int(stats.storage_completed));
		report->setProperty("storage_failed",
				    int(stats.storage_failed));
		bool healthy = stopped && stats.primary_entries == 1 &&
			       stats.secondary_entries == 0 &&
			       stats.affinity_failures == 0 &&
			       stats.bound_threads >= 8 && stats.frames > 0 &&
			       stats.heartbeats > 0;
#ifdef SDK_DIAGNOSTICS
		report->setProperty("rtos_contracts_passed",
				    sdk_rtos_self_test_passed() != 0);
		healthy = healthy && sdk_rtos_self_test_passed();
#endif
		unsigned expectedStorage = 0;
#ifdef CONFIG_SDK_FLASHDB
		++expectedStorage;
#endif
#ifdef CONFIG_SDK_LITTLEFS
		++expectedStorage;
#endif
		if (smokeSeconds)
			healthy = healthy && stats.ui_events == 1 &&
				  stats.storage_failed == 0 &&
				  stats.storage_completed >= expectedStorage;
		if (viewportProbe)
			healthy = healthy && probeStage == 8 && probeNoClicks &&
				  probeMaximized &&
				  stats.pointer_cancelled_reads >= 3;
		report->setProperty("healthy", healthy);
		auto directory = juce::File::getCurrentWorkingDirectory();
		directory.getChildFile("runtime-report.json")
			.replaceWithText(
				juce::JSON::toString(juce::var(report.get())));
		auto snapshot = directory.getChildFile("snapshot.png")
					.createOutputStream();
		if (snapshot && snapshot->openedOk())
			juce::PNGImageFormat().writeImageToStream(frameImage(),
								  *snapshot);
		saveViewport("viewport.png");
		if (smokeSeconds && !healthy)
			juce::JUCEApplication::getInstance()
				->setApplicationReturnValue(2);
	}

    private:
	juce::Point<float> buttonPoint() const
	{
		return placement().toView(
			{ 144.5f, CONFIG_SDK_SCREEN_HEIGHT / 2.0f + 22.5f });
	}
	void saveViewport(const juce::String &name)
	{
		if (getWidth() <= 0 || getHeight() <= 0)
			return;
		auto image = createComponentSnapshot(getLocalBounds());
		if (auto stream = juce::File::getCurrentWorkingDirectory()
					  .getChildFile(name)
					  .createOutputStream())
			juce::PNGImageFormat().writeImageToStream(image,
								  *stream);
	}
	void probePress()
	{
		auto s = sdk_host_stats();
		probePressReads = s.pointer_pressed_reads;
		probeFrames = s.frames;
		probeCancelReads = s.pointer_cancelled_reads;
		pointerDown(buttonPoint());
		++probeStage;
	}
	void probeTick(double elapsed)
	{
		auto s = sdk_host_stats();
		const bool pressed = s.pointer_pressed_reads >
					     probePressReads &&
				     s.frames > probeFrames;
		const bool cancelled = s.pointer_cancelled_reads >
				       probeCancelReads;
		if (probeStage == 0 && elapsed >= 500 && s.frames > 0) {
			saveViewport("viewport-wide.png");
			pointerDown({ 1, 1 });
			pointerDrag(buttonPoint());
			pointerUp(buttonPoint());
			probePress();
		} else if (probeStage == 1 && pressed) {
			pointerDrag({ 1, 1 });
			pointerDrag(buttonPoint());
			pointerUp(buttonPoint());
			++probeStage;
		} else if (probeStage == 2 && cancelled) {
			probeNoClicks = probeNoClicks && s.ui_events == 0;
			resizeHost(800, 800);
			saveViewport("viewport-tall.png");
			probePress();
		} else if (probeStage == 3 && pressed) {
			resizeHost(400, 240);
			++probeStage;
		} else if (probeStage == 4 && cancelled) {
			probeNoClicks = probeNoClicks && s.ui_events == 0;
			saveViewport("viewport-small.png");
			probePress();
		} else if (probeStage == 5 && pressed) {
			focusLost(juce::Component::focusChangedDirectly);
			++probeStage;
		} else if (probeStage == 6 && cancelled) {
			probeNoClicks = probeNoClicks && s.ui_events == 0;
			probeMaximized = maximizeHost();
			++probeStage;
		} else if (probeStage == 7) {
			probeMaximized = probeMaximized && getWidth() > 0 &&
					 getHeight() > 0;
			saveViewport("viewport-maximized.png");
			resizeHost(1200, 600);
			probeReadyAt = elapsed;
			++probeStage;
		}
	}
	void timerCallback() override
	{
		if (sdk_host_copy_frame(pixels.data(), pixels.size())) {
			juce::Image::BitmapData bitmap(
				frameImage(),
				juce::Image::BitmapData::writeOnly);
			for (int row = 0; row < CONFIG_SDK_SCREEN_HEIGHT; ++row)
				std::memcpy(
					bitmap.getLinePointer(row),
					pixels.data() +
						row * CONFIG_SDK_SCREEN_WIDTH *
							4ull,
					CONFIG_SDK_SCREEN_WIDTH * 4ull);
			repaint();
		}
		char output[2048];
		size_t count = sdk_host_drain_log(output, sizeof(output));
		if (count) {
			std::fwrite(output, 1, count, stdout);
			std::fflush(stdout);
		}
		const double elapsed =
			juce::Time::getMillisecondCounterHiRes() - start;
		if (viewportProbe)
			probeTick(elapsed);
		if (smokeSeconds && !injectedDown && elapsed >= 1000 &&
		    sdk_host_stats().frames > 0 &&
		    (!viewportProbe ||
		     (probeStage == 8 && elapsed - probeReadyAt >= 200))) {
			pressedReadsBefore =
				sdk_host_stats().pointer_pressed_reads;
			pointerDown(buttonPoint());
			injectedDown = true;
			injectedAt = elapsed;
		}
		if (smokeSeconds && injectedDown && !injectedUp &&
		    elapsed - injectedAt >= 250 &&
		    sdk_host_stats().pointer_pressed_reads >
			    pressedReadsBefore) {
			pointerUp(buttonPoint());
			injectedUp = true;
		}
		if (smokeSeconds &&
		    juce::Time::getMillisecondCounterHiRes() - start >=
			    smokeSeconds * 1000.0)
			juce::JUCEApplication::getInstance()
				->systemRequestedQuit();
	}
	int smokeSeconds;
	HostMedia &media;
	bool viewportProbe = false;
	int probeStage = 0;
	bool probeNoClicks = true, probeMaximized = false;
	uint64_t probePressReads = 0, probeCancelReads = 0, probeFrames = 0;
	double probeReadyAt = 0;
	double start{};
	double injectedAt{};
	uint64_t pressedReadsBefore{};
	bool injectedDown{}, injectedUp{};
	std::vector<unsigned char> pixels;
};

class SimulatorShell final : public juce::Component, private juce::Timer {
    public:
	SimulatorShell(int seconds, HostMedia &service, bool probe)
		: view(seconds, service, probe)
		, keyboard(view, seconds == 0)
		, media(service)
	{
		addAndMakeVisible(view);
		addAndMakeVisible(devices);
		addAndMakeVisible(state);
		devices.setButtonText("Audio / MIDI settings");
		devices.onClick = [&] { media.showSettings(); };
		state.setText("JUCE host devices", juce::dontSendNotification);
		setSize(CONFIG_SDK_SCREEN_WIDTH, CONFIG_SDK_SCREEN_HEIGHT + 44);
		startTimerHz(2);
	}
	void resized() override
	{
		auto r = getLocalBounds();
		auto toolbar = r.removeFromTop(44).reduced(6);
		devices.setBounds(toolbar.removeFromLeft(190));
		state.setBounds(toolbar.reduced(12, 0));
		view.setBounds(r);
	}
	void paint(juce::Graphics &g) override
	{
		g.fillAll(juce::Colour(0xff20242b));
	}
	SimulatorView view;
	KeyboardInput keyboard;

    private:
	void timerCallback() override
	{
		state.setText(media.summary().upToFirstOccurrenceOf("\n", false,
								    false),
			      juce::dontSendNotification);
	}
	HostMedia &media;
	juce::TextButton devices;
	juce::Label state;
};

class SimulatorApplication final : public juce::JUCEApplication {
	class Window final : public juce::DocumentWindow {
	    public:
		explicit Window(int seconds, HostMedia &media, bool probe)
			: DocumentWindow("LVGL JUCE SDK / CPU0 FreeRTOS",
					 juce::Colour(0xff101b2c), allButtons)
		{
			setUsingNativeTitleBar(true);
			shell = new SimulatorShell(seconds, media, probe);
			setContentOwned(shell, true);
			setResizable(true, false);
			setResizeLimits(240, 180, 8192, 8192);
			shell->view.resizeHost = [this](int w, int h) {
				setFullScreen(false);
				setContentComponentSize(w, h + 44);
			};
			shell->view.maximizeHost = [this] {
				setFullScreen(true);
				return isFullScreen();
			};
			if (probe)
				setContentComponentSize(1200, 644);
			centreWithSize(getWidth(), getHeight());
			setVisible(true);
		}
		void closeButtonPressed() override
		{
			juce::JUCEApplication::getInstance()
				->systemRequestedQuit();
		}
		SimulatorShell *shell{};
		void activeWindowStatusChanged() override
		{
			juce::DocumentWindow::activeWindowStatusChanged();
			if (!isActiveWindow() && shell) {
				shell->view.cancelPointer();
				shell->keyboard.cancel();
			}
		}
	};

    public:
	const juce::String getApplicationName() override
	{
		return "LVGL JUCE SDK";
	}
	const juce::String getApplicationVersion() override
	{
		return SDK_VERSION;
	}
	bool moreThanOneInstanceAllowed() override
	{
		return true;
	}
	void initialise(const juce::String &commandLine) override
	{
		auto arguments =
			juce::StringArray::fromTokens(commandLine, true);
		arguments.trim();
#ifdef SDK_DIAGNOSTICS
		int viewportTest = arguments.indexOf("--viewport-self-test");
		if (viewportTest >= 0 && viewportTest + 1 < arguments.size()) {
			setApplicationReturnValue(
				runViewportTests(juce::File(
					arguments[viewportTest + 1].unquoted())) ?
					0 :
					6);
			quit();
			return;
		}
		int inputTest = arguments.indexOf("--audio-input-test");
		if (inputTest >= 0 && inputTest + 1 < arguments.size()) {
			setApplicationReturnValue(
				HostMedia::inputTest(juce::File(
					arguments[inputTest + 1].unquoted())) ?
					0 :
					5);
			quit();
			return;
		}
		int selfTest = arguments.indexOf("--media-self-test");
		if (selfTest >= 0 && selfTest + 1 < arguments.size()) {
			setApplicationReturnValue(
				HostMedia::selfTest(juce::File(
					arguments[selfTest + 1].unquoted())) ?
					0 :
					4);
			quit();
			return;
		}
#endif
		if (sdk_host_init()) {
			setApplicationReturnValue(1);
			quit();
			return;
		}
		int option = arguments.indexOf("--data-dir");
		juce::String dataDirectory = SDK_DATA_DIRECTORY;
		if (option >= 0 && option + 1 < arguments.size())
			dataDirectory = arguments[option + 1].unquoted();
		auto storage = juce::File(dataDirectory);
		if (sdk_host_storage_start(
			    storage.getFullPathName().toRawUTF8())) {
			sdk_host_storage_stop();
			setApplicationReturnValue(3);
			quit();
			return;
		}
		int seconds = 0;
		option = arguments.indexOf("--smoke-seconds");
		if (option >= 0 && option + 1 < arguments.size())
			seconds = arguments[option + 1].getIntValue();
		media.start(storage, arguments.contains("--no-audio"));
		const bool probe = arguments.contains("--viewport-smoke");
		if (probe && seconds < 6)
			seconds = 6;
		window = std::make_unique<Window>(seconds, media, probe);
		if (arguments.contains("--show-devices"))
			media.showSettings();
		sdk_runtime_start();
	}
	void shutdown() override
	{
		if (window)
			window->shell->view.finish();
		window.reset();
	}

    private:
	HostMedia media;
	std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(SimulatorApplication)
