#pragma once

#include <QColor>

// Qt-side equivalent of femm/FemmeView.cpp's OnViewDarkTheme/ApplyTheme --
// classic FEMM's dark theme is just ~8 RGB constant swaps read by its own
// drawing code, plus DarkMode::SetEnabled() for native Win32 control
// chrome. Here that's QApplication::setPalette() (covers every standard
// Qt widget -- menus, dialogs, buttons -- for free) plus these named scene
// colors, which GeometryScene/SolutionView read at item-creation time
// instead of hardcoding Qt::black. Toggling at runtime requires the
// caller to also re-run whatever repaints/rebuilds its own view (see
// MainWindow/SolutionWindow's "Dark Theme" action) -- this namespace only
// tracks the flag and the color table, it doesn't know what's on screen.
namespace AppTheme {

bool isDark();

// Sets the in-memory flag and calls qApp->setPalette() (or resets to the
// style's own default palette when turning dark mode off). Does not
// persist anything or repaint any window -- see AppPreferences for
// persistence and MainWindow/SolutionWindow's toggle handlers for repaint.
void setDark(bool dark);

QColor background();
QColor gridLine();
QColor nodeColor();
QColor segmentColor();
QColor arcColor();
// Segments/arcs with a boundary condition assigned (boundaryMarker != 0)
// render in this color instead of segmentColor()/arcColor() -- a modern-
// CAD-style visual cue (classic FEMM has no equivalent; it doesn't
// color-code this) so a boundary condition is visible without opening
// each edge's properties dialog.
QColor boundaryEdgeColor();
QColor holeColor();
QColor blockLabelNameColor();
QColor selectedColor();
QColor meshLineColor();
QColor meshPointColor();
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: the
// Solution Viewer's problem-geometry overlay (SolutionView.cpp's
// paintProblemGeometry -- the original segments/arcs drawn on TOP of a
// Density Plot fill) can't safely reuse segmentColor()/arcColor(): those
// are now the same blue/teal accents the Density Plot's own jet-style
// color table (SolutionView.cpp's kColorMap) sweeps through at its low
// end, so on any model whose visible field is dominated by low values
// (common -- see kColorMap's own comment) the overlay all but
// disappears into the fill. A jet colormap runs blue->cyan->green->
// yellow->orange->red and never passes through magenta/pink, so that's
// the one hue guaranteed not to collide with ANY point in it, unlike
// picking "a slightly different blue" which would just move the
// collision to wherever the fill happens to dominate on some other
// model.
QColor densityOverlayColor();

// Matches femm/FemmviewView.cpp's default RegionColor (RGB(120,220,120))
// -- the solid fill PlotSelectedElm draws over every element belonging to
// a block label selected via the Area tool, on top of whatever Density/
// Contour plot is active. Classic makes this user-configurable via
// GeneralPrefs; femmqt doesn't expose that (no Preferences UI hook for
// it yet), so this is the fixed default only.
QColor regionSelectionColor();

} // namespace AppTheme
