#include "PropertyFields.h"

#include "FemmProblem.h"
#include "ProblemKind.h"

namespace {

using ProblemKind::Category;

// A double field bound to one member of one record.
template <typename Record>
PropertyFields::Field num(QVector<Record>& list, int index, const QString& label,
    double Record::*member, QVector<int> enabledFor = {})
{
  PropertyFields::Field f;
  f.label = label;
  f.enabledForOptions = std::move(enabledFor);
  f.get = [&list, index, member]() {
    return (index >= 0 && index < list.size())
        ? QString::number(list[index].*member, 'g', 12)
        : QString();
  };
  f.set = [&list, index, member](const QString& text) {
    if (index >= 0 && index < list.size())
      list[index].*member = text.toDouble();
  };
  return f;
}

template <typename Record>
PropertyFields::Field integer(QVector<Record>& list, int index, const QString& label,
    int Record::*member, QVector<int> enabledFor = {})
{
  PropertyFields::Field f;
  f.label = label;
  f.integer = true;
  f.enabledForOptions = std::move(enabledFor);
  f.get = [&list, index, member]() {
    return (index >= 0 && index < list.size()) ? QString::number(list[index].*member)
                                               : QString();
  };
  f.set = [&list, index, member](const QString& text) {
    if (index >= 0 && index < list.size())
      list[index].*member = text.toInt();
  };
  return f;
}

template <typename Record>
PropertyFields::Selector bdryTypeSelector(QVector<Record>& list, int index,
    const QStringList& options)
{
  PropertyFields::Selector s;
  s.label = QStringLiteral("Boundary condition type:");
  s.options = options;
  s.get = [&list, index]() {
    return (index >= 0 && index < list.size()) ? list[index].bdryFormat : 0;
  };
  s.set = [&list, index](int v) {
    if (index >= 0 && index < list.size())
      list[index].bdryFormat = v;
  };
  return s;
}

// The source record is shared by the three non-magnetics kinds, but what
// its numbers MEAN is not -- see FemmConductorProp. The labels come from
// the kind, the storage does not.
PropertyFields::Spec conductorSpec(FemmProblem& p, int index)
{
  const bool complex = (p.kind == FemmProblemKind::CurrentFlow);
  const bool thermal = (p.kind == FemmProblemKind::HeatFlow);

  const QString valueLabel = thermal ? QStringLiteral("Prescribed temperature, K:")
                                     : QStringLiteral("Prescribed voltage, V:");
  const QString fluxLabel = thermal ? QStringLiteral("Prescribed heat flux, W:")
      : complex             ? QStringLiteral("Prescribed current, A (real):")
                            : QStringLiteral("Prescribed charge, C:");

  PropertyFields::Spec spec;
  spec.title = QStringLiteral("Conductor Property");
  spec.getName = [&p, index]() {
    return ProblemKind::name(p, Category::Source, index);
  };
  spec.setName = [&p, index](const QString& n) {
    ProblemKind::setName(p, Category::Source, index, n);
  };

  // Matches the classic conductor dialogs' radio pair: a conductor is
  // EITHER held at a value OR carrying a prescribed total flux, never
  // both, and <ConductorType> records which.
  PropertyFields::Selector sel;
  sel.label = QStringLiteral("Conductor is:");
  sel.options = { thermal ? QStringLiteral("Prescribed total heat flux")
                          : QStringLiteral("Prescribed total charge/current"),
    thermal ? QStringLiteral("Prescribed temperature")
            : QStringLiteral("Prescribed voltage") };
  sel.get = [&p, index]() {
    return (index >= 0 && index < p.conductorProps.size())
        ? p.conductorProps[index].conductorType
        : 0;
  };
  sel.set = [&p, index](int v) {
    if (index >= 0 && index < p.conductorProps.size())
      p.conductorProps[index].conductorType = v;
  };
  spec.selector = sel;

  spec.fields << num(p.conductorProps, index, valueLabel, &FemmConductorProp::valueRe, { 1 });
  if (complex) {
    spec.fields << num(p.conductorProps, index, QStringLiteral("Prescribed voltage, V (imaginary):"),
        &FemmConductorProp::valueIm, { 1 });
  }
  spec.fields << num(p.conductorProps, index, fluxLabel, &FemmConductorProp::fluxRe, { 0 });
  if (complex) {
    spec.fields << num(p.conductorProps, index, QStringLiteral("Prescribed current, A (imaginary):"),
        &FemmConductorProp::fluxIm, { 0 });
  }
  return spec;
}

} // namespace

bool PropertyFields::hasSpec(const FemmProblem& p, ProblemKind::Category category)
{
  // Magnetics keeps its hand-written dialogs for now: they already match
  // the classic ones field for field, and two of them do things a flat
  // field list cannot describe -- the BH curve editor, and the point
  // property's radio pair that ZEROES the other pair rather than merely
  // disabling it.
  (void)category;
  return p.kind != FemmProblemKind::Magnetics;
}

PropertyFields::Spec PropertyFields::specFor(FemmProblem& p, ProblemKind::Category category, int index)
{
  Spec spec;
  spec.getName = [&p, category, index]() { return ProblemKind::name(p, category, index); };
  spec.setName = [&p, category, index](const QString& n) {
    ProblemKind::setName(p, category, index, n);
  };

  switch (p.kind) {

  // --- Electrostatics ------------------------------------------------------
  case FemmProblemKind::Electrostatics:
    switch (category) {
    case Category::Point:
      spec.title = QStringLiteral("Point Property");
      spec.fields << num(p.esPointProps, index, QStringLiteral("Prescribed voltage, V:"),
                          &FemmEsPointProp::Vp)
                  << num(p.esPointProps, index, QStringLiteral("Point charge density, C/m:"),
                         &FemmEsPointProp::qp);
      return spec;
    case Category::Boundary:
      spec.title = QStringLiteral("Boundary Property");
      // Indices and gating from femm/bd_BdryDlg.cpp's
      // OnSelchangeBdryformat: 0 enables Vs, 1 enables c0/c1, 2 enables
      // qs.
      spec.selector = bdryTypeSelector(p.esBoundaryProps, index,
          { QStringLiteral("Fixed Voltage"), QStringLiteral("Mixed"),
              QStringLiteral("Surface Charge Density") });
      spec.fields << num(p.esBoundaryProps, index, QStringLiteral("Fixed voltage, V:"),
                          &FemmEsBoundaryProp::Vs, { 0 })
                  << num(p.esBoundaryProps, index, QStringLiteral("Surface charge density, C/m2:"),
                         &FemmEsBoundaryProp::qs, { 2 })
                  << num(p.esBoundaryProps, index, QStringLiteral("Mixed BC parameter c0:"),
                         &FemmEsBoundaryProp::c0, { 1 })
                  << num(p.esBoundaryProps, index, QStringLiteral("Mixed BC parameter c1:"),
                         &FemmEsBoundaryProp::c1, { 1 });
      return spec;
    case Category::Material:
      spec.title = QStringLiteral("Material Property");
      spec.fields << num(p.esMaterialProps, index, QStringLiteral("Relative permittivity, ex:"),
                          &FemmEsMaterialProp::ex)
                  << num(p.esMaterialProps, index, QStringLiteral("Relative permittivity, ey:"),
                         &FemmEsMaterialProp::ey)
                  << num(p.esMaterialProps, index, QStringLiteral("Volume charge density, C/m3:"),
                         &FemmEsMaterialProp::qv);
      return spec;
    case Category::Source:
      return conductorSpec(p, index);
    }
    break;

  // --- Heat flow -----------------------------------------------------------
  case FemmProblemKind::HeatFlow:
    switch (category) {
    case Category::Point:
      spec.title = QStringLiteral("Point Property");
      spec.fields << num(p.htPointProps, index, QStringLiteral("Prescribed temperature, K:"),
                          &FemmHtPointProp::Tp)
                  << num(p.htPointProps, index, QStringLiteral("Point heat generation, W/m:"),
                         &FemmHtPointProp::qp);
      return spec;
    case Category::Boundary:
      spec.title = QStringLiteral("Boundary Property");
      // Indices and gating from femm/hd_BdryDlg.cpp: 0 Tset; 1 qs;
      // 2 adds h and Tinf; 3 adds beta and TinfRad on top of those.
      spec.selector = bdryTypeSelector(p.htBoundaryProps, index,
          { QStringLiteral("Fixed Temperature"), QStringLiteral("Heat Flux"),
              QStringLiteral("Convection"), QStringLiteral("Radiation") });
      spec.fields << num(p.htBoundaryProps, index, QStringLiteral("Fixed temperature, K:"),
                          &FemmHtBoundaryProp::Tset, { 0 })
                  << num(p.htBoundaryProps, index, QStringLiteral("Heat flux, W/m2:"),
                         &FemmHtBoundaryProp::qs, { 1, 2, 3 })
                  << num(p.htBoundaryProps, index,
                         QStringLiteral("Convection coefficient h, W/(m2*K):"),
                         &FemmHtBoundaryProp::h, { 2, 3 })
                  << num(p.htBoundaryProps, index,
                         QStringLiteral("Convection ambient temperature, K:"),
                         &FemmHtBoundaryProp::Tinf, { 2, 3 })
                  << num(p.htBoundaryProps, index, QStringLiteral("Emissivity:"),
                         &FemmHtBoundaryProp::beta, { 3 })
                  << num(p.htBoundaryProps, index,
                         QStringLiteral("Radiation ambient temperature, K:"),
                         &FemmHtBoundaryProp::TinfRad, { 3 });
      return spec;
    case Category::Material:
      spec.title = QStringLiteral("Material Property");
      spec.fields << num(p.htMaterialProps, index,
                          QStringLiteral("Thermal conductivity kx, W/(m*K):"),
                          &FemmHtMaterialProp::Kx)
                  << num(p.htMaterialProps, index,
                         QStringLiteral("Thermal conductivity ky, W/(m*K):"),
                         &FemmHtMaterialProp::Ky)
                  << num(p.htMaterialProps, index,
                         QStringLiteral("Volumetric heat capacity, MJ/(m3*K):"),
                         &FemmHtMaterialProp::Kt)
                  << num(p.htMaterialProps, index,
                         QStringLiteral("Volume heat generation, W/m3:"),
                         &FemmHtMaterialProp::qv);
      return spec;
    case Category::Source:
      return conductorSpec(p, index);
    }
    break;

  // --- Current flow --------------------------------------------------------
  case FemmProblemKind::CurrentFlow:
    switch (category) {
    case Category::Point:
      spec.title = QStringLiteral("Point Property");
      spec.fields << num(p.cfPointProps, index, QStringLiteral("Prescribed voltage, V (real):"),
                          &FemmCfPointProp::vpr)
                  << num(p.cfPointProps, index, QStringLiteral("Prescribed voltage, V (imaginary):"),
                         &FemmCfPointProp::vpi)
                  << num(p.cfPointProps, index, QStringLiteral("Point current, A (real):"),
                         &FemmCfPointProp::qpr)
                  << num(p.cfPointProps, index, QStringLiteral("Point current, A (imaginary):"),
                         &FemmCfPointProp::qpi);
      return spec;
    case Category::Boundary:
      spec.title = QStringLiteral("Boundary Property");
      // Same three as electrostatics, per femm/cd_BdryDlg.cpp, with the
      // real/imaginary split current flow has everywhere.
      spec.selector = bdryTypeSelector(p.cfBoundaryProps, index,
          { QStringLiteral("Fixed Voltage"), QStringLiteral("Mixed"),
              QStringLiteral("Surface Current Density") });
      spec.fields << num(p.cfBoundaryProps, index, QStringLiteral("Fixed voltage, V (real):"),
                          &FemmCfBoundaryProp::vsr, { 0 })
                  << num(p.cfBoundaryProps, index, QStringLiteral("Fixed voltage, V (imaginary):"),
                         &FemmCfBoundaryProp::vsi, { 0 })
                  << num(p.cfBoundaryProps, index,
                         QStringLiteral("Surface current density, A/m2 (real):"),
                         &FemmCfBoundaryProp::qsr, { 2 })
                  << num(p.cfBoundaryProps, index,
                         QStringLiteral("Surface current density, A/m2 (imaginary):"),
                         &FemmCfBoundaryProp::qsi, { 2 })
                  << num(p.cfBoundaryProps, index, QStringLiteral("Mixed BC c0 (real):"),
                         &FemmCfBoundaryProp::c0r, { 1 })
                  << num(p.cfBoundaryProps, index, QStringLiteral("Mixed BC c0 (imaginary):"),
                         &FemmCfBoundaryProp::c0i, { 1 })
                  << num(p.cfBoundaryProps, index, QStringLiteral("Mixed BC c1 (real):"),
                         &FemmCfBoundaryProp::c1r, { 1 })
                  << num(p.cfBoundaryProps, index, QStringLiteral("Mixed BC c1 (imaginary):"),
                         &FemmCfBoundaryProp::c1i, { 1 });
      return spec;
    case Category::Material:
      spec.title = QStringLiteral("Material Property");
      spec.fields << num(p.cfMaterialProps, index, QStringLiteral("Conductivity ox, S/m:"),
                          &FemmCfMaterialProp::ox)
                  << num(p.cfMaterialProps, index, QStringLiteral("Conductivity oy, S/m:"),
                         &FemmCfMaterialProp::oy)
                  << num(p.cfMaterialProps, index, QStringLiteral("Relative permittivity, ex:"),
                         &FemmCfMaterialProp::ex)
                  << num(p.cfMaterialProps, index, QStringLiteral("Relative permittivity, ey:"),
                         &FemmCfMaterialProp::ey)
                  << num(p.cfMaterialProps, index, QStringLiteral("Dielectric loss tangent, x:"),
                         &FemmCfMaterialProp::ltx)
                  << num(p.cfMaterialProps, index, QStringLiteral("Dielectric loss tangent, y:"),
                         &FemmCfMaterialProp::lty);
      return spec;
    case Category::Source:
      return conductorSpec(p, index);
    }
    break;

  case FemmProblemKind::Magnetics:
    // hasSpec() says no; the hand-written dialogs handle these.
    break;
  }

  spec.title = QStringLiteral("Property");
  return spec;
}
