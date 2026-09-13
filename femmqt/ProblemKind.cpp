#include "ProblemKind.h"

#include "FemmProblem.h"

#include <QFileInfo>
#include <QSet>

namespace {

// One table rather than four switches. Adding a fifth physics would mean
// adding a row here and a codec; anything that has to change in more
// places than that is a design that will drift.
struct KindInfo {
  FemmProblemKind kind;
  const char* display;
  const char* extension;
  const char* solver;
  const char* solutionExtension;
  const char* sourceLabel;
};

constexpr KindInfo kKinds[] = {
  { FemmProblemKind::Magnetics, "Magnetics", "fem", "fkn.exe", "ans", "Circuits" },
  { FemmProblemKind::Electrostatics, "Electrostatics", "fee", "belasolv.exe", "res", "Conductors" },
  { FemmProblemKind::HeatFlow, "Heat Flow", "feh", "hsolv.exe", "anh", "Conductors" },
  { FemmProblemKind::CurrentFlow, "Current Flow", "fec", "csolv.exe", "anc", "Conductors" },
};

const KindInfo& info(FemmProblemKind kind)
{
  for (const KindInfo& k : kKinds) {
    if (k.kind == kind)
      return k;
  }
  return kKinds[0];
}

} // namespace

QString ProblemKind::displayName(FemmProblemKind kind)
{
  return QString::fromLatin1(info(kind).display);
}

QString ProblemKind::extension(FemmProblemKind kind)
{
  return QString::fromLatin1(info(kind).extension);
}

bool ProblemKind::kindForPath(const QString& path, FemmProblemKind& kindOut)
{
  const QString suffix = QFileInfo(path).suffix().toLower();
  for (const KindInfo& k : kKinds) {
    if (suffix == QLatin1String(k.extension)) {
      kindOut = k.kind;
      return true;
    }
  }
  return false;
}

QString ProblemKind::solverExecutable(FemmProblemKind kind)
{
  return QString::fromLatin1(info(kind).solver);
}

QString ProblemKind::solutionExtension(FemmProblemKind kind)
{
  return QString::fromLatin1(info(kind).solutionExtension);
}

QString ProblemKind::categoryLabel(FemmProblemKind kind, Category category)
{
  switch (category) {
  case Category::Point:
    return QStringLiteral("Point Properties");
  case Category::Boundary:
    return QStringLiteral("Boundary Conditions");
  case Category::Material:
    return QStringLiteral("Materials");
  case Category::Source:
    // The one label that genuinely differs: a circuit carries current
    // through a region, a conductor is an equipotential surface.
    return QString::fromLatin1(info(kind).sourceLabel);
  }
  return QString();
}

// ---------------------------------------------------------------------------
// The access path
// ---------------------------------------------------------------------------
//
// Written as one switch over (kind, category) returning a count or a
// name. Verbose, but every branch is a one-liner and the alternative --
// a virtual interface over sixteen tiny structs -- is more machinery
// than the problem deserves.

int ProblemKind::count(const FemmProblem& p, Category category)
{
  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    switch (category) {
    case Category::Point: return p.pointProps.size();
    case Category::Boundary: return p.boundaryProps.size();
    case Category::Material: return p.materialProps.size();
    case Category::Source: return p.circuitProps.size();
    }
    break;
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point: return p.esPointProps.size();
    case Category::Boundary: return p.esBoundaryProps.size();
    case Category::Material: return p.esMaterialProps.size();
    case Category::Source: return p.conductorProps.size();
    }
    break;
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point: return p.htPointProps.size();
    case Category::Boundary: return p.htBoundaryProps.size();
    case Category::Material: return p.htMaterialProps.size();
    case Category::Source: return p.conductorProps.size();
    }
    break;
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point: return p.cfPointProps.size();
    case Category::Boundary: return p.cfBoundaryProps.size();
    case Category::Material: return p.cfMaterialProps.size();
    case Category::Source: return p.conductorProps.size();
    }
    break;
  }
  return 0;
}

QString ProblemKind::name(const FemmProblem& p, Category category, int index)
{
  if (index < 0 || index >= count(p, category))
    return QString();

  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    switch (category) {
    case Category::Point: return p.pointProps[index].name;
    case Category::Boundary: return p.boundaryProps[index].name;
    case Category::Material: return p.materialProps[index].name;
    case Category::Source: return p.circuitProps[index].name;
    }
    break;
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point: return p.esPointProps[index].name;
    case Category::Boundary: return p.esBoundaryProps[index].name;
    case Category::Material: return p.esMaterialProps[index].name;
    case Category::Source: return p.conductorProps[index].name;
    }
    break;
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point: return p.htPointProps[index].name;
    case Category::Boundary: return p.htBoundaryProps[index].name;
    case Category::Material: return p.htMaterialProps[index].name;
    case Category::Source: return p.conductorProps[index].name;
    }
    break;
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point: return p.cfPointProps[index].name;
    case Category::Boundary: return p.cfBoundaryProps[index].name;
    case Category::Material: return p.cfMaterialProps[index].name;
    case Category::Source: return p.conductorProps[index].name;
    }
    break;
  }
  return QString();
}

QStringList ProblemKind::names(const FemmProblem& p, Category category)
{
  QStringList out;
  const int n = count(p, category);
  out.reserve(n);
  for (int i = 0; i < n; i++)
    out << name(p, category, i);
  return out;
}

void ProblemKind::setName(FemmProblem& p, Category category, int index, const QString& newName)
{
  if (index < 0 || index >= count(p, category))
    return;

  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    switch (category) {
    case Category::Point: p.pointProps[index].name = newName; return;
    case Category::Boundary: p.boundaryProps[index].name = newName; return;
    case Category::Material: p.materialProps[index].name = newName; return;
    case Category::Source: p.circuitProps[index].name = newName; return;
    }
    return;
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point: p.esPointProps[index].name = newName; return;
    case Category::Boundary: p.esBoundaryProps[index].name = newName; return;
    case Category::Material: p.esMaterialProps[index].name = newName; return;
    case Category::Source: p.conductorProps[index].name = newName; return;
    }
    return;
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point: p.htPointProps[index].name = newName; return;
    case Category::Boundary: p.htBoundaryProps[index].name = newName; return;
    case Category::Material: p.htMaterialProps[index].name = newName; return;
    case Category::Source: p.conductorProps[index].name = newName; return;
    }
    return;
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point: p.cfPointProps[index].name = newName; return;
    case Category::Boundary: p.cfBoundaryProps[index].name = newName; return;
    case Category::Material: p.cfMaterialProps[index].name = newName; return;
    case Category::Source: p.conductorProps[index].name = newName; return;
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// List editing (issue #81)
// ---------------------------------------------------------------------------

namespace {

// Visits every 1-based reference to `category` in the problem's geometry,
// calling fn(ref) with a mutable reference to each.
//
// Written once, over the ENTITY fields rather than the property lists,
// because which entity field refers to a category is a property of the
// category and the kind -- not of the physics' field names. That is what
// makes the renumbering below identical in all four.
template <typename Fn>
void visitReferences(FemmProblem& p, ProblemKind::Category category, Fn fn)
{
  switch (category) {
  case ProblemKind::Category::Point:
    for (FemmNode& n : p.nodes)
      fn(n.pointPropIndex);
    return;
  case ProblemKind::Category::Boundary:
    for (FemmSegment& s : p.segments)
      fn(s.boundaryMarker);
    for (FemmArcSegment& a : p.arcSegments)
      fn(a.boundaryMarker);
    return;
  case ProblemKind::Category::Material:
    for (FemmBlockLabel& b : p.blockLabels)
      fn(b.blockTypeIndex);
    return;
  case ProblemKind::Category::Source:
    // The asymmetry the formats themselves have: magnetics hangs a
    // circuit off a block label, the other three hang a conductor off
    // nodes, segments and arcs.
    if (p.kind == FemmProblemKind::Magnetics) {
      for (FemmBlockLabel& b : p.blockLabels)
        fn(b.circuitIndex);
    } else {
      for (FemmNode& n : p.nodes)
        fn(n.conductorIndex);
      for (FemmSegment& s : p.segments)
        fn(s.conductorIndex);
      for (FemmArcSegment& a : p.arcSegments)
        fn(a.conductorIndex);
    }
    return;
  }
}

// "New Material", "New Material 2", ... -- never a duplicate, because
// the .fem formats identify a property BY NAME when the classic GUI
// writes them out, so two entries sharing one is not a cosmetic problem
// (this is the same defect #34 fixed in the shipped material libraries).
QString uniqueName(const FemmProblem& p, ProblemKind::Category category, const QString& base)
{
  QSet<QString> taken;
  const int n = ProblemKind::count(p, category);
  for (int i = 0; i < n; i++)
    taken.insert(ProblemKind::name(p, category, i));
  if (!taken.contains(base))
    return base;
  for (int k = 2;; k++) {
    const QString candidate = QStringLiteral("%1 %2").arg(base).arg(k);
    if (!taken.contains(candidate))
      return candidate;
  }
}

const char* defaultNameFor(ProblemKind::Category category)
{
  switch (category) {
  case ProblemKind::Category::Point: return "New Point Property";
  case ProblemKind::Category::Boundary: return "New Boundary Condition";
  case ProblemKind::Category::Material: return "New Material";
  case ProblemKind::Category::Source: return "New Source";
  }
  return "New";
}

} // namespace

int ProblemKind::addDefault(FemmProblem& p, Category category)
{
  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    switch (category) {
    case Category::Point: p.pointProps.push_back(FemmPointProp()); break;
    case Category::Boundary: p.boundaryProps.push_back(FemmBoundaryProp()); break;
    case Category::Material: p.materialProps.push_back(FemmMaterialProp()); break;
    case Category::Source: p.circuitProps.push_back(FemmCircuitProp()); break;
    }
    break;
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point: p.esPointProps.push_back(FemmEsPointProp()); break;
    case Category::Boundary: p.esBoundaryProps.push_back(FemmEsBoundaryProp()); break;
    case Category::Material: p.esMaterialProps.push_back(FemmEsMaterialProp()); break;
    case Category::Source: p.conductorProps.push_back(FemmConductorProp()); break;
    }
    break;
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point: p.htPointProps.push_back(FemmHtPointProp()); break;
    case Category::Boundary: p.htBoundaryProps.push_back(FemmHtBoundaryProp()); break;
    case Category::Material: p.htMaterialProps.push_back(FemmHtMaterialProp()); break;
    case Category::Source: p.conductorProps.push_back(FemmConductorProp()); break;
    }
    break;
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point: p.cfPointProps.push_back(FemmCfPointProp()); break;
    case Category::Boundary: p.cfBoundaryProps.push_back(FemmCfBoundaryProp()); break;
    case Category::Material: p.cfMaterialProps.push_back(FemmCfMaterialProp()); break;
    case Category::Source: p.conductorProps.push_back(FemmConductorProp()); break;
    }
    break;
  }

  const int index = count(p, category) - 1;
  setName(p, category, index,
      uniqueName(p, category, QString::fromLatin1(defaultNameFor(category))));
  return index;
}

int ProblemKind::referenceCount(const FemmProblem& p, Category category, int index)
{
  if (index < 0 || index >= count(p, category))
    return 0;
  const int oneBased = index + 1;
  int n = 0;
  // const_cast: visitReferences hands out mutable references so it can
  // serve remove() too, and counting does not use that.
  visitReferences(const_cast<FemmProblem&>(p), category, [&](int& ref) {
    if (ref == oneBased)
      n++;
  });
  return n;
}

void ProblemKind::remove(FemmProblem& p, Category category, int index)
{
  if (index < 0 || index >= count(p, category))
    return;
  const int oneBased = index + 1;

  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    switch (category) {
    case Category::Point: p.pointProps.remove(index); break;
    case Category::Boundary: p.boundaryProps.remove(index); break;
    case Category::Material: p.materialProps.remove(index); break;
    case Category::Source: p.circuitProps.remove(index); break;
    }
    break;
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point: p.esPointProps.remove(index); break;
    case Category::Boundary: p.esBoundaryProps.remove(index); break;
    case Category::Material: p.esMaterialProps.remove(index); break;
    case Category::Source: p.conductorProps.remove(index); break;
    }
    break;
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point: p.htPointProps.remove(index); break;
    case Category::Boundary: p.htBoundaryProps.remove(index); break;
    case Category::Material: p.htMaterialProps.remove(index); break;
    case Category::Source: p.conductorProps.remove(index); break;
    }
    break;
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point: p.cfPointProps.remove(index); break;
    case Category::Boundary: p.cfBoundaryProps.remove(index); break;
    case Category::Material: p.cfMaterialProps.remove(index); break;
    case Category::Source: p.conductorProps.remove(index); break;
    }
    break;
  }

  // A reference to the entry that went becomes "none"; everything above
  // it shifts down by one. A material's "none" is -1 (a hole), not 0 --
  // the one place the encodings differ, and getting it wrong would turn
  // every label that used the deleted material into a meshed region of
  // the FIRST material rather than a hole.
  const int noneValue = (category == Category::Material) ? -1 : 0;
  visitReferences(p, category, [&](int& ref) {
    if (ref == oneBased)
      ref = noneValue;
    else if (ref > oneBased)
      ref--;
  });
}
