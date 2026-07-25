#include "AppTheme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

namespace {
bool g_dark = false;
}

bool AppTheme::isDark()
{
  return g_dark;
}

void AppTheme::setDark(bool dark)
{
  g_dark = dark;
  if (!qApp)
    return;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // both branches now always switch to Fusion (see each branch's own
  // comment for why) instead of light mode restoring whatever native
  // style was captured at startup -- there's no longer a native style to
  // restore, so that capture-and-restore dance is gone too.
  if (dark) {
    // Windows' native styles (windowsvista/windows11) render QLineEdit/
    // QComboBox backgrounds from OS theme chrome rather than fully
    // honoring a custom QPalette::Base -- confirmed directly: with only
    // the palette below applied (no style change), every text-entry
    // field in Problem Properties/Preferences/etc. stayed a light native
    // gray with barely-visible near-white text on top, while palette-
    // driven widgets elsewhere (labels, checkboxes) went dark correctly.
    // Fusion is Qt's own recommended style for custom palettes -- it
    // honors every role below, including Base -- so dark mode switches
    // to it explicitly rather than fighting the native style.
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: per
    // user-supplied reference palette ("FEMMX GUI -- Dark Theme, 5 Color
    // Palette": Background #1E1E1E, Surface/Panels #2D2D30, Text/Primary
    // #D4D4D4, Primary Accent #007ACC, Secondary Accent #4EC9B0 -- the
    // VS Code Dark+ palette) replacing the generic "standard Qt dark
    // palette recipe" that was here before, per "use the above color
    // palette... less striking". Window/Base both use the Surface/Panels
    // tone (the reference's own panels/trees/tables all share that one
    // grey, distinct from the pure #1E1E1E canvas background -- see
    // AppTheme::background(), which already matched #1E1E1E exactly and
    // needed no change) -- ToolTipBase kept a touch lighter than Surface
    // (VS Code's own tooltip is #252526/#2D2D30-ish but a little lifted)
    // so tooltips still read as a distinct floating layer.
    QPalette p;
    QColor surface(0x2D, 0x2D, 0x30);
    QColor text(0xD4, 0xD4, 0xD4);
    QColor accent(0x00, 0x7A, 0xCC);
    p.setColor(QPalette::Window, surface);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, QColor(0x25, 0x25, 0x26));
    p.setColor(QPalette::ToolTipBase, QColor(0x3C, 0x3C, 0x3C));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6A, 0x6A, 0x6A));
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6A, 0x6A, 0x6A));
    p.setColor(QPalette::BrightText, QColor(0xF4, 0x47, 0x47)); // VS Code's own error-red, muted vs. pure red
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(p);
  } else {
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
    // was: restore the captured native style (windowsvista/windows11) and
    // its own standardPalette(). Turns out that's exactly the dated,
    // beige "Windows Vista"-looking result the user reported -- confirmed
    // directly this session that "windows11" was *already* the active
    // style on this machine, so preferring it explicitly (an earlier
    // attempt at this fix, since reverted) changed nothing: the beige
    // tone is just what Qt6's own "windows11" style's standardPalette()
    // looks like for plain QComboBox/QLineEdit fields, not a fallback bug.
    // Applying the SAME fix dark mode already uses for the identical
    // underlying problem (native styles not honoring a custom palette on
    // these controls) -- switch to Fusion here too, with a clean, modern
    // light palette instead of trusting the native style's own colors.
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: per
    // the same user-supplied reference palette as the dark branch above
    // ("FEMMX GUI -- Light Theme, 5 Color Palette": Background #FFFFFF,
    // Surface/Panels #F3F4F6, Text/Primary #1E1E1E, Primary Accent
    // #007ACC, Secondary Accent #4EC9B0) -- replacing the previous
    // Windows-11-flavored light palette. Highlight now matches the same
    // #007ACC accent the dark branch uses (a deliberately shared, less
    // "striking" accent across both themes) instead of a separate
    // Windows-11-specific blue.
    QPalette p;
    QColor surface(0xF3, 0xF4, 0xF6);
    QColor text(0x1E, 0x1E, 0x1E);
    QColor accent(0x00, 0x7A, 0xCC);
    p.setColor(QPalette::Window, surface);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, Qt::white);
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xA0, 0xA0, 0xA0));
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xA0, 0xA0, 0xA0));
    p.setColor(QPalette::BrightText, QColor(0xC4, 0x2B, 0x2B)); // muted red, not pure Qt::red
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(p);
  }
}

QColor AppTheme::background()
{
  return g_dark ? QColor(30, 30, 30) : Qt::white;
}

QColor AppTheme::gridLine()
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // light mode's (220,220,220) was reported as "grid doesn't seem to
  // work" -- confirmed directly: that's a ~14% contrast difference from
  // a pure white (255,255,255) canvas, rendered as single, non-
  // antialiased 1px dots (GeometryScene::drawBackground's drawPoint
  // calls) -- practically invisible on most displays, especially at
  // high DPI where a device pixel is physically tiny. Darkened
  // significantly; dark mode's (60,60,60) against a (30,30,30)
  // background was never reported as an issue, left alone.
  return g_dark ? QColor(60, 60, 60) : QColor(160, 160, 160);
}

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: per
// user-supplied reference palette ("...use the above color palette for
// the white and dark theme... less striking, especially when drawing
// and plotting") -- the geometry/overlay entity colors below used to
// span a much wider, more saturated hue range (pure blue, bright green,
// bright orange, bright red, all independently tuned per theme) than the
// reference calls for. Rebuilt around just the reference's 2 accents
// (Primary #007ACC, Secondary #4EC9B0) PLUS one muted warm accent
// (#CE9178, VS Code's own "string" color -- same design family as the
// two given accents, since #007ACC/#4EC9B0 are themselves literally VS
// Code's Dark+ palette) for the couple of roles that still need a third,
// clearly-different hue (boundary edges) to stay legible against
// segments/arcs. Deliberately the SAME values in both themes now, unlike
// before -- one consistent accent read regardless of theme is part of
// "less striking" (fewer independently-tuned hues to reconcile), and
// these already have enough contrast against both background colors
// (backed by background()'s own dark #1E1E1E / light #FFFFFF).
// Deliberately NOT touched: the Density Plot's own band color table
// (SolutionView.cpp's kColorMap/kGreyMap) -- that's a quantitative data
// colormap serving a different purpose than these entity/drawing colors,
// and the reference mockups' own "Flux Density" legend keeps a vivid
// rainbow spectrum there too.
namespace {
const QColor kPrimaryAccent(0x00, 0x7A, 0xCC);
const QColor kSecondaryAccent(0x4E, 0xC9, 0xB0);
const QColor kWarmAccent(0xCE, 0x91, 0x78);
}

QColor AppTheme::nodeColor()
{
  return g_dark ? QColor(0xD4, 0xD4, 0xD4) : QColor(0x1E, 0x1E, 0x1E);
}

QColor AppTheme::segmentColor()
{
  return kPrimaryAccent;
}

QColor AppTheme::arcColor()
{
  return kSecondaryAccent;
}

QColor AppTheme::boundaryEdgeColor()
{
  return kWarmAccent;
}

QColor AppTheme::holeColor()
{
  return g_dark ? QColor(140, 140, 140) : QColor(150, 150, 150);
}

QColor AppTheme::blockLabelNameColor()
{
  // A desaturated version of the original warm-red role, not the
  // boundary edge's more orange kWarmAccent -- still a distinct hue at a
  // glance amongst blue segments/teal arcs, just muted rather than the
  // previous saturated red.
  return g_dark ? QColor(0xC9, 0xA0, 0xA0) : QColor(0x8A, 0x50, 0x50);
}

QColor AppTheme::selectedColor()
{
  // VS Code's own error-red, matching QPalette::BrightText above -- muted
  // vs. the previous pure (220,0,0), now theme-aware like everything else
  // on this page instead of one fixed value for both.
  return g_dark ? QColor(0xF4, 0x47, 0x47) : QColor(0xC4, 0x2B, 0x2B);
}

QColor AppTheme::meshLineColor()
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // light mode's (200,200,220) had the same low-contrast-on-white
  // problem as gridLine()'s old color, just less severe -- darkened
  // alongside it so Show Mesh is clearly visible when toggled on rather
  // than looking like it silently did nothing.
  return g_dark ? QColor(90, 90, 130) : QColor(150, 150, 180);
}

QColor AppTheme::meshPointColor()
{
  // Same reasoning as nodeColor() above -- also drives paintContour's
  // field-line color (SolutionView.cpp), so this is the one that used to
  // make the Contour Plot's lines pure stark white; now the same muted
  // Text/Primary tone as everything else.
  return g_dark ? QColor(0xD4, 0xD4, 0xD4) : QColor(0x1E, 0x1E, 0x1E);
}

QColor AppTheme::densityOverlayColor()
{
  // See this function's declaration in AppTheme.h for why a jet colormap
  // specifically calls for magenta/pink here rather than reusing
  // segmentColor()/arcColor() or picking another blue/teal/green/orange
  // shade. Same value in both themes -- it needs to stand out against
  // the Density Plot's own data colors, not the app chrome, so light/
  // dark theme isn't really the relevant axis here.
  return QColor(0xE0, 0x5A, 0xC0);
}
