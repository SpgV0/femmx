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

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
// direct user request, one unified material carries BOTH magnetics and
// heat-flow properties together -- a physical material (e.g. "Copper")
// really does have both an electrical/magnetic character and a thermal
// one, so femmqt models it as one entry rather than two independently-
// assigned ones. A material sourced from only one side (e.g. a plain
// magnetics .fem with no paired .feh, or a matlib.dat entry with no
// heatlib.dat counterpart -- see MaterialLibraryIO's merge) simply leaves
// the other side's fields at 0 ("empty"/not specified), not a fabricated
// default -- deliberately 0 rather than e.g. Kx=Ky=1, so a blank thermal
// side is visually obvious in the dialog rather than looking like real
// data. FemmBlockLabel::blockTypeIndex is the single index into this
// list used by both physics types (see that field's own comment).
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

  // Heat-flow side, field names/semantics confirmed directly against
  // femm/hd_nosebl.h and femm/HDRAWDOC.CPP's .feh tag reader -- see
  // HeatFileIO.h. 0/empty (not e.g. Kx=Ky=1) means "no thermal data" --
  // see this struct's own header comment.
  double Kx = 0, Ky = 0; // thermal conductivity, W/(m*K)
  double Kt = 0; // volumetric heat capacity, MJ/(m^3*K)
  double qv = 0; // volumetric heat generation, W/m^3
  // Nonlinear k(T) curve, only meaningful if non-empty (linear material
  // otherwise, using Kx/Ky directly) -- read/written but not yet editable
  // in the material dialog, mirroring bhData exactly.
  QVector<QPair<double, double>> tkData;
};

struct FemmCircuitProp {
  QString name;
  double ampsRe = 0, ampsIm = 0;
  int circType = 0; // 0 = parallel, 1 = series
};

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: heat-
// flow counterparts to the magnetics boundary/point property structs
// above, added per direct user request to solve both magnetics and
// thermal problems on the same femmqt geometry. Field names/semantics
// confirmed directly against femm/hd_nosebl.h (hdrawdata::
// CBoundaryProp/CPointProp), femm/HDRAWDOC.CPP's .feh tag reader, and
// femm/hd_BdryDlg.cpp's boundary-type combo box -- not a re-derivation.
// Unlike materials (see FemmMaterialProp's comment, merged into one
// struct/list per direct user request), boundary conditions and point
// sources don't have a natural "same physical thing" unification --
// magnetics' fixed-A/mixed boundary and heat flow's fixed-temperature/
// convection boundary are conceptually different constraints even when
// applied to the same segment, so these stay separate structs/lists,
// matching the classic GUI's own fully separate Doc/View hierarchies per
// physics type.
struct FemmThermalBoundaryProp {
  QString name;
  // Matches hd_BdryDlg.cpp's IDC_HD_BDRYFORMAT combo box exactly:
  int bdryFormat = 0; // 0 = Fixed Temperature (uses Tset)
                       // 1 = Heat Flux (uses qs)
                       // 2 = Convection (uses qs, h, Tinf)
                       // 3 = Radiation (uses qs, h, Tinf, beta, TinfRad)
                       // 4 = Periodic, 5 = Antiperiodic -- NOT supported by
                       //     femmqt yet, same as magnetics' own periodic/
                       //     antiperiodic boundaries (see FemmBoundaryProp)
  double Tset = 0;
  double qs = 0;
  double beta = 0, h = 0, Tinf = 0, TinfRad = 0;
};

struct FemmThermalPointProp {
  QString name;
  double Tp = 0; // prescribed nodal temperature, K -- <Tp> in .feh
  double qp = 0; // point heat generation, W
};

// Heat flow's node/segment/arc-level analog to magnetics' block-level
// FemmCircuitProp ("Conductor" in the .feh format/classic UI, not
// "Circuit"). Read/written so a femmqt round-trip of a classic-GUI .feh
// doesn't silently drop a file's conductor definitions, mirroring
// FemmMaterialProp::bhData's "preserve, don't yet expose editing"
// precedent -- femmqt has no UI to create/assign these yet, and
// MeshBuilder doesn't encode the per-entity conductor index into the
// mesh (a real follow-up, see Round 6's plan for why this is harder than
// it looks: unlike circuits, conductors are node/segment-level, not
// block-label-level).
struct FemmThermalConductorProp {
  QString name;
  double Tc = 0, qc = 0;
  int circType = 0; // 0 = parallel (fixed T), 1 = series (fixed net q) -- matches FemmCircuitProp::circType
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
  // 0 = none, else 1-based index into thermalPointProps -- independent of
  // pointPropIndex above, so the same node can carry a magnetics point
  // property AND a thermal point property at once (see FemmProblem's
  // Round 6 comment on why geometry is shared between physics types).
  int thermalPointPropIndex = 0;
  int thermalConductorIndex = 0; // 0 = none, else 1-based into thermalConductorProps -- see FemmThermalConductorProp
};

struct FemmSegment {
  int n0 = 0, n1 = 0;
  double maxSideLength = -1; // -1 = <No Mesh Constraint>
  int boundaryMarker = 0;
  bool hidden = false;
  int inGroup = 0;
  bool isSelected = false;
  // 0 = none, else 1-based index into thermalBoundaryProps -- independent
  // of boundaryMarker above, same reasoning as FemmNode::thermalPointPropIndex.
  int thermalBoundaryMarker = 0;
  int thermalConductorIndex = 0; // see FemmNode::thermalConductorIndex
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
  int thermalBoundaryMarker = 0; // see FemmSegment::thermalBoundaryMarker
  int thermalConductorIndex = 0; // see FemmNode::thermalConductorIndex
};

// A block label with blockTypeIndex < 0 is a hole ("<No Mesh>" in the .fem
// text format) -- everything else (material/circuit/turns/etc.) is
// meaningless for a hole and left at its default.
struct FemmBlockLabel {
  double x = 0, y = 0;
  // -1 = hole, else 1-based index into materialProps -- used for BOTH
  // magnetics and heat flow (see FemmMaterialProp's comment): a block
  // label assigned to "Copper" solves with that same material's magnetic
  // properties in a magnetics solve and its thermal properties in a
  // heat-flow solve, since they're the same in-memory entry.
  int blockTypeIndex = -1;
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

// Which physics a mesh/solve operation targets -- a plain parameter for
// MeshBuilder/SolveRunner (Round 6), not stored in FemmProblem itself and
// not a UI "mode": both physics types' properties can be edited on the
// same FemmProblem at once (see the per-entity dialogs' "(Heat Flow)"
// fields), this just tells the mesh writer/solve invocation which of the
// two parallel property/index sets to encode and which solver exe to run.
enum class FemmPhysicsType {
  Magnetics,
  HeatFlow,
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

  // Heat-flow boundary/point/conductor property lists, parallel to the
  // magnetics ones above -- see FemmThermalBoundaryProp's comment for why
  // these stay separate lists (unlike materials, folded into materialProps
  // above per direct user request). Top-level scalar fields above
  // (precision/minAngle/depth/lengthUnits/problemType/coordsPolar/
  // extZo,Ro,Ri/gpuAccel/comment) are mesh/solve-level settings that
  // apply the same way to either physics type and are deliberately NOT
  // duplicated; frequency/acSolver are AC-magnetics-only and are simply
  // omitted when writing a .feh (heat flow is steady-state only).
  QVector<FemmThermalPointProp> thermalPointProps;
  QVector<FemmThermalBoundaryProp> thermalBoundaryProps;
  QVector<FemmThermalConductorProp> thermalConductorProps;

  QVector<FemmNode> nodes;
  QVector<FemmSegment> segments;
  QVector<FemmArcSegment> arcSegments;
  QVector<FemmBlockLabel> blockLabels; // includes holes (blockTypeIndex < 0)
};
