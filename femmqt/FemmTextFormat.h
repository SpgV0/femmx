#pragma once

// FemmTextFormat.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// The line-level grammar shared by all four FEMM text formats: tags of
// the form "  <Tag> = value", section headers "[Section] = count", and
// whitespace-separated geometry rows. These were private helpers inside
// FemmFileIO.cpp when there was one format; there are four, and they
// parse identically in all of them, so they move here rather than being
// copied three times.

#include <QString>
#include <QVector>

namespace FemmTextFormat {

// "%.17g"-equivalent. 17 significant digits always reconstructs an
// IEEE-754 double exactly, which is what matters: the solvers read these
// back with plain strtod, so the requirement is round-trip fidelity, not
// byte-identity with the classic writer's formatting.
QString g17(double v);

// Strips one surrounding pair of double quotes, if present.
QString unquote(const QString& s);

// Splits "  <Tag>   =  value" or "[Tag] = value" into tag ("Tag") and
// value. Returns false when the line has no '=' -- which is how the bare
// record delimiters (<BeginBlock>, <EndBlock> and friends) are told
// apart from fields, since those carry no value.
bool splitTagValue(const QString& line, QString& tag, QString& value);

// Whitespace-separated columns, for the geometry rows.
QVector<QString> splitFields(const QString& line);

} // namespace FemmTextFormat
