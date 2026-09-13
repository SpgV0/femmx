#include "ProblemKind.h"

#include "FemmProblem.h"

#include <QFileInfo>

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
