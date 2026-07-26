#pragma once

#include <QIcon>
#include <QString>

// Loads a toolbar icon from icons.qrc and recolors it to match the
// current application palette, instead of shipping separate light/dark
// asset variants. Each source SVG (femmqt/icons/*.svg) uses the literal
// placeholder "CURRENTCOLOR" in place of any fill/stroke color -- this
// substitutes it with QPalette::ButtonText before rasterizing, so the
// icon reads correctly whether Qt's active palette is light or dark
// (including a future in-app theme toggle, or the OS theme on platforms
// where Qt's style follows it automatically) without this code needing
// to know which one is active.
namespace IconTheme {

QIcon themedToolIcon(const QString& svgResourcePath);

} // namespace IconTheme
