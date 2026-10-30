#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>
struct DisplayPlacement {
	float scale = 0;
	juce::Rectangle<float> bounds;
	int sourceWidth = 0, sourceHeight = 0;
	static DisplayPlacement fit(int sourceWidth, int sourceHeight,
				    float width, float height);
	std::optional<juce::Point<int> >
	toSource(juce::Point<float> point) const;
	juce::Point<float> toView(juce::Point<float> point) const;
};
class DisplayCanvas : public juce::Component {
    public:
	enum class InputKind { update, cancel };
	struct Input {
		InputKind kind;
		int x, y;
		bool pressed;
	};
	using InputSink = std::function<void(const Input &)>;
	DisplayCanvas(int width, int height, InputSink sink);
	DisplayPlacement placement() const;
	juce::Image &frameImage()
	{
		return frame;
	}
	void paint(juce::Graphics &) override;
	void resized() override;
	void focusLost(FocusChangeType) override;
	void visibilityChanged() override;
	void mouseDown(const juce::MouseEvent &) override;
	void mouseDrag(const juce::MouseEvent &) override;
	void mouseUp(const juce::MouseEvent &) override;
	void mouseMove(const juce::MouseEvent &) override;
	void mouseExit(const juce::MouseEvent &) override;
	// Same entry points used by JUCE events and deterministic viewport smoke tests.
	void pointerDown(juce::Point<float>);
	void pointerDrag(juce::Point<float>);
	void pointerUp(juce::Point<float>);
	void pointerMove(juce::Point<float>);
	void cancelPointer();
	void setUserInputEnabled(bool enabled)
	{
		userInputEnabled = enabled;
		if (!enabled)
			cancelPointer();
	}
	static constexpr juce::uint32 borderColour = 0xff070a10;

    private:
	void emit(juce::Point<int>, bool pressed);
	juce::Image frame;
	InputSink input;
	juce::Point<int> lastPoint;
	bool active = false;
	bool userInputEnabled = true;
};
