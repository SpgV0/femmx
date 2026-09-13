#pragma once

#include <QString>
#include <QVector>

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-19:
// plain Qt/STL-only mirror of the magnetics-relevant subset of
// femm/Problem.h's classes (CNode, CSegment, CArcSegment, CBlockLabel,
// CMaterialProp, CBoundaryProp, CPointProp, CCircuit) plus femm/FemmeDoc.h's
// top-level scalar problem fields. Deliberately NOT a reuse of the MFC
// types (CArray/CString/CComplex) -- see the plan this was built from for
// why: femmqt is a second, independent GUI toolkit and should not pull in
// MFC. Field names/semantics match the .fem text format exactly (see
// FemmFileIO::readFem), which is the authoritative, shared contract with
// femmx.exe, fkn.exe and triangle.exe -- not this struct's own layout.
//
// Modified by Claude (Anthropic), noreply@anthropic.com: heat-flow (Round
// 6) support was added and then removed again per direct user request
// ("remove all the functionality regarding thermal problems in qt and
// revert back to magnetics") -- femmqt is magnetics-only again, matching
// the classic GUI's own scope for this app.

struct FemmPointProp {
  QString name;
  double Jr = 0, Ji = 0; // applied point current, A
  double Ar = 0, Ai = 0; // prescribed nodal value
};

struct FemmBoundaryProp {
  QString name;
  int bdryFormat = 0; // 0 = fixed A, 1 = small skin depth, 2 = mixed
  double A0 = 0, A1 = 0, A2 = 0, phi = 0;
  double c0re = 0, c0im = 0, c1re = 0, c1im = 0;
  double muSsd = 0, sigmaSsd = 0;
  double innerAngle = 0, outerAngle = 0;
};

struct FemmMaterialProp {
  QString name;
  double muX = 1, muY = 1;
  double Hc = 0, HcAngle = 0;
  double JsrcRe = 0, JsrcIm = 0;
  double sigma = 0; // conductivity, MS/m
  double dLam = 0; // lamination thickness, mm
  double phiH = 0, phiHx = 0, phiHy = 0; // hysteresis angles, degrees
  int lamType = 0;
  double lamFill = 1;
  int nStrands = 0;
  double wireD = 0;
  // BH curve points, only meaningful if non-empty (linear material
  // otherwise, using muX/muY directly) -- Phase 1 reads/preserves these on
  // round-trip but the material dialog doesn't expose editing them yet.
  QVector<QPair<double, double>> bhData;
};

struct FemmCircuitProp {
  QString name;
  double ampsRe = 0, ampsIm = 0;
  int circType = 0; // 0 = parallel, 1 = series
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
  // (the <voltgradient> ticket). fkn reads <VoltGradient_re>/<_im> into
  // CCircuit::dVolts_re/im and drives a circuit at a prescribed voltage
  // gradient with them (fkn/femmedoccore.cpp:965, prob1big.cpp:97 and
  // its three siblings). Neither GUI wrote them or parsed them, so a
  // hand-authored or third-party .fem carrying a voltage-driven circuit
  // lost it silently on the next save.
  //
  // Carried here so it round-trips. NOT currently reachable in the
  // solver: LoadCircuits rewrites every CircType 1 to 0 before the solve
  // (fkn/femmedoccore.cpp:1100), so the branch that reads dVolts never
  // runs. Preserving the value is still right -- losing a field a user
  // deliberately set is a bug whether or not the solver acts on it yet.
  double voltGradientRe = 0, voltGradientIm = 0;
};

// ---------------------------------------------------------------------------
// The four problem kinds (issue #80)
// ---------------------------------------------------------------------------
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13.
//
// FEMM has four physics, four file formats and four solvers that all
// already ship. They share an identical geometry skeleton -- [NumPoints],
// [NumSegments], [NumArcSegments], [NumBlockLabels] and the
// <BeginPoint>/<BeginBdry>/<BeginBlock> record structure are the same in
// all four -- and differ only in the PROPERTY payloads. That is why the
// CAD layer (GeometryScene, ConstraintSolver, dimensions, snapping)
// transfers to all four for free: it touches nodes, segments, arcs and
// block labels and never touches a property.
//
// A document is ONE kind, chosen at New and inferred from the extension
// at Open, exactly like the classic GUI and exactly like the formats.
// This is deliberately not the reverted thermal attempt (8e88951), which
// gave every entity a parallel index per physics; that does not map onto
// any of the on-disk formats, where there is exactly one [PointProps],
// one [BdryProps], one [BlockProps] and one source list per file.
enum class FemmProblemKind {
  Magnetics,      // .fem  -> fkn.exe
  Electrostatics, // .fee  -> belasolv.exe
  HeatFlow,       // .feh  -> hsolv.exe
  CurrentFlow,    // .fec  -> csolv.exe
};

// Field names below are the tags each format actually uses, taken from
// the four classic writers (femm/FemmeDoc.cpp, beladrawDoc.cpp,
// hdrawDoc.cpp, cdrawDoc.cpp) rather than from memory. The tags are the
// contract with the solvers, so a rename here is a file-format change.

// --- Electrostatics (.fee) -------------------------------------------------

struct FemmEsPointProp {
  QString name; // <PointName>
  double Vp = 0; // <Vp>, prescribed potential, V
  double qp = 0; // <qp>, point charge density, C/m
};

struct FemmEsBoundaryProp {
  QString name; // <BdryName>
  int bdryFormat = 0; // <BdryType>
  double Vs = 0; // <Vs>, fixed voltage
  double qs = 0; // <qs>, surface charge density
  double c0 = 0, c1 = 0; // <c0>, <c1>, mixed BC coefficients
};

struct FemmEsMaterialProp {
  QString name; // <BlockName>
  double ex = 1, ey = 1; // <ex>, <ey>, relative permittivity
  double qv = 0; // <qv>, volume charge density
};

// --- Heat flow (.feh) ------------------------------------------------------

struct FemmHtPointProp {
  QString name; // <PointName>
  double Tp = 0; // <Tp>, prescribed temperature, K
  double qp = 0; // <qp>, point heat generation, W/m
};

struct FemmHtBoundaryProp {
  QString name; // <BdryName>
  int bdryFormat = 0; // <BdryType>
  double Tset = 0; // <Tset>, fixed temperature
  double qs = 0; // <qs>, heat flux
  double beta = 0; // <beta>, emissivity for radiation
  double h = 0; // <h>, convection coefficient
  double Tinf = 0; // <Tinf>, ambient temperature for convection
  double TinfRad = 0; // <TinfRad>, ambient temperature for radiation
};

struct FemmHtMaterialProp {
  QString name; // <BlockName>
  double Kx = 0, Ky = 0; // <Kx>, <Ky>, thermal conductivity, W/(m*K)
  double Kt = 0; // <Kt>, volumetric heat capacity, MJ/(m^3*K)
  double qv = 0; // <qv>, volumetric heat generation, W/m^3
  // <TKPoints>: a temperature-dependent conductivity curve, the thermal
  // counterpart of magnetics' BH curve. Empty means constant Kx/Ky.
  QVector<QPair<double, double>> tkData;
};

// --- Current flow (.fec) ---------------------------------------------------
//
// The only one of the three whose properties are COMPLEX: current flow is
// solved at a frequency, so a prescribed potential has a real and an
// imaginary part.

struct FemmCfPointProp {
  QString name; // <PointName>
  double vpr = 0, vpi = 0; // <vpr>, <vpi>, prescribed voltage
  double qpr = 0, qpi = 0; // <qpr>, <qpi>, point current
};

struct FemmCfBoundaryProp {
  QString name; // <BdryName>
  int bdryFormat = 0; // <BdryType>
  double vsr = 0, vsi = 0; // <vsr>, <vsi>, fixed voltage
  double qsr = 0, qsi = 0; // <qsr>, <qsi>, surface current density
  double c0r = 0, c0i = 0, c1r = 0, c1i = 0; // <c0r>..<c1i>, mixed BC
};

struct FemmCfMaterialProp {
  QString name; // <BlockName>
  double ox = 0, oy = 0; // <ox>, <oy>, conductivity, S/m
  double ex = 1, ey = 1; // <ex>, <ey>, relative permittivity
  double ltx = 0, lty = 0; // <ltx>, <lty>, dielectric loss tangent
};

// --- Sources ---------------------------------------------------------------
//
// Magnetics has circuits (FemmCircuitProp above); the other three have
// CONDUCTORS, which are a different thing: a circuit carries a current
// through a region, a conductor is an equipotential surface with either
// its potential or its total flux prescribed.
//
// One struct for all three rather than three near-identical ones,
// because the record genuinely is the same shape in each -- a prescribed
// value, a prescribed flux, and which of the two is set. What differs is
// what the value MEANS, and the tag it is written under, which is the
// codec's business:
//
//   kind             value            flux
//   Electrostatics   <Vc>  volts      <qc>  charge
//   HeatFlow         <Tc>  kelvin     <qc>  heat flux
//   CurrentFlow      <vcr>/<vci>      <qcr>/<qci>
//
// Only current flow uses the imaginary halves; the other two leave them
// at zero. Naming them for their ROLE rather than for one kind's tag is
// deliberate -- a field called Vc holding a temperature is exactly the
// kind of thing that misleads later.
struct FemmConductorProp {
  QString name; // <ConductorName>
  double valueRe = 0, valueIm = 0;
  double fluxRe = 0, fluxIm = 0;
  // <ConductorType>: 0 = prescribed flux, 1 = prescribed value. Matches
  // the classic dialogs' "Prescribed total charge/heat flux" vs
  // "Prescribed voltage/temperature" radio pair.
  int conductorType = 0;
};

struct FemmNode {
  double x = 0, y = 0;
  // 0 = none, else 1-based index into pointProps -- NOT boundaryProps.
  // Confirmed against femm/FemmeDoc.cpp's .fem writer (OnSaveDocument):
  // this column is produced by matching against nodeproplist[j].PointName,
  // not lineproplist[j].BdryName like segments'/arcs' own boundaryMarker
  // below -- individual nodes only ever carry a point property (nodal
  // current source / prescribed A) in classic FEMM, never a standalone
  // boundary condition (that's implicit from whichever segment/arc the
  // node happens to touch). Named differently from FemmSegment/
  // FemmArcSegment's boundaryMarker on purpose so this distinction can't
  // be missed again.
  int pointPropIndex = 0;
  int inGroup = 0;
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #80). 0 = none, else 1-based into FemmProblem::conductorProps.
  //
  // Electrostatics, heat flow and current flow attach a conductor to
  // NODES, SEGMENTS and ARCS -- their .fee/.feh/.fec rows carry an extra
  // column for it that the magnetics .fem row does not have. Magnetics'
  // equivalent, a circuit, attaches to BLOCK LABELS instead
  // (FemmBlockLabel::circuitIndex), because a circuit carries current
  // through a region while a conductor is an equipotential surface.
  //
  // This is the one place the four formats' geometry sections genuinely
  // differ; #79 described the skeleton as identical, and it is identical
  // apart from this column and the block-label row. Unused and left at 0
  // for magnetics. The CAD layer never reads it, so the sketch layer
  // stays type-blind either way.
  int conductorIndex = 0;
  bool isSelected = false;
  // See FemmSegment::isConstruction (issue #31). A node is only dropped
  // from the exported .fem if nothing surviving still references it: a
  // node shared between a centreline and a real edge has to be written,
  // or the real edge loses an endpoint.
  bool isConstruction = false;
};

struct FemmSegment {
  int n0 = 0, n1 = 0;
  double maxSideLength = -1; // -1 = <No Mesh Constraint>
  int boundaryMarker = 0;
  bool hidden = false;
  int inGroup = 0;
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #80). 0 = none, else 1-based into FemmProblem::conductorProps.
  //
  // Electrostatics, heat flow and current flow attach a conductor to
  // NODES, SEGMENTS and ARCS -- their .fee/.feh/.fec rows carry an extra
  // column for it that the magnetics .fem row does not have. Magnetics'
  // equivalent, a circuit, attaches to BLOCK LABELS instead
  // (FemmBlockLabel::circuitIndex), because a circuit carries current
  // through a region while a conductor is an equipotential surface.
  //
  // This is the one place the four formats' geometry sections genuinely
  // differ; #79 described the skeleton as identical, and it is identical
  // apart from this column and the block-label row. Unused and left at 0
  // for magnetics. The CAD layer never reads it, so the sketch layer
  // stays type-blind either way.
  int conductorIndex = 0;
  bool isSelected = false;
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
  // (issue #31): construction geometry -- a centreline, a bolt circle, a
  // reference rectangle. It exists to be constrained and dimensioned
  // against and NEVER reaches the mesher or the solver: it is excluded
  // from the .fem and the .femx, and lives only in the .fes sidecar.
  //
  // A flag rather than a separate list, because constraints and
  // dimensions reference geometry by index into these lists -- that is
  // the whole point of the feature, and a parallel list would mean a
  // second index space for every reference to be ambiguous between.
  bool isConstruction = false;
};

struct FemmArcSegment {
  int n0 = 0, n1 = 0;
  double arcLength = 0; // included angle, degrees, n0 -> n1 counterclockwise
  double maxSideLength = 1; // max degrees per mesh element side
  int boundaryMarker = 0;
  bool hidden = false;
  int inGroup = 0;
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #80). 0 = none, else 1-based into FemmProblem::conductorProps.
  //
  // Electrostatics, heat flow and current flow attach a conductor to
  // NODES, SEGMENTS and ARCS -- their .fee/.feh/.fec rows carry an extra
  // column for it that the magnetics .fem row does not have. Magnetics'
  // equivalent, a circuit, attaches to BLOCK LABELS instead
  // (FemmBlockLabel::circuitIndex), because a circuit carries current
  // through a region while a conductor is an equipotential surface.
  //
  // This is the one place the four formats' geometry sections genuinely
  // differ; #79 described the skeleton as identical, and it is identical
  // apart from this column and the block-label row. Unused and left at 0
  // for magnetics. The CAD layer never reads it, so the sketch layer
  // stays type-blind either way.
  int conductorIndex = 0;
  double mySideLength = 1;
  bool isSelected = false;
  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
  // (issue #31): construction geometry -- a centreline, a bolt circle, a
  // reference rectangle. It exists to be constrained and dimensioned
  // against and NEVER reaches the mesher or the solver: it is excluded
  // from the .fem and the .femx, and lives only in the .fes sidecar.
  //
  // A flag rather than a separate list, because constraints and
  // dimensions reference geometry by index into these lists -- that is
  // the whole point of the feature, and a parallel list would mean a
  // second index space for every reference to be ambiguous between.
  bool isConstruction = false;
};

// A block label with blockTypeIndex < 0 is a hole ("<No Mesh>" in the .fem
// text format) -- everything else (material/circuit/turns/etc.) is
// meaningless for a hole and left at its default.
struct FemmBlockLabel {
  double x = 0, y = 0;
  int blockTypeIndex = -1; // -1 = hole, else 1-based index into materialProps
  double maxArea = 0; // mesh triangle area constraint, 0 = <No Mesh Constraint>
  int circuitIndex = 0; // 0 = none, else 1-based index into circuitProps
  double magDir = 0; // degrees
  QString magDirFctn; // custom Lua expression, usually empty
  int inGroup = 0;
  int turns = 1;
  bool isExternal = false;
  bool isDefault = false;
  bool isSelected = false;
};

enum class FemmLengthUnits {
  Inches = 0,
  Millimeters = 1,
  Centimeters = 2,
  Meters = 3,
  Mils = 4,
  Microns = 5,
};

// Metres per model unit. Added by Claude (Anthropic),
// noreply@anthropic.com, 2026-09-13 (issue #83): every derived field
// quantity needs this, and there were already three separate copies of
// it (AnsFileIO's table, CircuitAnalysis's function, SolutionView's
// function). Placed beside the enum it describes so the next one has an
// obvious home rather than a fourth copy.
inline double lengthToMeters(FemmLengthUnits u)
{
  switch (u) {
  case FemmLengthUnits::Inches: return 0.0254;
  case FemmLengthUnits::Millimeters: return 0.001;
  case FemmLengthUnits::Centimeters: return 0.01;
  case FemmLengthUnits::Meters: return 1.0;
  case FemmLengthUnits::Mils: return 2.54e-05;
  case FemmLengthUnits::Microns: return 1.0e-06;
  }
  return 1.0;
}

enum class FemmCoordinateType {
  Planar = 0,
  Axisymmetric = 1,
};

// Modified by Claude (Anthropic), noreply@anthropic.com: CAD-style
// geometric constraints and dimensions -- per direct user request
// ("add dimensions when drawings and constraints similar to modern
// cad"), scoped to an IN-SESSION drawing aid only, not a persisted file
// format extension ("I just want the drawing capability" -- explicit
// correction ruling out extending .fem/.femx). These two lists follow
// the exact precedent FemmNode/FemmSegment/FemmArcSegment/
// FemmBlockLabel's own `isSelected` field already sets: a real
// FemmProblem field that FemmFileIO.cpp/FemxFileIO.cpp's explicit,
// non-reflective per-field tag parsers simply never read or write, so
// adding them is safe by construction and needs no changes to either
// file. The *result* of using a constraint/dimension (the node/segment/
// arc coordinates it drove) is ordinary geometry and saves normally;
// only the abstract relationship itself is session-transient, lost the
// same way an unsaved selection is.
enum class ConstraintType {
  Coincident,
  Horizontal,
  Vertical,
  Parallel,
  Perpendicular,
  Equal,
  Tangent,
  Concentric,
  Symmetric,
};

// Meaning of refA/refB/refC depends on `type`:
//   Coincident:    refA, refB = node indices
//   Horizontal/Vertical: refA = segment index
//   Parallel/Perpendicular: refA, refB = segment indices
//   Equal:         refA, refB = segment indices, OR arc indices (both
//                  same kind) -- disambiguated by isArcPair
//   Tangent:       refA = segment OR arc index, refB = arc index,
//                  disambiguated by firstIsArc
//   Concentric:    refA, refB = arc indices
//   Symmetric:     refA, refB = node indices, refC = segment index
//                  (the mirror line)
struct FemmConstraint {
  ConstraintType type = ConstraintType::Coincident;
  int refA = -1, refB = -1, refC = -1;
  bool isArcPair = false; // Equal only: refA/refB are arc, not segment, indices
  bool firstIsArc = false; // Tangent only: refA is an arc, not a segment, index
};

// Modified by Claude (Anthropic), noreply@anthropic.com: HorizontalDistance/
// VerticalDistance added per direct user request ("implement according to"
// a supplied Fusion 360 Sketch Dimension reference) -- Fusion's own doc
// treats these as genuinely distinct CONSTRAINTS from Distance ("A
// horizontal dimension of 50 mm... introduces |x2-x1| = 50 mm", separate
// from the Euclidean |P2-P1| = 50 mm a plain Distance dimension enforces),
// not just a different label on the same equation -- see
// ConstraintSolver.cpp's residualHorizontalDistance/residualVerticalDistance
// for why reusing residualDistance's hypot() formula would have been
// silently wrong for any non-axis-aligned pair of points.
enum class DimensionType {
  Distance,
  HorizontalDistance,
  VerticalDistance,
  Radius,
  Angle,
  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
  // report that "the angle tool does not always work well", asking for
  // Fusion 360's behaviour. Angle above is defined as vertex + two ray
  // endpoints, so it can only describe two lines that MEET at a shared
  // node -- angling two lines that do not touch had nowhere to put the
  // vertex and silently did nothing. Fusion dimensions those against the
  // lines' virtual intersection, which needs no vertex at all: the angle
  // between two lines is a function of their DIRECTIONS alone. So this
  // variant references the two segments directly, which also makes its
  // solver residual simpler than the 3-node one rather than harder.
  AngleLines,
};

// Meaning of refA/refB/refC depends on `type`:
//   Distance/HorizontalDistance/VerticalDistance: refA, refB = node indices
//   Radius:   refA = arc index
//   Angle:    refA = vertex node index, refB/refC = the two ray-endpoint
//             node indices
//   AngleLines: refA, refB = segment indices; refC unused. Measured at the
//             two lines' intersection, real or virtual.
struct FemmDimension {
  DimensionType type = DimensionType::Distance;
  int refA = -1, refB = -1, refC = -1;
  double value = 0; // target value (mm/deg per lengthUnits) -- editing
                     // this drives the constraint solve
  double labelOffsetX = 0, labelOffsetY = 0; // where the dimension
                                              // line/text is drawn
};

struct FemmProblem {
  double frequency = 0;
  double precision = 1e-8;
  double minAngle = 30;
  bool smartMesh = true;
  double depth = 1;
  // Defaults to Millimeters (SI), per direct user request -- previously
  // Inches, matching the pre-fork FEMM 4.2 default.
  FemmLengthUnits lengthUnits = FemmLengthUnits::Millimeters;
  FemmCoordinateType problemType = FemmCoordinateType::Planar;
  bool coordsPolar = false;
  double extZo = 0, extRo = 0, extRi = 0; // axisymmetric external region, optional
  int acSolver = 0;
  // Mirrors femm/FemmeDoc.cpp's identical reasoning: only default GPU
  // acceleration on when femmqt itself was built alongside CUDA-enabled
  // solvers (see femmqt/CMakeLists.txt's FEMM_CUDA_ENABLED block) -- a
  // plain build stays off by default, since flipping this unconditionally
  // would pop up fkn.exe's "built without CUDA support" dialog on every
  // single solve for a machine that never asked for GPU acceleration.
#ifdef FEMM_CUDA_ENABLED
  int gpuAccel = 1;
#else
  int gpuAccel = 0;
#endif
  int prevType = 0;
  QString prevSoln;
  // <dT>, heat flow only: the time step hsolv uses for a transient
  // run. Read and written so a .feh round-trips; femmqt does not
  // expose it, since transient thermal is not in scope here.
  double heatTimeStep = 0;
  QString comment;

  // Which physics this document is (issue #80). Defaults to Magnetics so
  // every existing model, and every code path that has not been taught
  // about the others yet, behaves exactly as before.
  FemmProblemKind kind = FemmProblemKind::Magnetics;

  // Properties, one set per kind. Only the set matching `kind` is
  // populated -- an empty QVector costs nothing, and keeping them as
  // separate typed lists means each field keeps the name its own file
  // format uses instead of a union of four vocabularies.
  //
  // The per-ENTITY indices (FemmNode::pointPropIndex,
  // FemmSegment::boundaryMarker, FemmBlockLabel::blockTypeIndex and the
  // circuit/conductor index) are NOT duplicated per kind: each file has
  // exactly one of each list, so an entity has exactly one of each
  // reference. Only what the index points AT changes with the kind.
  QVector<FemmPointProp> pointProps;         // Magnetics
  QVector<FemmBoundaryProp> boundaryProps;   // Magnetics
  QVector<FemmMaterialProp> materialProps;   // Magnetics
  QVector<FemmCircuitProp> circuitProps;     // Magnetics

  QVector<FemmEsPointProp> esPointProps;
  QVector<FemmEsBoundaryProp> esBoundaryProps;
  QVector<FemmEsMaterialProp> esMaterialProps;

  QVector<FemmHtPointProp> htPointProps;
  QVector<FemmHtBoundaryProp> htBoundaryProps;
  QVector<FemmHtMaterialProp> htMaterialProps;

  QVector<FemmCfPointProp> cfPointProps;
  QVector<FemmCfBoundaryProp> cfBoundaryProps;
  QVector<FemmCfMaterialProp> cfMaterialProps;

  // Electrostatics, heat flow and current flow all use conductors where
  // magnetics uses circuits -- see FemmConductorProp.
  QVector<FemmConductorProp> conductorProps;

  QVector<FemmNode> nodes;
  QVector<FemmSegment> segments;
  QVector<FemmArcSegment> arcSegments;
  QVector<FemmBlockLabel> blockLabels; // includes holes (blockTypeIndex < 0)

  // In-session-only drawing aid -- see ConstraintType's own comment.
  // Never read/written by FemmFileIO.cpp or FemxFileIO.cpp.
  QVector<FemmConstraint> constraints;
  QVector<FemmDimension> dimensions;
};
