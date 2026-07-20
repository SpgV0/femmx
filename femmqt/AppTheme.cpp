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

    // Standard Qt dark palette recipe (the same one Qt's own examples and
    // most third-party "instant dark mode" snippets use) -- not trying to
    // pixel-match classic FEMM's own DarkMode::SetEnabled() (that's Win32
    // native-control theming with no Qt equivalent), just a readable,
    // conventional dark palette for the Qt widgets this app is built from.
    QPalette p;
    p.setColor(QPalette::Window, QColor(53, 53, 53));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(35, 35, 35));
    p.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
    p.setColor(QPalette::Button, QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::Highlight, QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
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

    QPalette p;
    p.setColor(QPalette::Window, QColor(240, 240, 240));
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
    p.setColor(QPalette::ToolTipText, Qt::black);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(160, 160, 160));
    p.setColor(QPalette::Button, QColor(240, 240, 240));
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(160, 160, 160));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(0, 102, 204));
    // Windows 11's own accent blue, not Fusion's default darker one --
    // the one concrete "make it look win11" touch that's actually
    // reliable to hand-pick, unlike trying to reproduce Fluent chrome.
    p.setColor(QPalette::Highlight, QColor(0, 120, 215));
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

QColor AppTheme::nodeColor()
{
  return g_dark ? Qt::white : Qt::black;
}

QColor AppTheme::segmentColor()
{
  return g_dark ? QColor(120, 170, 255) : Qt::blue;
}

QColor AppTheme::arcColor()
{
  return g_dark ? QColor(120, 200, 120) : QColor(0, 128, 0);
}

QColor AppTheme::boundaryEdgeColor()
{
  return g_dark ? QColor(255, 170, 40) : QColor(230, 140, 0);
}

QColor AppTheme::holeColor()
{
  return g_dark ? QColor(120, 120, 120) : QColor(160, 160, 160);
}

QColor AppTheme::blockLabelNameColor()
{
  return g_dark ? QColor(230, 160, 160) : QColor(120, 0, 0);
}

QColor AppTheme::selectedColor()
{
  return QColor(220, 0, 0); // red reads fine against both backgrounds -- unchanged by theme
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
  return g_dark ? Qt::white : Qt::black;
}
