#include "PropertyCodec.h"

#include "FemmProblem.h"
#include "FemmTextFormat.h"

#include <QTextStream>

using FemmTextFormat::g17;
using FemmTextFormat::splitFields;
using FemmTextFormat::splitTagValue;
using FemmTextFormat::unquote;

namespace {

// Reads `count` self-delimiting records into `into`.
//
// One loop instead of sixteen copies of it. The per-kind part is the
// `field` callback, which is the only thing that actually differs
// between the formats -- the Begin/End framing is identical in all four,
// and it is the framing that has the subtle failure: a record whose
// closing tag is never matched swallows the rest of the file silently,
// because the bare <EndBlock> has no '=' and so is invisible to
// splitTagValue.
template <typename T, typename FieldFn>
void readRecords(QVector<T>& into, int count, const char* endTag,
    const PropertyCodec::LineReader& next, FieldFn field)
{
  const QString end = QString::fromLatin1(endTag);
  QString line;
  for (int i = 0; i < count; i++) {
    T record;
    while (next(line)) {
      if (line.trimmed() == end)
        break;
      QString tag, value;
      if (!splitTagValue(line, tag, value))
        continue; // a bare <BeginX>, or a curve data row handled below
      field(record, tag, value);
    }
    into.push_back(record);
  }
}

// A curve appended after its own <...Points> = N tag: N rows of two
// whitespace-separated numbers. Magnetics' BH curve and heat flow's
// temperature/conductivity curve have the identical shape.
void readCurve(QVector<QPair<double, double>>& into, int count,
    const PropertyCodec::LineReader& next)
{
  QString line;
  for (int k = 0; k < count && next(line); k++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() >= 2)
      into.push_back({ f[0].toDouble(), f[1].toDouble() });
  }
}

void tag(QTextStream& out, const char* name, const QString& value)
{
  out << "    <" << name << "> = " << value << "\n";
}

void tagD(QTextStream& out, const char* name, double value)
{
  tag(out, name, g17(value));
}

void tagI(QTextStream& out, const char* name, int value)
{
  tag(out, name, QString::number(value));
}

void tagQ(QTextStream& out, const char* name, const QString& value)
{
  tag(out, name, "\"" + value + "\"");
}

void writeCurve(QTextStream& out, const QVector<QPair<double, double>>& curve)
{
  for (const QPair<double, double>& pt : curve)
    out << "      " << g17(pt.first) << "\t" << g17(pt.second) << "\n";
}

} // namespace

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

bool PropertyCodec::readSection(FemmProblem& p, const QString& tag, int count,
    const LineReader& next)
{
  switch (p.kind) {

  // --- Magnetics (.fem) ----------------------------------------------------
  case FemmProblemKind::Magnetics:
    if (tag == "PointProps") {
      readRecords(p.pointProps, count, "<EndPoint>", next,
          [](FemmPointProp& r, const QString& t, const QString& v) {
            if (t == "PointName") r.name = unquote(v);
            else if (t == "I_re") r.Jr = v.toDouble();
            else if (t == "I_im") r.Ji = v.toDouble();
            else if (t == "A_re") r.Ar = v.toDouble();
            else if (t == "A_im") r.Ai = v.toDouble();
          });
      return true;
    }
    if (tag == "BdryProps") {
      readRecords(p.boundaryProps, count, "<EndBdry>", next,
          [](FemmBoundaryProp& r, const QString& t, const QString& v) {
            if (t == "BdryName") r.name = unquote(v);
            else if (t == "BdryType") r.bdryFormat = v.toInt();
            else if (t == "A_0") r.A0 = v.toDouble();
            else if (t == "A_1") r.A1 = v.toDouble();
            else if (t == "A_2") r.A2 = v.toDouble();
            else if (t == "Phi") r.phi = v.toDouble();
            else if (t == "c0") r.c0re = v.toDouble();
            else if (t == "c0i") r.c0im = v.toDouble();
            else if (t == "c1") r.c1re = v.toDouble();
            else if (t == "c1i") r.c1im = v.toDouble();
            else if (t == "Mu_ssd") r.muSsd = v.toDouble();
            else if (t == "Sigma_ssd") r.sigmaSsd = v.toDouble();
            else if (t == "innerangle") r.innerAngle = v.toDouble();
            else if (t == "outerangle") r.outerAngle = v.toDouble();
          });
      return true;
    }
    if (tag == "BlockProps") {
      readRecords(p.materialProps, count, "<EndBlock>", next,
          [&next](FemmMaterialProp& r, const QString& t, const QString& v) {
            if (t == "BlockName") r.name = unquote(v);
            else if (t == "Mu_x") r.muX = v.toDouble();
            else if (t == "Mu_y") r.muY = v.toDouble();
            else if (t == "H_c") r.Hc = v.toDouble();
            else if (t == "H_cAngle") r.HcAngle = v.toDouble();
            else if (t == "J_re") r.JsrcRe = v.toDouble();
            else if (t == "J_im") r.JsrcIm = v.toDouble();
            else if (t == "Sigma") r.sigma = v.toDouble();
            else if (t == "d_lam") r.dLam = v.toDouble();
            else if (t == "Phi_h") r.phiH = v.toDouble();
            else if (t == "Phi_hx") r.phiHx = v.toDouble();
            else if (t == "Phi_hy") r.phiHy = v.toDouble();
            else if (t == "LamType") r.lamType = v.toInt();
            else if (t == "LamFill") r.lamFill = v.toDouble();
            else if (t == "NStrands") r.nStrands = v.toInt();
            else if (t == "WireD") r.wireD = v.toDouble();
            else if (t == "BHPoints") readCurve(r.bhData, v.toInt(), next);
          });
      return true;
    }
    if (tag == "CircuitProps") {
      readRecords(p.circuitProps, count, "<EndCircuit>", next,
          [](FemmCircuitProp& r, const QString& t, const QString& v) {
            if (t == "CircuitName") r.name = unquote(v);
            else if (t == "TotalAmps_re") r.ampsRe = v.toDouble();
            else if (t == "TotalAmps_im") r.ampsIm = v.toDouble();
            else if (t == "CircuitType") r.circType = v.toInt();
            else if (t == "VoltGradient_re") r.voltGradientRe = v.toDouble();
            else if (t == "VoltGradient_im") r.voltGradientIm = v.toDouble();
          });
      return true;
    }
    return false;

  // --- Electrostatics (.fee) -----------------------------------------------
  case FemmProblemKind::Electrostatics:
    if (tag == "PointProps") {
      readRecords(p.esPointProps, count, "<EndPoint>", next,
          [](FemmEsPointProp& r, const QString& t, const QString& v) {
            if (t == "PointName") r.name = unquote(v);
            else if (t == "Vp") r.Vp = v.toDouble();
            else if (t == "qp") r.qp = v.toDouble();
          });
      return true;
    }
    if (tag == "BdryProps") {
      readRecords(p.esBoundaryProps, count, "<EndBdry>", next,
          [](FemmEsBoundaryProp& r, const QString& t, const QString& v) {
            if (t == "BdryName") r.name = unquote(v);
            else if (t == "BdryType") r.bdryFormat = v.toInt();
            else if (t == "Vs") r.Vs = v.toDouble();
            else if (t == "qs") r.qs = v.toDouble();
            else if (t == "c0") r.c0 = v.toDouble();
            else if (t == "c1") r.c1 = v.toDouble();
          });
      return true;
    }
    if (tag == "BlockProps") {
      readRecords(p.esMaterialProps, count, "<EndBlock>", next,
          [](FemmEsMaterialProp& r, const QString& t, const QString& v) {
            if (t == "BlockName") r.name = unquote(v);
            else if (t == "ex") r.ex = v.toDouble();
            else if (t == "ey") r.ey = v.toDouble();
            else if (t == "qv") r.qv = v.toDouble();
          });
      return true;
    }
    if (tag == "ConductorProps") {
      readRecords(p.conductorProps, count, "<EndConductor>", next,
          [](FemmConductorProp& r, const QString& t, const QString& v) {
            if (t == "ConductorName") r.name = unquote(v);
            else if (t == "Vc") r.valueRe = v.toDouble();
            else if (t == "qc") r.fluxRe = v.toDouble();
            else if (t == "ConductorType") r.conductorType = v.toInt();
          });
      return true;
    }
    return false;

  // --- Heat flow (.feh) ----------------------------------------------------
  case FemmProblemKind::HeatFlow:
    if (tag == "PointProps") {
      readRecords(p.htPointProps, count, "<EndPoint>", next,
          [](FemmHtPointProp& r, const QString& t, const QString& v) {
            if (t == "PointName") r.name = unquote(v);
            else if (t == "Tp") r.Tp = v.toDouble();
            else if (t == "qp") r.qp = v.toDouble();
          });
      return true;
    }
    if (tag == "BdryProps") {
      readRecords(p.htBoundaryProps, count, "<EndBdry>", next,
          [](FemmHtBoundaryProp& r, const QString& t, const QString& v) {
            if (t == "BdryName") r.name = unquote(v);
            else if (t == "BdryType") r.bdryFormat = v.toInt();
            else if (t == "Tset") r.Tset = v.toDouble();
            else if (t == "qs") r.qs = v.toDouble();
            else if (t == "beta") r.beta = v.toDouble();
            else if (t == "h") r.h = v.toDouble();
            else if (t == "Tinf") r.Tinf = v.toDouble();
            else if (t == "TinfRad") r.TinfRad = v.toDouble();
          });
      return true;
    }
    if (tag == "BlockProps") {
      readRecords(p.htMaterialProps, count, "<EndBlock>", next,
          [&next](FemmHtMaterialProp& r, const QString& t, const QString& v) {
            if (t == "BlockName") r.name = unquote(v);
            else if (t == "Kx") r.Kx = v.toDouble();
            else if (t == "Ky") r.Ky = v.toDouble();
            else if (t == "Kt") r.Kt = v.toDouble();
            else if (t == "qv") r.qv = v.toDouble();
            else if (t == "TKPoints") readCurve(r.tkData, v.toInt(), next);
          });
      return true;
    }
    if (tag == "ConductorProps") {
      readRecords(p.conductorProps, count, "<EndConductor>", next,
          [](FemmConductorProp& r, const QString& t, const QString& v) {
            if (t == "ConductorName") r.name = unquote(v);
            else if (t == "Tc") r.valueRe = v.toDouble();
            else if (t == "qc") r.fluxRe = v.toDouble();
            else if (t == "ConductorType") r.conductorType = v.toInt();
          });
      return true;
    }
    return false;

  // --- Current flow (.fec) -------------------------------------------------
  case FemmProblemKind::CurrentFlow:
    if (tag == "PointProps") {
      readRecords(p.cfPointProps, count, "<EndPoint>", next,
          [](FemmCfPointProp& r, const QString& t, const QString& v) {
            if (t == "PointName") r.name = unquote(v);
            else if (t == "vpr") r.vpr = v.toDouble();
            else if (t == "vpi") r.vpi = v.toDouble();
            else if (t == "qpr") r.qpr = v.toDouble();
            else if (t == "qpi") r.qpi = v.toDouble();
          });
      return true;
    }
    if (tag == "BdryProps") {
      readRecords(p.cfBoundaryProps, count, "<EndBdry>", next,
          [](FemmCfBoundaryProp& r, const QString& t, const QString& v) {
            if (t == "BdryName") r.name = unquote(v);
            else if (t == "BdryType") r.bdryFormat = v.toInt();
            else if (t == "vsr") r.vsr = v.toDouble();
            else if (t == "vsi") r.vsi = v.toDouble();
            else if (t == "qsr") r.qsr = v.toDouble();
            else if (t == "qsi") r.qsi = v.toDouble();
            else if (t == "c0r") r.c0r = v.toDouble();
            else if (t == "c0i") r.c0i = v.toDouble();
            else if (t == "c1r") r.c1r = v.toDouble();
            else if (t == "c1i") r.c1i = v.toDouble();
          });
      return true;
    }
    if (tag == "BlockProps") {
      readRecords(p.cfMaterialProps, count, "<EndBlock>", next,
          [](FemmCfMaterialProp& r, const QString& t, const QString& v) {
            if (t == "BlockName") r.name = unquote(v);
            else if (t == "ox") r.ox = v.toDouble();
            else if (t == "oy") r.oy = v.toDouble();
            else if (t == "ex") r.ex = v.toDouble();
            else if (t == "ey") r.ey = v.toDouble();
            else if (t == "ltx") r.ltx = v.toDouble();
            else if (t == "lty") r.lty = v.toDouble();
          });
      return true;
    }
    if (tag == "ConductorProps") {
      readRecords(p.conductorProps, count, "<EndConductor>", next,
          [](FemmConductorProp& r, const QString& t, const QString& v) {
            if (t == "ConductorName") r.name = unquote(v);
            else if (t == "vcr") r.valueRe = v.toDouble();
            else if (t == "vci") r.valueIm = v.toDouble();
            else if (t == "qcr") r.fluxRe = v.toDouble();
            else if (t == "qci") r.fluxIm = v.toDouble();
            else if (t == "ConductorType") r.conductorType = v.toInt();
          });
      return true;
    }
    return false;
  }
  return false;
}

bool PropertyCodec::readKindScalar(FemmProblem& p, const QString& tag, const QString& value)
{
  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    if (tag == "Frequency") { p.frequency = value.toDouble(); return true; }
    if (tag == "ACSolver") { p.acSolver = value.toInt(); return true; }
    if (tag == "PrevType") { p.prevType = value.toInt(); return true; }
    if (tag == "PrevSoln") { p.prevSoln = unquote(value); return true; }
    return false;
  case FemmProblemKind::CurrentFlow:
    // Current flow is solved at a frequency too -- it is the only other
    // one of the four that is, which is why its properties are complex.
    if (tag == "Frequency") { p.frequency = value.toDouble(); return true; }
    return false;
  case FemmProblemKind::HeatFlow:
    if (tag == "dT") { p.heatTimeStep = value.toDouble(); return true; }
    if (tag == "PrevSoln") { p.prevSoln = unquote(value); return true; }
    return false;
  case FemmProblemKind::Electrostatics:
    return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void PropertyCodec::writeKindScalars(const FemmProblem& p, QTextStream& out)
{
  switch (p.kind) {
  case FemmProblemKind::Magnetics:
    out << "[Frequency] = " << g17(p.frequency) << "\n";
    out << "[ACSolver] = " << p.acSolver << "\n";
    out << "[PrevType] = " << p.prevType << "\n";
    out << "[PrevSoln] = \"" << p.prevSoln << "\"\n";
    return;
  case FemmProblemKind::CurrentFlow:
    out << "[Frequency] = " << g17(p.frequency) << "\n";
    return;
  case FemmProblemKind::HeatFlow:
    out << "[dT] = " << g17(p.heatTimeStep) << "\n";
    out << "[PrevSoln] = \"" << p.prevSoln << "\"\n";
    return;
  case FemmProblemKind::Electrostatics:
    return;
  }
}

void PropertyCodec::writeSections(const FemmProblem& p, QTextStream& out)
{
  switch (p.kind) {

  case FemmProblemKind::Magnetics:
    out << "[PointProps] = " << p.pointProps.size() << "\n";
    for (const FemmPointProp& r : p.pointProps) {
      out << "  <BeginPoint>\n";
      tagQ(out, "PointName", r.name);
      tagD(out, "I_re", r.Jr);
      tagD(out, "I_im", r.Ji);
      tagD(out, "A_re", r.Ar);
      tagD(out, "A_im", r.Ai);
      out << "  <EndPoint>\n";
    }
    out << "[BdryProps] = " << p.boundaryProps.size() << "\n";
    for (const FemmBoundaryProp& r : p.boundaryProps) {
      out << "  <BeginBdry>\n";
      tagQ(out, "BdryName", r.name);
      tagI(out, "BdryType", r.bdryFormat);
      tagD(out, "A_0", r.A0);
      tagD(out, "A_1", r.A1);
      tagD(out, "A_2", r.A2);
      tagD(out, "Phi", r.phi);
      tagD(out, "c0", r.c0re);
      tagD(out, "c0i", r.c0im);
      tagD(out, "c1", r.c1re);
      tagD(out, "c1i", r.c1im);
      tagD(out, "Mu_ssd", r.muSsd);
      tagD(out, "Sigma_ssd", r.sigmaSsd);
      tagD(out, "innerangle", r.innerAngle);
      tagD(out, "outerangle", r.outerAngle);
      out << "  <EndBdry>\n";
    }
    out << "[BlockProps] = " << p.materialProps.size() << "\n";
    for (const FemmMaterialProp& r : p.materialProps) {
      out << "  <BeginBlock>\n";
      tagQ(out, "BlockName", r.name);
      tagD(out, "Mu_x", r.muX);
      tagD(out, "Mu_y", r.muY);
      tagD(out, "H_c", r.Hc);
      tagD(out, "H_cAngle", r.HcAngle);
      tagD(out, "J_re", r.JsrcRe);
      tagD(out, "J_im", r.JsrcIm);
      tagD(out, "Sigma", r.sigma);
      tagD(out, "d_lam", r.dLam);
      tagD(out, "Phi_h", r.phiH);
      tagD(out, "Phi_hx", r.phiHx);
      tagD(out, "Phi_hy", r.phiHy);
      tagI(out, "LamType", r.lamType);
      tagD(out, "LamFill", r.lamFill);
      tagI(out, "NStrands", r.nStrands);
      tagD(out, "WireD", r.wireD);
      tagI(out, "BHPoints", r.bhData.size());
      writeCurve(out, r.bhData);
      out << "  <EndBlock>\n";
    }
    out << "[CircuitProps] = " << p.circuitProps.size() << "\n";
    for (const FemmCircuitProp& r : p.circuitProps) {
      out << "  <BeginCircuit>\n";
      tagQ(out, "CircuitName", r.name);
      tagD(out, "TotalAmps_re", r.ampsRe);
      tagD(out, "TotalAmps_im", r.ampsIm);
      tagI(out, "CircuitType", r.circType);
      if (r.voltGradientRe != 0 || r.voltGradientIm != 0) {
        tagD(out, "VoltGradient_re", r.voltGradientRe);
        tagD(out, "VoltGradient_im", r.voltGradientIm);
      }
      out << "  <EndCircuit>\n";
    }
    return;

  case FemmProblemKind::Electrostatics:
    out << "[PointProps] = " << p.esPointProps.size() << "\n";
    for (const FemmEsPointProp& r : p.esPointProps) {
      out << "  <BeginPoint>\n";
      tagQ(out, "PointName", r.name);
      tagD(out, "Vp", r.Vp);
      tagD(out, "qp", r.qp);
      out << "  <EndPoint>\n";
    }
    out << "[BdryProps] = " << p.esBoundaryProps.size() << "\n";
    for (const FemmEsBoundaryProp& r : p.esBoundaryProps) {
      out << "  <BeginBdry>\n";
      tagQ(out, "BdryName", r.name);
      tagI(out, "BdryType", r.bdryFormat);
      tagD(out, "Vs", r.Vs);
      tagD(out, "qs", r.qs);
      tagD(out, "c0", r.c0);
      tagD(out, "c1", r.c1);
      out << "  <EndBdry>\n";
    }
    out << "[BlockProps] = " << p.esMaterialProps.size() << "\n";
    for (const FemmEsMaterialProp& r : p.esMaterialProps) {
      out << "  <BeginBlock>\n";
      tagQ(out, "BlockName", r.name);
      tagD(out, "ex", r.ex);
      tagD(out, "ey", r.ey);
      tagD(out, "qv", r.qv);
      out << "  <EndBlock>\n";
    }
    out << "[ConductorProps] = " << p.conductorProps.size() << "\n";
    for (const FemmConductorProp& r : p.conductorProps) {
      out << "  <BeginConductor>\n";
      tagQ(out, "ConductorName", r.name);
      tagD(out, "Vc", r.valueRe);
      tagD(out, "qc", r.fluxRe);
      tagI(out, "ConductorType", r.conductorType);
      out << "  <EndConductor>\n";
    }
    return;

  case FemmProblemKind::HeatFlow:
    out << "[PointProps] = " << p.htPointProps.size() << "\n";
    for (const FemmHtPointProp& r : p.htPointProps) {
      out << "  <BeginPoint>\n";
      tagQ(out, "PointName", r.name);
      tagD(out, "Tp", r.Tp);
      tagD(out, "qp", r.qp);
      out << "  <EndPoint>\n";
    }
    out << "[BdryProps] = " << p.htBoundaryProps.size() << "\n";
    for (const FemmHtBoundaryProp& r : p.htBoundaryProps) {
      out << "  <BeginBdry>\n";
      tagQ(out, "BdryName", r.name);
      tagI(out, "BdryType", r.bdryFormat);
      tagD(out, "Tset", r.Tset);
      tagD(out, "qs", r.qs);
      tagD(out, "beta", r.beta);
      tagD(out, "h", r.h);
      tagD(out, "Tinf", r.Tinf);
      tagD(out, "TinfRad", r.TinfRad);
      out << "  <EndBdry>\n";
    }
    out << "[BlockProps] = " << p.htMaterialProps.size() << "\n";
    for (const FemmHtMaterialProp& r : p.htMaterialProps) {
      out << "  <BeginBlock>\n";
      tagQ(out, "BlockName", r.name);
      tagD(out, "Kx", r.Kx);
      tagD(out, "Ky", r.Ky);
      tagD(out, "Kt", r.Kt);
      tagD(out, "qv", r.qv);
      tagI(out, "TKPoints", r.tkData.size());
      writeCurve(out, r.tkData);
      out << "  <EndBlock>\n";
    }
    out << "[ConductorProps] = " << p.conductorProps.size() << "\n";
    for (const FemmConductorProp& r : p.conductorProps) {
      out << "  <BeginConductor>\n";
      tagQ(out, "ConductorName", r.name);
      tagD(out, "Tc", r.valueRe);
      tagD(out, "qc", r.fluxRe);
      tagI(out, "ConductorType", r.conductorType);
      out << "  <EndConductor>\n";
    }
    return;

  case FemmProblemKind::CurrentFlow:
    out << "[PointProps] = " << p.cfPointProps.size() << "\n";
    for (const FemmCfPointProp& r : p.cfPointProps) {
      out << "  <BeginPoint>\n";
      tagQ(out, "PointName", r.name);
      tagD(out, "vpr", r.vpr);
      tagD(out, "vpi", r.vpi);
      tagD(out, "qpr", r.qpr);
      tagD(out, "qpi", r.qpi);
      out << "  <EndPoint>\n";
    }
    out << "[BdryProps] = " << p.cfBoundaryProps.size() << "\n";
    for (const FemmCfBoundaryProp& r : p.cfBoundaryProps) {
      out << "  <BeginBdry>\n";
      tagQ(out, "BdryName", r.name);
      tagI(out, "BdryType", r.bdryFormat);
      tagD(out, "vsr", r.vsr);
      tagD(out, "vsi", r.vsi);
      tagD(out, "qsr", r.qsr);
      tagD(out, "qsi", r.qsi);
      tagD(out, "c0r", r.c0r);
      tagD(out, "c0i", r.c0i);
      tagD(out, "c1r", r.c1r);
      tagD(out, "c1i", r.c1i);
      out << "  <EndBdry>\n";
    }
    out << "[BlockProps] = " << p.cfMaterialProps.size() << "\n";
    for (const FemmCfMaterialProp& r : p.cfMaterialProps) {
      out << "  <BeginBlock>\n";
      tagQ(out, "BlockName", r.name);
      tagD(out, "ox", r.ox);
      tagD(out, "oy", r.oy);
      tagD(out, "ex", r.ex);
      tagD(out, "ey", r.ey);
      tagD(out, "ltx", r.ltx);
      tagD(out, "lty", r.lty);
      out << "  <EndBlock>\n";
    }
    out << "[ConductorProps] = " << p.conductorProps.size() << "\n";
    for (const FemmConductorProp& r : p.conductorProps) {
      out << "  <BeginConductor>\n";
      tagQ(out, "ConductorName", r.name);
      tagD(out, "vcr", r.valueRe);
      tagD(out, "vci", r.valueIm);
      tagD(out, "qcr", r.fluxRe);
      tagD(out, "qci", r.fluxIm);
      tagI(out, "ConductorType", r.conductorType);
      out << "  <EndConductor>\n";
    }
    return;
  }
}
