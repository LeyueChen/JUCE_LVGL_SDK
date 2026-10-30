#include "viewport.h"
#include "DisplayCanvas.h"
#include <cmath>
#include <limits>
#include <vector>

bool runViewportTests(const juce::File &directory)
{
	if (directory.createDirectory().failed())
		return false;
	bool ok = true;
	juce::Array<juce::var> checks;
	auto check = [&](bool passed, const char *name) {
		juce::DynamicObject::Ptr item = new juce::DynamicObject;
		item->setProperty("check", name);
		item->setProperty("passed", passed);
		checks.add(juce::var(item.get()));
		ok = ok && passed;
	};
	const auto wide = DisplayPlacement::fit(800, 480, 1200, 600);
	check(wide.scale == 1.25f &&
		      wide.bounds == juce::Rectangle<float>(100, 0, 1000, 600),
	      "wide window has symmetric side bars");
	const auto tall = DisplayPlacement::fit(800, 480, 800, 800);
	check(tall.scale == 1 &&
		      tall.bounds == juce::Rectangle<float>(0, 160, 800, 480),
	      "tall window has symmetric top/bottom bars");
	const auto small = DisplayPlacement::fit(800, 480, 400, 240);
	check(small.scale == 0.5f &&
		      small.bounds == juce::Rectangle<float>(0, 0, 400, 240),
	      "small window scales uniformly");
	check(wide.toSource({ 600.625f, 300.625f }) ==
		      juce::Point<int>(400, 240),
	      "fractional pointer subtracts offset before inverse scale");
	check(wide.toSource({ 100, 0 }) == juce::Point<int>(0, 0) &&
		      wide.toSource({ 1099.99f, 599.99f }) ==
			      juce::Point<int>(799, 479),
	      "first and last framebuffer pixels reachable");
	check(!wide.toSource({ 99.99f, 300 }) &&
		      !wide.toSource({ 1100, 300 }) &&
		      !wide.toSource({ 600, 600 }),
	      "bars and right/bottom boundaries rejected");
	check(!DisplayPlacement::fit(800, 480, 0, 500).toSource({ 0, 0 }) &&
		      !DisplayPlacement::fit(800, 480, 500, 0)
			       .toSource({ 0, 0 }) &&
		      !DisplayPlacement::fit(0, 480, 500, 500)
			       .toSource({ 0, 0 }) &&
		      !wide.toSource(
			      { std::numeric_limits<float>::quiet_NaN(), 0 }),
	      "empty/invalid geometry never divides by zero");

	std::vector<DisplayCanvas::Input> events;
	DisplayCanvas canvas(800, 480, [&](const auto &event) {
		events.push_back(event);
	});
	canvas.setVisible(true);
	canvas.setSize(1200, 600);
	auto mouseEvent = [&](int modifiers) {
		auto time = juce::Time::getCurrentTime();
		return juce::MouseEvent(
			juce::Desktop::getInstance().getMainMouseSource(),
			{ 600.625f, 300.625f }, juce::ModifierKeys(modifiers),
			1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas, time,
			{ 600.625f, 300.625f }, time, 1, false);
	};
	auto left = mouseEvent(juce::ModifierKeys::leftButtonModifier);
	canvas.mouseDown(left);
	canvas.mouseUp(left);
	check(events.size() == 2 && events[0].pressed && !events[1].pressed,
	      "JUCE mouseUp with pre-release left modifier still releases");
	events.clear();
	auto right = mouseEvent(juce::ModifierKeys::rightButtonModifier);
	canvas.mouseDown(right);
	canvas.mouseUp(right);
	check(events.empty(), "right button cannot create a touch gesture");
	canvas.pointerDown({ 1, 300 });
	canvas.pointerDrag({ 600, 300 });
	canvas.pointerUp({ 600, 300 });
	check(events.empty(),
	      "a border press never becomes a touch when dragged inward");
	canvas.pointerDown({ 600.625f, 300.625f });
	canvas.pointerUp({ 600.625f, 300.625f });
	check(events.size() == 2 && events[0].pressed && !events[1].pressed &&
		      events[1].x == 400 && events[1].y == 240,
	      "click uses fitted image coordinates");
	events.clear();
	canvas.pointerDown({ 600, 300 });
	canvas.pointerDrag({ 1, 300 });
	canvas.pointerDrag({ 600, 300 });
	canvas.pointerUp({ 600, 300 });
	check(events.size() == 2 &&
		      events.back().kind == DisplayCanvas::InputKind::cancel,
	      "dragging out cancels once and cannot resume the same gesture");
	events.clear();
	canvas.pointerDown({ 600, 300 });
	canvas.setSize(800, 800);
	check(events.size() == 2 &&
		      events.back().kind == DisplayCanvas::InputKind::cancel,
	      "resizing while pressed cancels");
	events.clear();
	canvas.pointerDown({ 400, 400 });
	canvas.focusLost(juce::Component::focusChangedDirectly);
	check(events.size() == 2 &&
		      events.back().kind == DisplayCanvas::InputKind::cancel,
	      "focus loss cancels");
	events.clear();
	canvas.pointerDown({ 400, 400 });
	canvas.setVisible(false);
	check(events.size() == 2 &&
		      events.back().kind == DisplayCanvas::InputKind::cancel,
	      "hiding cancels");
	canvas.setVisible(true);

	auto &frame = canvas.frameImage();
	{
		juce::Graphics g(frame);
		g.fillAll(juce::Colours::whitesmoke);
		g.setColour(juce::Colours::red);
		g.fillEllipse(300, 140, 200, 200);
		g.setColour(juce::Colours::blue);
		g.fillRect(40, 40, 80, 80);
		g.setColour(juce::Colours::lime);
		for (auto p : { juce::Point<int>(0, 0),
				{ 792, 0 },
				{ 0, 472 },
				{ 792, 472 } })
			g.fillRect(p.x, p.y, 8, 8);
		g.setColour(juce::Colours::black);
		g.drawText("800 x 480 fixed framebuffer", 200, 40, 400, 40,
			   juce::Justification::centred);
	}
	struct Case {
		const char *name;
		int width, height;
		float dpi;
	};
	for (auto c : { Case{ "wide", 1200, 600, 1 },
			{ "tall", 800, 800, 1 },
			{ "small", 400, 240, 1 },
			{ "fractional", 823, 611, 1 },
			{ "dpi150", 1200, 600, 1.5f },
			{ "dpi200", 1200, 600, 2 } }) {
		canvas.setSize(c.width, c.height);
		auto image = canvas.createComponentSnapshot(
			canvas.getLocalBounds(), false, c.dpi);
		int minX = image.getWidth(), minY = image.getHeight(),
		    maxX = -1, maxY = -1;
		for (int y = 0; y < image.getHeight(); ++y)
			for (int x = 0; x < image.getWidth(); ++x) {
				auto colour = image.getPixelAt(x, y);
				if (colour.getRed() > 200 &&
				    colour.getGreen() < 60 &&
				    colour.getBlue() < 60) {
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
				}
			}
		auto p = canvas.placement();
		const float expectedDiameter = 200 * p.scale * c.dpi;
		bool circle = maxX >= minX &&
			      std::abs((maxX - minX) - (maxY - minY)) <= 2 &&
			      std::abs(float(maxX - minX + 1) -
				       expectedDiameter) <= 2;
		check(circle,
		      (juce::String(c.name) + ": rendered circle stays round")
			      .toRawUTF8());
		bool corners = true;
		for (auto source : { juce::Point<float>{ 4, 4 },
				     { 796, 4 },
				     { 4, 476 },
				     { 796, 476 } }) {
			auto pixel = (p.toView(source) * c.dpi).toInt();
			auto colour = image.getPixelAt(pixel.x, pixel.y);
			corners = corners && colour.getGreen() > 200 &&
				  colour.getRed() < 60 && colour.getBlue() < 60;
		}
		check(corners, (juce::String(c.name) +
				": all framebuffer corners remain visible")
				       .toRawUTF8());
		if (p.bounds.getX() > 0 || p.bounds.getY() > 0)
			check(image.getPixelAt(0, 0) ==
				      juce::Colour(DisplayCanvas::borderColour),
			      (juce::String(c.name) + ": margin filled")
				      .toRawUTF8());
		auto stream =
			directory.getChildFile(juce::String(c.name) + ".png")
				.createOutputStream();
		check(stream && stream->openedOk() &&
			      juce::PNGImageFormat().writeImageToStream(
				      image, *stream),
		      (juce::String(c.name) + ": screenshot saved").toRawUTF8());
	}
	juce::DynamicObject::Ptr report = new juce::DynamicObject;
	report->setProperty("passed", ok);
	report->setProperty("checks", checks);
	return directory.getChildFile("viewport-tests.json")
		       .replaceWithText(
			       juce::JSON::toString(juce::var(report.get()))) &&
	       ok;
}
