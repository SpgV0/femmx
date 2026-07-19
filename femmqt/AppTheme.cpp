#include "AppTheme.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>

namespace {
bool g_dark = false;
// Captured lazily on the very first setDark() call, before this ever
// touches the active style -- lets light mode restore exactly whatever
// native style (typically "windowsvista" or "windows11" on Windows) was
// active at startup, rather than hardcoding a guess.
QString g_originalStyleName;
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
  if (g_originalStyleName.isEmpty())
    g_originalStyleName = qApp->style()->objectName();

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
    if (!g_originalStyleName.isEmpty())
      qApp->setStyle(QStyleFactory::create(g_originalStyleName));
    qApp->setPalette(qApp->style()->standardPalette());
  }
}

QColor AppTheme::background()
{
  return g_dark ? QColor(30, 30, 30) : Qt::white;
}

QColor AppTheme::gridLine()
{
  return g_dark ? QColor(60, 60, 60) : QColor(220, 220, 220);
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
  return g_dark ? QColor(90, 90, 130) : QColor(200, 200, 220);
}

QColor AppTheme::meshPointColor()
{
  return g_dark ? Qt::white : Qt::black;
}
