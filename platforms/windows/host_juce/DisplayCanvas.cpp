#include "DisplayCanvas.h"
#include <algorithm>
#include <cmath>
DisplayPlacement DisplayPlacement::fit(int w, int h, float width, float height)
{
	DisplayPlacement p;
	p.sourceWidth = w;
	p.sourceHeight = h;
	if (w <= 0 || h <= 0 || !std::isfinite(width) ||
	    !std::isfinite(height) || width <= 0 || height <= 0)
		return p;
	p.scale = std::min(width / float(w), height / float(h));
	const float drawnWidth = float(w) * p.scale,
		    drawnHeight = float(h) * p.scale;
	p.bounds = { (width - drawnWidth) * 0.5f, (height - drawnHeight) * 0.5f,
		     drawnWidth, drawnHeight };
	return p;
}
std::optional<juce::Point<int> >
DisplayPlacement::toSource(juce::Point<float> point) const
{
	if (scale <= 0 || !std::isfinite(point.x) || !std::isfinite(point.y) ||
	    !bounds.contains(point))
		return {};
	return juce::Point<int>{
		std::clamp(int(std::floor((point.x - bounds.getX()) / scale)),
			   0, sourceWidth - 1),
		std::clamp(int(std::floor((point.y - bounds.getY()) / scale)),
			   0, sourceHeight - 1)
	};
}
juce::Point<float> DisplayPlacement::toView(juce::Point<float> point) const
{
	return { bounds.getX() + point.x * scale,
		 bounds.getY() + point.y * scale };
}
DisplayCanvas::DisplayCanvas(int width, int height, InputSink sink)
	: frame(juce::Image::ARGB, width, height, true)
	, input(std::move(sink))
{
	setWantsKeyboardFocus(true);
	setSize(width, height);
}
DisplayPlacement DisplayCanvas::placement() const
{
	return DisplayPlacement::fit(frame.getWidth(), frame.getHeight(),
				     float(getWidth()), float(getHeight()));
}
void DisplayCanvas::paint(juce::Graphics &g)
{
	g.fillAll(juce::Colour(borderColour));
	const auto p = placement();
	if (p.scale > 0)
		g.drawImageTransformed(
			frame, juce::AffineTransform::scale(p.scale).translated(
				       p.bounds.getX(), p.bounds.getY()));
}
void DisplayCanvas::emit(juce::Point<int> point, bool pressed)
{
	lastPoint = point;
	if (input)
		input({ InputKind::update, point.x, point.y, pressed });
}
void DisplayCanvas::pointerDown(juce::Point<float> point)
{
	if (active)
		return;
	if (auto source = placement().toSource(point)) {
		active = true;
		emit(*source, true);
	}
}
void DisplayCanvas::pointerDrag(juce::Point<float> point)
{
	if (!active)
		return;
	if (auto source = placement().toSource(point))
		emit(*source, true);
	else
		cancelPointer();
}
void DisplayCanvas::pointerUp(juce::Point<float> point)
{
	if (!active)
		return;
	if (auto source = placement().toSource(point)) {
		active = false;
		emit(*source, false);
	} else
		cancelPointer();
}
void DisplayCanvas::pointerMove(juce::Point<float> point)
{
	if (active) {
		cancelPointer();
		return;
	}
	if (auto source = placement().toSource(point))
		emit(*source, false);
}
void DisplayCanvas::cancelPointer()
{
	if (!active)
		return;
	active = false;
	if (input)
		input({ InputKind::cancel, lastPoint.x, lastPoint.y, false });
}
void DisplayCanvas::resized()
{
	cancelPointer();
	repaint();
}
void DisplayCanvas::focusLost(FocusChangeType)
{
	cancelPointer();
}
void DisplayCanvas::visibilityChanged()
{
	if (!isVisible())
		cancelPointer();
}
void DisplayCanvas::mouseDown(const juce::MouseEvent &e)
{
	if (userInputEnabled && e.mods.isLeftButtonDown())
		pointerDown(e.position);
}
void DisplayCanvas::mouseDrag(const juce::MouseEvent &e)
{
	if (!userInputEnabled)
		return;
	if (e.mods.isLeftButtonDown())
		pointerDrag(e.position);
	else
		cancelPointer();
}
void DisplayCanvas::mouseUp(const juce::MouseEvent &e)
{
	// JUCE reports the modifiers from BEFORE release in a mouseUp event.
	// Only pointerDown can activate a left-button gesture, so every mouseUp
	// can safely finish that gesture regardless of the old modifier bits.
	if (userInputEnabled)
		pointerUp(e.position);
}
void DisplayCanvas::mouseMove(const juce::MouseEvent &e)
{
	if (userInputEnabled)
		pointerMove(e.position);
}
void DisplayCanvas::mouseExit(const juce::MouseEvent &)
{
	if (userInputEnabled)
		cancelPointer();
}
