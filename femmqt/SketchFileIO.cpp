#include "SketchFileIO.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cmath>

namespace {

// Text, not binary, and deliberately: .fem is text, the whole toolchain
// is diffable by hand, and a sketch file a user can read and repair in a
// text editor is worth more than a few saved bytes.
// Bumped to 2 for construction geometry (issue #31). A version-1 file
// simply has no construction sections, which reads as "no construction
// geometry" -- which is exactly what every model written before this is.
constexpr int kSketchVersion = 2;

// Fingerprints are compared at this absolute tolerance, in the model's
// own length units. Generous on purpose: the point is to recognise the
// SAME entity after an unrelated edit, not to detect that someone nudged
// it. A node that moved further than this is, for these purposes, a
// different node -- and the constraint gets reported rather than
// silently re-attached to it.
constexpr double kMatchTol = 1e-6;

bool near(double a, double b)
{
  return std::fabs(a - b) <= kMatchTol;
}

// --- what each reference of each type indexes -----------------------------
//
// The authority is the comment above FemmConstraint/FemmDimension in
// FemmProblem.h. Mirrored here rather than inferred, because getting it
// wrong would fingerprint a segment index against a node's coordinates
// and silently drop everything.

enum class RefKind { None, Node, Segment, Arc };

void constraintRefKinds(const FemmConstraint& c, RefKind kinds[3])
{
  kinds[0] = kinds[1] = kinds[2] = RefKind::None;
  switch (c.type) {
  case ConstraintType::Coincident:
    kinds[0] = kinds[1] = RefKind::Node;
    break;
  case ConstraintType::Horizontal:
  case ConstraintType::Vertical:
    kinds[0] = RefKind::Segment;
    break;
  case ConstraintType::Parallel:
  case ConstraintType::Perpendicular:
    kinds[0] = kinds[1] = RefKind::Segment;
    break;
  case ConstraintType::Equal:
    kinds[0] = kinds[1] = c.isArcPair ? RefKind::Arc : RefKind::Segment;
    break;
  case ConstraintType::Tangent:
    kinds[0] = c.firstIsArc ? RefKind::Arc : RefKind::Segment;
    kinds[1] = RefKind::Arc;
    break;
  case ConstraintType::Concentric:
    kinds[0] = kinds[1] = RefKind::Arc;
    break;
  case ConstraintType::Symmetric:
    kinds[0] = kinds[1] = RefKind::Node;
    kinds[2] = RefKind::Segment;
    break;
  }
}

void dimensionRefKinds(const FemmDimension& d, RefKind kinds[3])
{
  kinds[0] = kinds[1] = kinds[2] = RefKind::None;
  switch (d.type) {
  case DimensionType::Distance:
  case DimensionType::HorizontalDistance:
  case DimensionType::VerticalDistance:
    kinds[0] = kinds[1] = RefKind::Node;
    break;
  case DimensionType::Radius:
    kinds[0] = RefKind::Arc;
    break;
  case DimensionType::Angle:
    kinds[0] = kinds[1] = kinds[2] = RefKind::Node;
    break;
  case DimensionType::AngleLines:
    kinds[0] = kinds[1] = RefKind::Segment;
    break;
  }
}

// --- fingerprints ---------------------------------------------------------
//
// Four numbers is enough for every kind: a node uses two, an edge its two
// endpoints. Written as plain doubles so the file stays readable.

struct Fingerprint {
  double v[4] = { 0, 0, 0, 0 };
};

bool fingerprintOf(const FemmProblem& p, RefKind kind, int index,
    Fingerprint& out)
{
  if (index < 0)
    return false;
  if (kind == RefKind::Node) {
    if (index >= p.nodes.size())
      return false;
    out.v[0] = p.nodes[index].x;
    out.v[1] = p.nodes[index].y;
    return true;
  }
  if (kind == RefKind::Segment) {
    if (index >= p.segments.size())
      return false;
    const FemmSegment& s = p.segments[index];
    if (s.n0 < 0 || s.n0 >= p.nodes.size() || s.n1 < 0 || s.n1 >= p.nodes.size())
      return false;
    out.v[0] = p.nodes[s.n0].x;
    out.v[1] = p.nodes[s.n0].y;
    out.v[2] = p.nodes[s.n1].x;
    out.v[3] = p.nodes[s.n1].y;
    return true;
  }
  if (kind == RefKind::Arc) {
    if (index >= p.arcSegments.size())
      return false;
    const FemmArcSegment& a = p.arcSegments[index];
    if (a.n0 < 0 || a.n0 >= p.nodes.size() || a.n1 < 0 || a.n1 >= p.nodes.size())
      return false;
    out.v[0] = p.nodes[a.n0].x;
    out.v[1] = p.nodes[a.n0].y;
    out.v[2] = p.nodes[a.n1].x;
    out.v[3] = p.nodes[a.n1].y;
    return true;
  }
  return false;
}

bool sameFingerprint(const Fingerprint& a, const Fingerprint& b, RefKind kind)
{
  const int n = (kind == RefKind::Node) ? 2 : 4;
  for (int i = 0; i < n; i++) {
    if (!near(a.v[i], b.v[i]))
      return false;
  }
  return true;
}

int countOfKind(const FemmProblem& p, RefKind kind)
{
  switch (kind) {
  case RefKind::Node: return p.nodes.size();
  case RefKind::Segment: return p.segments.size();
  case RefKind::Arc: return p.arcSegments.size();
  default: return 0;
  }
}

// Accept the stored index if what sits there still matches; otherwise
// look for the one entity that does. Returns -1 when the answer is not
// unique, which is the case that MUST be reported rather than guessed:
// two identical segments would otherwise get the constraint attached to
// whichever came first.
int resolveRef(const FemmProblem& p, RefKind kind, int storedIndex,
    const Fingerprint& want, QString& why)
{
  if (kind == RefKind::None)
    return storedIndex;

  Fingerprint here;
  if (fingerprintOf(p, kind, storedIndex, here)
      && sameFingerprint(here, want, kind))
    return storedIndex;

  int found = -1;
  int matches = 0;
  const int n = countOfKind(p, kind);
  for (int i = 0; i < n; i++) {
    Fingerprint f;
    if (!fingerprintOf(p, kind, i, f))
      continue;
    if (sameFingerprint(f, want, kind)) {
      matches++;
      found = i;
    }
  }
  if (matches == 1)
    return found;
  why = (matches == 0)
      ? QStringLiteral("the geometry it referenced is gone")
      : QStringLiteral("its geometry is ambiguous (%1 entities match)")
            .arg(matches);
  return -1;
}

QString fingerprintToString(const Fingerprint& f, RefKind kind)
{
  const int n = (kind == RefKind::Node) ? 2 : 4;
  QStringList parts;
  for (int i = 0; i < n; i++)
    parts << QString::number(f.v[i], 'g', 17);
  while (parts.size() < 4)
    parts << QStringLiteral("0");
  return parts.join(' ');
}

Fingerprint fingerprintFromFields(const QStringList& fields, int at)
{
  Fingerprint f;
  for (int i = 0; i < 4 && at + i < fields.size(); i++)
    f.v[i] = fields[at + i].toDouble();
  return f;
}


// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12 (#31).
//
// Construction geometry is stored by coordinate, so restoring it means
// finding the node already at that spot or making one. Finding it
// matters more than making it: a centreline endpoint that coincides with
// a real corner is the normal case -- that is what it was drawn against
// -- and inventing a second node on top of the corner would leave the
// sketch looking joined while meshing as two separate things.
//
// The tolerance is relative to the model's own extent for the usual
// reason: these coordinates are written with 17 significant digits and
// come back exact, but a model drawn in metres and one drawn in
// millimetres cannot share an absolute epsilon.
int findOrAddNode(FemmProblem& p, double x, double y)
{
  double scale = 1.0;
  for (const FemmNode& n : p.nodes)
    scale = std::max(scale, std::max(std::abs(n.x), std::abs(n.y)));
  const double tol = 1e-12 * scale;

  for (int i = 0; i < p.nodes.size(); i++) {
    if (std::abs(p.nodes[i].x - x) <= tol && std::abs(p.nodes[i].y - y) <= tol)
      return i;
  }
  FemmNode n;
  n.x = x;
  n.y = y;
  p.nodes.push_back(n);
  return p.nodes.size() - 1;
}

} // namespace

QString SketchFileIO::sidecarPathFor(const QString& modelPath)
{
  if (modelPath.isEmpty())
    return QString();
  const QFileInfo fi(modelPath);
  return fi.absolutePath() + "/" + fi.completeBaseName() + ".fes";
}

bool SketchFileIO::writeSketch(const QString& modelPath, const FemmProblem& p,
    QString& errorMessage)
{
  const QString path = sidecarPathFor(modelPath);
  if (path.isEmpty()) {
    errorMessage = QStringLiteral("no model path to place the sketch beside");
    return false;
  }

  // Construction geometry lives ONLY here (issue #31) -- it is stripped
  // from the .fem and the .femx -- so a model with a centreline and no
  // constraints at all still needs a sidecar.
  bool anyConstruction = false;
  for (const FemmSegment& seg : p.segments)
    anyConstruction = anyConstruction || seg.isConstruction;
  for (const FemmArcSegment& arc : p.arcSegments)
    anyConstruction = anyConstruction || arc.isConstruction;
  for (const FemmNode& n : p.nodes)
    anyConstruction = anyConstruction || n.isConstruction;

  if (p.constraints.isEmpty() && p.dimensions.isEmpty() && !anyConstruction) {
    // Deleting the last constraint has to persist too. Leaving the old
    // file would resurrect the sketch on the next open.
    if (QFile::exists(path) && !QFile::remove(path)) {
      errorMessage = QStringLiteral("couldn't remove the now-empty %1")
                         .arg(QFileInfo(path).fileName());
      return false;
    }
    return true;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("couldn't write %1").arg(path);
    return false;
  }
  QTextStream out(&file);

  out << "# FEMMX sketch layer -- constraints and dimensions for the .fem\n";
  out << "# beside this file. Safe to delete: the model loads without it as\n";
  out << "# an unconstrained sketch. See femmqt/SketchFileIO.h.\n";
  out << "<Format> = " << kSketchVersion << "\n";
  out << "<Model> = \"" << QFileInfo(modelPath).fileName() << "\"\n";
  // Construction geometry comes FIRST, because the constraint and
  // dimension references below may point at it and are resolved as they
  // are read. Stored by COORDINATE rather than by index: these entities
  // are not in the .fem at all, so there is no index for them to survive
  // as, and the fingerprint machinery this file already has is built on
  // coordinates for the same reason.
  int cNodes = 0, cSegs = 0, cArcs = 0;
  for (const FemmNode& n : p.nodes)
    if (n.isConstruction)
      cNodes++;
  for (const FemmSegment& seg : p.segments)
    if (seg.isConstruction)
      cSegs++;
  for (const FemmArcSegment& arc : p.arcSegments)
    if (arc.isConstruction)
      cArcs++;

  out << "<NumConstructionNodes> = " << cNodes << "\n";
  for (const FemmNode& n : p.nodes) {
    if (!n.isConstruction)
      continue;
    out << "cnode " << QString::number(n.x, 'g', 17) << " "
        << QString::number(n.y, 'g', 17) << " " << n.inGroup << "\n";
  }

  out << "<NumConstructionSegments> = " << cSegs << "\n";
  for (const FemmSegment& seg : p.segments) {
    if (!seg.isConstruction)
      continue;
    if (seg.n0 < 0 || seg.n0 >= p.nodes.size() || seg.n1 < 0 || seg.n1 >= p.nodes.size())
      continue;
    out << "cseg " << QString::number(p.nodes[seg.n0].x, 'g', 17) << " "
        << QString::number(p.nodes[seg.n0].y, 'g', 17) << " "
        << QString::number(p.nodes[seg.n1].x, 'g', 17) << " "
        << QString::number(p.nodes[seg.n1].y, 'g', 17) << " "
        << seg.inGroup << "\n";
  }

  out << "<NumConstructionArcs> = " << cArcs << "\n";
  for (const FemmArcSegment& arc : p.arcSegments) {
    if (!arc.isConstruction)
      continue;
    if (arc.n0 < 0 || arc.n0 >= p.nodes.size() || arc.n1 < 0 || arc.n1 >= p.nodes.size())
      continue;
    out << "carc " << QString::number(p.nodes[arc.n0].x, 'g', 17) << " "
        << QString::number(p.nodes[arc.n0].y, 'g', 17) << " "
        << QString::number(p.nodes[arc.n1].x, 'g', 17) << " "
        << QString::number(p.nodes[arc.n1].y, 'g', 17) << " "
        << QString::number(arc.arcLength, 'g', 17) << " "
        << arc.inGroup << "\n";
  }

  out << "<NumConstraints> = " << p.constraints.size() << "\n";

  for (const FemmConstraint& c : p.constraints) {
    RefKind kinds[3];
    constraintRefKinds(c, kinds);
    out << "constraint " << (int)c.type << " " << (c.isArcPair ? 1 : 0)
        << " " << (c.firstIsArc ? 1 : 0);
    const int refs[3] = { c.refA, c.refB, c.refC };
    for (int i = 0; i < 3; i++) {
      Fingerprint f;
      const bool ok = fingerprintOf(p, kinds[i], refs[i], f);
      out << " " << refs[i] << " "
          << (ok ? fingerprintToString(f, kinds[i])
                 : QStringLiteral("0 0 0 0"));
    }
    out << "\n";
  }

  out << "<NumDimensions> = " << p.dimensions.size() << "\n";
  for (const FemmDimension& d : p.dimensions) {
    RefKind kinds[3];
    dimensionRefKinds(d, kinds);
    out << "dimension " << (int)d.type << " "
        << QString::number(d.value, 'g', 17) << " "
        << QString::number(d.labelOffsetX, 'g', 17) << " "
        << QString::number(d.labelOffsetY, 'g', 17);
    const int refs[3] = { d.refA, d.refB, d.refC };
    for (int i = 0; i < 3; i++) {
      Fingerprint f;
      const bool ok = fingerprintOf(p, kinds[i], refs[i], f);
      out << " " << refs[i] << " "
          << (ok ? fingerprintToString(f, kinds[i])
                 : QStringLiteral("0 0 0 0"));
    }
    out << "\n";
  }

  return true;
}

bool SketchFileIO::readSketch(const QString& modelPath, FemmProblem& p,
    QStringList& report, QString& errorMessage)
{
  report.clear();
  p.constraints.clear();
  p.dimensions.clear();
  // #31: construction geometry lives only in this file, so re-reading it
  // has to start from the state the .fem describes. Without this a
  // second read would stack a duplicate centreline on the first.
  if (FemmProblemEdit::hasConstruction(p))
    p = FemmProblemEdit::withoutConstruction(p);

  const QString path = sidecarPathFor(modelPath);
  if (path.isEmpty() || !QFile::exists(path))
    return true; // no sketch is not an error

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("couldn't read %1").arg(path);
    return false;
  }
  QTextStream in(&file);

  int version = 0;
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#'))
      continue;

    if (line.startsWith("<Format>", Qt::CaseInsensitive)) {
      const int eq = line.indexOf('=');
      version = (eq >= 0) ? line.mid(eq + 1).trimmed().toInt() : 0;
      if (version > kSketchVersion) {
        errorMessage =
            QStringLiteral("%1 was written by a newer version of FEMMX "
                           "(format %2, this build understands %3)")
                .arg(QFileInfo(path).fileName())
                .arg(version)
                .arg(kSketchVersion);
        return false;
      }
      continue;
    }
    if (line.startsWith('<'))
      continue; // counts and the model name are informational

    const QStringList f = line.split(' ', Qt::SkipEmptyParts);
    if (f.isEmpty())
      continue;

    // Construction geometry (#31). These appear before any constraint in
    // the file, which matters: a constraint may reference one of them,
    // and references are resolved as they are read.
    if (f[0] == "cnode" && f.size() >= 3) {
      const int n = findOrAddNode(p, f[1].toDouble(), f[2].toDouble());
      p.nodes[n].isConstruction = true;
      if (f.size() >= 4)
        p.nodes[n].inGroup = f[3].toInt();
      continue;
    }

    if (f[0] == "cseg" && f.size() >= 5) {
      const int a = findOrAddNode(p, f[1].toDouble(), f[2].toDouble());
      const int b = findOrAddNode(p, f[3].toDouble(), f[4].toDouble());
      if (a == b) {
        report << QStringLiteral("dropped a construction line: both ends "
                                 "resolved to the same point");
        continue;
      }
      FemmSegment seg;
      seg.n0 = a;
      seg.n1 = b;
      seg.isConstruction = true;
      if (f.size() >= 6)
        seg.inGroup = f[5].toInt();
      p.segments.push_back(seg);
      continue;
    }

    if (f[0] == "carc" && f.size() >= 6) {
      const int a = findOrAddNode(p, f[1].toDouble(), f[2].toDouble());
      const int b = findOrAddNode(p, f[3].toDouble(), f[4].toDouble());
      if (a == b) {
        report << QStringLiteral("dropped a construction arc: both ends "
                                 "resolved to the same point");
        continue;
      }
      FemmArcSegment arc;
      arc.n0 = a;
      arc.n1 = b;
      arc.arcLength = f[5].toDouble();
      arc.isConstruction = true;
      if (f.size() >= 7)
        arc.inGroup = f[6].toInt();
      p.arcSegments.push_back(arc);
      continue;
    }

    if (f[0] == "constraint" && f.size() >= 4 + 3 * 5) {
      FemmConstraint c;
      c.type = (ConstraintType)f[1].toInt();
      c.isArcPair = f[2].toInt() != 0;
      c.firstIsArc = f[3].toInt() != 0;
      RefKind kinds[3];
      constraintRefKinds(c, kinds);

      int resolved[3] = { -1, -1, -1 };
      QString why;
      bool ok = true;
      for (int i = 0; i < 3; i++) {
        const int base = 4 + i * 5;
        const int stored = f[base].toInt();
        const Fingerprint want = fingerprintFromFields(f, base + 1);
        if (kinds[i] == RefKind::None) {
          resolved[i] = stored;
          continue;
        }
        resolved[i] = resolveRef(p, kinds[i], stored, want, why);
        if (resolved[i] < 0)
          ok = false;
      }
      if (!ok) {
        report << QStringLiteral("dropped a constraint: %1")
                      .arg(why.isEmpty()
                              ? QStringLiteral("its geometry no longer matches")
                              : why);
        continue;
      }
      c.refA = resolved[0];
      c.refB = resolved[1];
      c.refC = resolved[2];
      p.constraints.push_back(c);
      continue;
    }

    if (f[0] == "dimension" && f.size() >= 5 + 3 * 5) {
      FemmDimension d;
      d.type = (DimensionType)f[1].toInt();
      d.value = f[2].toDouble();
      d.labelOffsetX = f[3].toDouble();
      d.labelOffsetY = f[4].toDouble();
      RefKind kinds[3];
      dimensionRefKinds(d, kinds);

      int resolved[3] = { -1, -1, -1 };
      QString why;
      bool ok = true;
      for (int i = 0; i < 3; i++) {
        const int base = 5 + i * 5;
        const int stored = f[base].toInt();
        const Fingerprint want = fingerprintFromFields(f, base + 1);
        if (kinds[i] == RefKind::None) {
          resolved[i] = stored;
          continue;
        }
        resolved[i] = resolveRef(p, kinds[i], stored, want, why);
        if (resolved[i] < 0)
          ok = false;
      }
      if (!ok) {
        report << QStringLiteral("dropped a dimension: %1")
                      .arg(why.isEmpty()
                              ? QStringLiteral("its geometry no longer matches")
                              : why);
        continue;
      }
      d.refA = resolved[0];
      d.refB = resolved[1];
      d.refC = resolved[2];
      p.dimensions.push_back(d);
      continue;
    }

    report << QStringLiteral("ignored an unrecognised line: %1")
                  .arg(line.left(60));
  }

  return true;
}
