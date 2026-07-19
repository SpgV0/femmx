#pragma once

class QToolBar;

// Qt's own automatic tooltip appears almost instantly (~700ms, OS/style
// dependent) -- per explicit user request, toolbar buttons instead wait
// a slower, more deliberate 2 seconds before showing their help text.
// Call once per toolbar, after all of its actions/buttons are in place
// (this scans QToolBar::actions() -> widgetForAction() once, it doesn't
// track actions added later). Reads each button's QAction::toolTip(), so
// callers must set a real, descriptive tooltip on every action for this
// to be useful -- an empty toolTip() shows nothing, same as Qt's own
// mechanism would.
namespace HoverTooltip {
void installOn(QToolBar* bar);
}
