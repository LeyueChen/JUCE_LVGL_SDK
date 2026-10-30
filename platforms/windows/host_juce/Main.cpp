#include <juce_gui_basics/juce_gui_basics.h>
#include "sdk/host_bridge.h"
#include "sdk_config.h"
#include <vector>
#include <cstdio>

class SimulatorView final : public juce::Component, private juce::Timer {
public:
    explicit SimulatorView(int seconds) : smokeSeconds(seconds), pixels(CONFIG_SDK_SCREEN_WIDTH * CONFIG_SDK_SCREEN_HEIGHT * 4ull),
        image(juce::Image::ARGB, CONFIG_SDK_SCREEN_WIDTH, CONFIG_SDK_SCREEN_HEIGHT, true) {
        setSize(CONFIG_SDK_SCREEN_WIDTH, CONFIG_SDK_SCREEN_HEIGHT);
        start = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
    void paint(juce::Graphics &g) override { g.fillAll(juce::Colour(0xff101b2c)); g.drawImage(image, getLocalBounds().toFloat()); }
    void mouseDown(const juce::MouseEvent &e) override { pointer(e, true); }
    void mouseDrag(const juce::MouseEvent &e) override { pointer(e, true); }
    void mouseUp(const juce::MouseEvent &e) override { pointer(e, false); }
    void mouseMove(const juce::MouseEvent &e) override { pointer(e, false); }
    void finish() {
        stopTimer();
        sdk_runtime_request_stop();
        const bool stopped = sdk_runtime_join(5000) == 0;
        sdk_host_storage_stop();
        auto stats = sdk_host_stats();
        juce::DynamicObject::Ptr report = new juce::DynamicObject;
        report->setProperty("primary_entries", int(stats.primary_entries));
        report->setProperty("secondary_entries", int(stats.secondary_entries));
        report->setProperty("bound_threads", int(stats.bound_threads));
        report->setProperty("affinity_failures", int(stats.affinity_failures));
        report->setProperty("host_group", int(stats.group));
        report->setProperty("host_processor", int(stats.processor));
        report->setProperty("frames", juce::int64(stats.frames));
        report->setProperty("heartbeats", juce::int64(stats.heartbeats));
        report->setProperty("scheduler_stopped", stopped);
        report->setProperty("ui_events", int(stats.ui_events));
        report->setProperty("storage_completed", int(stats.storage_completed));
        report->setProperty("storage_failed", int(stats.storage_failed));
        bool healthy = stopped && stats.primary_entries == 1 && stats.secondary_entries == 0 && stats.affinity_failures == 0
            && stats.bound_threads >= 8 && stats.frames > 0 && stats.heartbeats > 0;
        unsigned expectedStorage = 0;
#ifdef CONFIG_SDK_FLASHDB
        ++expectedStorage;
#endif
#ifdef CONFIG_SDK_LITTLEFS
        ++expectedStorage;
#endif
        if (smokeSeconds) healthy = healthy && stats.ui_events == 1 && stats.storage_failed == 0 && stats.storage_completed >= expectedStorage;
        report->setProperty("healthy", healthy);
        auto directory = juce::File::getCurrentWorkingDirectory();
        directory.getChildFile("runtime-report.json").replaceWithText(juce::JSON::toString(juce::var(report.get())));
        auto snapshot = directory.getChildFile("snapshot.png").createOutputStream();
        if (snapshot && snapshot->openedOk()) juce::PNGImageFormat().writeImageToStream(image, *snapshot);
        if (smokeSeconds && !healthy) juce::JUCEApplication::getInstance()->setApplicationReturnValue(2);
    }
private:
    void pointer(const juce::MouseEvent &e, bool pressed) {
        const int x = juce::jlimit(0, CONFIG_SDK_SCREEN_WIDTH - 1, e.x * CONFIG_SDK_SCREEN_WIDTH / getWidth());
        const int y = juce::jlimit(0, CONFIG_SDK_SCREEN_HEIGHT - 1, e.y * CONFIG_SDK_SCREEN_HEIGHT / getHeight());
        sdk_host_set_pointer(x, y, pressed ? 1 : 0);
    }
    void timerCallback() override {
        if (sdk_host_copy_frame(pixels.data(), pixels.size())) {
            juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::writeOnly);
            for (int row = 0; row < CONFIG_SDK_SCREEN_HEIGHT; ++row)
                std::memcpy(bitmap.getLinePointer(row), pixels.data() + row * CONFIG_SDK_SCREEN_WIDTH * 4ull, CONFIG_SDK_SCREEN_WIDTH * 4ull);
            repaint();
        }
        char output[2048];
        size_t count = sdk_host_drain_log(output, sizeof(output));
        if (count) { std::fwrite(output, 1, count, stdout); std::fflush(stdout); }
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - start;
        if (smokeSeconds && !injectedDown && elapsed >= 1000) {
            sdk_host_set_pointer(144, CONFIG_SDK_SCREEN_HEIGHT / 2 + 22, 1);
            injectedDown = true;
        }
        if (smokeSeconds && injectedDown && !injectedUp && elapsed >= 1250) {
            sdk_host_set_pointer(144, CONFIG_SDK_SCREEN_HEIGHT / 2 + 22, 0);
            injectedUp = true;
        }
        if (smokeSeconds && juce::Time::getMillisecondCounterHiRes() - start >= smokeSeconds * 1000.0)
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
    int smokeSeconds;
    double start{};
    bool injectedDown{}, injectedUp{};
    std::vector<unsigned char> pixels;
    juce::Image image;
};

class SimulatorApplication final : public juce::JUCEApplication {
    class Window final : public juce::DocumentWindow {
    public:
        explicit Window(int seconds) : DocumentWindow("LVGL JUCE SDK / CPU0 FreeRTOS", juce::Colour(0xff101b2c), allButtons) {
            setUsingNativeTitleBar(true);
            view = new SimulatorView(seconds);
            setContentOwned(view, true);
            setResizable(true, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        SimulatorView *view{};
    };
public:
    const juce::String getApplicationName() override { return "LVGL JUCE SDK"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String &commandLine) override {
        if (sdk_host_init()) { setApplicationReturnValue(1); quit(); return; }
        auto storage = juce::File::getCurrentWorkingDirectory().getChildFile("data");
        if (sdk_host_storage_start(storage.getFullPathName().toRawUTF8())) {
            sdk_host_storage_stop(); setApplicationReturnValue(3); quit(); return;
        }
        int seconds = 0;
        auto arguments = juce::StringArray::fromTokens(commandLine, true);
        int option = arguments.indexOf("--smoke-seconds");
        if (option >= 0 && option + 1 < arguments.size()) seconds = arguments[option + 1].getIntValue();
        window = std::make_unique<Window>(seconds);
        sdk_runtime_start();
    }
    void shutdown() override { if (window) window->view->finish(); window.reset(); }
private:
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(SimulatorApplication)
