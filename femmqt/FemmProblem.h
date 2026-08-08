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
  bool isSelected = false;
};

struct FemmSegment {
  int n0 = 0, n1 = 0;
  double maxSideLength = -1; // -1 = <No Mesh Constraint>
  int boundaryMarker = 0;
  bool hidden = false;
  int inGroup = 0;
  bool isSelected = false;
};

struct FemmArcSegment {
  int n0 = 0, n1 = 0;
  double arcLength = 0; // included angle, degrees, n0 -> n1 counterclockwise
  double maxSideLength = 1; // max degrees per mesh element side
  int boundaryMarker = 0;
  bool hidden = false;
  int inGroup = 0;
  double mySideLength = 1;
  bool isSelected = false;
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
};

// Meaning of refA/refB/refC depends on `type`:
//   Distance/HorizontalDistance/VerticalDistance: refA, refB = node indices
//   Radius:   refA = arc index
//   Angle:    refA = vertex node index, refB/refC = the two ray-endpoint
//             node indices
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
  QString comment;

  QVector<FemmPointProp> pointProps;
  QVector<FemmBoundaryProp> boundaryProps;
  QVector<FemmMaterialProp> materialProps;
  QVector<FemmCircuitProp> circuitProps;

  QVector<FemmNode> nodes;
  QVector<FemmSegment> segments;
  QVector<FemmArcSegment> arcSegments;
  QVector<FemmBlockLabel> blockLabels; // includes holes (blockTypeIndex < 0)

  // In-session-only drawing aid -- see ConstraintType's own comment.
  // Never read/written by FemmFileIO.cpp or FemxFileIO.cpp.
  QVector<FemmConstraint> constraints;
  QVector<FemmDimension> dimensions;
};
