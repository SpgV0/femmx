#pragma once

#include <QVector>

// Solved-mesh data read from a .ans file (femm/FemmviewDoc.cpp's
// [Solution] section) -- kept as flat arrays of plain POD structs, not
// QGraphicsItems, matching the plan's decision that per-mesh-triangle
// graphics items don't scale to the millions-of-elements meshes this app
// is meant to handle. SolutionView paints directly from these.
struct MeshSolutionNode {
  double x = 0, y = 0;
  double Are = 0, Aim = 0;
};

struct MeshSolutionElement {
  int p0 = 0, p1 = 0, p2 = 0;
  int lbl = 0; // index into FemmProblem::blockLabels
  double B1re = 0, B1im = 0, B2re = 0, B2im = 0; // flux density, precomputed (see AnsFileIO::computeElementFields)
  double ctrX = 0, ctrY = 0; // centroid, precomputed

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
  // user request ("in the material properties you should have
  // permeability") -- the .ans text format's [BlockProps] section (same
  // layout as .fem's) already carries these; AnsFileIO::readAns resolves
  // each element's block label to its material once at load time and
  // bakes the result in here, the same way B1/B2 are precomputed instead
  // of re-derived on every paint. Relative permeability (muX/muY, not
  // absolute -- multiply by physical mu0 = 4*pi*1e-7 H/m to get H from B),
  // conductivity in MS/m, and source current density in MA/m^2 --
  // matching FemmMaterialProp's own units exactly so the same femm/
  // FemmviewDoc.cpp formulas (GetH/GetJA) port over unchanged. Linear,
  // unlaminated, non-permanent-magnet materials only (BHpoints==0,
  // LamType==0/LamFill==1, H_c==0) -- see MeshSolutionItem::elementQuantity's
  // comment for why nonlinear BH-curve materials are a separate, larger
  // follow-up rather than silently approximated here.
  double muX = 1, muY = 1;
  double sigma = 0;
  double jSrcRe = 0, jSrcIm = 0;

  // Total current density (source + induced eddy current), precomputed
  // the same way B1/B2 are -- unlike H (a pointwise function of B, muX,
  // muY alone, cheap to compute per-paint), this needs the element's
  // averaged nodal A (femm/FemmviewDoc.cpp's GetJA: Javg -= I*omega*sigma*
  // Aavg), which isn't available from a single MeshSolutionElement at
  // paint time. MA/m^2, matching JsrcRe/Im's units and femm.rc's own
  // "|Je|, MA/m^2" label. Does NOT include the circuit-current
  // correction GetJA also applies for circuit-driven blocks (needs
  // either a solved per-block-label voltage drop for voltage-specified
  // circuits, which isn't persisted in .ans at all, or a same-circuit
  // conductor-area aggregation for current-specified ones) -- a real,
  // scoped follow-up, not attempted here as a partial/rushed version.
  double jRe = 0, jIm = 0;
};

struct MeshSolution {
  QVector<MeshSolutionNode> nodes;
  QVector<MeshSolutionElement> elements;

  // |B| = sqrt(|B1|^2+|B2|^2) extremes across all elements, precomputed
  // once for the density plot's legend range.
  double bMagMin = 0, bMagMax = 0;
};
