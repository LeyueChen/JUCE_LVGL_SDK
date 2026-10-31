#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "sdk/input_backend.h"
#include "sdk/input.h"
class KeyboardInput final : private juce::KeyListener, private juce::Timer {
    public:
	explicit KeyboardInput(juce::Component &target, bool enabled = true)
		: owner(target)
		, enabled(enabled)
	{
		owner.addKeyListener(this);
		if (enabled)
			startTimerHz(100);
	}
	~KeyboardInput() override
	{
		stopTimer();
		owner.removeKeyListener(this);
		sdk_input_cancel();
	}
	void cancel()
	{
		sdk_input_cancel();
		active = false;
	}

    private:
	static int code(unsigned id)
	{
		const int keys[] = { juce::KeyPress::upKey,
				     juce::KeyPress::downKey,
				     juce::KeyPress::leftKey,
				     juce::KeyPress::rightKey,
				     juce::KeyPress::returnKey,
				     juce::KeyPress::escapeKey,
				     'A',
				     'B' };
		return keys[id];
	}
	bool keyPressed(const juce::KeyPress &key, juce::Component *) override
	{
		if (!enabled)
			return false;
		for (unsigned i = 0; i < SDK_BUTTON_COUNT; ++i)
			if (key.getKeyCode() == code(i))
				return true;
		return false;
	}
	bool keyStateChanged(bool, juce::Component *) override
	{
		update();
		return false;
	}
	void timerCallback() override
	{
		update();
	}
	void update()
	{
		if (!enabled)
			return;
		const bool focused = owner.hasKeyboardFocus(true) &&
				     owner.isShowing();
		if (!focused) {
			if (active)
				cancel();
			return;
		}
		active = true;
		uint32_t mask = 0;
		for (unsigned i = 0; i < SDK_BUTTON_COUNT; ++i)
			if (juce::KeyPress::isKeyCurrentlyDown(code(i)))
				mask |= 1u << i;
		sdk_input_publish(mask);
	}
	juce::Component &owner;
	bool active = false;
	bool enabled;
};
