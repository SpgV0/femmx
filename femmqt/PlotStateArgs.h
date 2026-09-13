#pragma once

// PlotStateArgs.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #86).
//
// The post-processor's view state, as command-line arguments.
//
// Lua runs only inside the classic MFC app, so `setgui("qt")` plus
// mo_savepng means "hand the solved file to femmqt.exe and let it
// draw". femmqt is a separate process: it opens the .ans fresh, with
// its own defaults, and can see nothing the script configured. Before
// this, the shell-out passed a size and nothing else, so
//
//     mo_showdensityplot(1, 0, 2.0, 0, "bmag")
//     mo_savepng("out.png")
//
// produced femmqt's default CONTOUR plot. 59% of pixels differed from
// what was asked for -- and, worse, no error said so. A valid image of
// the wrong plot is the failure mode this whole area keeps producing.
//
// So the state crosses as explicit arguments. Two decisions worth
// recording:
//
// QUANTITIES ARE NAMED, NOT NUMBERED. The classic post-processor's
// DensityPlot index means different things at DC and AC -- index 2 is
// Re(B) in a harmonic problem and |H| in a static one -- so passing the
// integer would be silently wrong for exactly half the problems. The
// names are the ones mo_showdensityplot itself accepts ("bmag",
// "hreal", "logb", ...), which also makes a hand-typed command line
// readable.
//
// UNKNOWN OPTIONS ARE AN ERROR. A mistyped --greyscal would otherwise
// render with defaults and exit 0, which is the same defect in a new
// costume.

#include <QString>
#include <QStringList>

// Every field starts "unset", meaning: leave whatever femmqt would do
// on its own. A caller that knows nothing about the view -- the
// pre-processor's mi_savepng, or a hand-run render -- passes nothing
// and gets the old behaviour exactly.
struct PlotState {
  enum class Mode { Unset, Density, Contour };

  Mode mode = Mode::Unset;

  // Index into MeshSolutionItem::DensityQuantity; -1 is unset.
  int quantity = -1;

  bool haveBounds = false;
  double lower = 0, upper = 0;

  // Tri-state: -1 unset, 0 off, 1 on.
  int greyscale = -1;
  int legend = -1;

  int numContours = 0; // 0 is unset

  bool haveContourBounds = false;
  double contourLower = 0, contourUpper = 0;

  bool isEmpty() const
  {
    return mode == Mode::Unset && quantity < 0 && !haveBounds && greyscale < 0
        && legend < 0 && numContours == 0 && !haveContourBounds;
  }
};

namespace PlotStateArgs {

// The vocabulary mo_showdensityplot accepts, mapped onto
// MeshSolutionItem::DensityQuantity's order. Returns -1 for a name that
// is not one of them.
int quantityFromName(const QString& name);
// The canonical name for an index, for messages. Empty if out of range.
QString nameForQuantity(int index);

// Reads the plot options out of an argument list, ignoring anything
// that is not "--"-prefixed (the positional size and crop arguments
// live in the same list). Returns false and sets `error` on an unknown
// option or a missing/unparsable value.
bool parse(const QStringList& args, PlotState& out, QString& error);

// The inverse, for the classic GUI's shell-out to build. Produces only
// the options that are actually set, so an empty state produces an
// empty list and the command line stays what it was.
QStringList toArguments(const PlotState& state);

} // namespace PlotStateArgs
