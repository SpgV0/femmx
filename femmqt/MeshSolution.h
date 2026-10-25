#pragma once

#include "BHCurve.h"

#include <QVector>

#include <complex>

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

  // Modified by Claude (Anthropic), noreply@anthropic.com: index into
  // MeshSolution::nonlinearMaterials, or -1 for a linear material (use
  // muX/muY above instead) -- see BHCurve.h for why muX/muY alone are
  // wrong for a real (BHpoints>0) nonlinear material and what this
  // fixes. Deduplicated across elements sharing the same material (the
  // BH slope solve happens once per unique material at load time, not
  // once per element).
  int bhMaterialIndex = -1;

  // Total current density (source + induced eddy current), precomputed
  // the same way B1/B2 are -- unlike H (a pointwise function of B, muX,
  // muY alone, cheap to compute per-paint), this needs the element's
  // averaged nodal A (femm/FemmviewDoc.cpp's GetJA: Javg -= I*omega*sigma*
  // Aavg), which isn't available from a single MeshSolutionElement at
  // paint time. MA/m^2, matching JsrcRe/Im's units and femm.rc's own
  // "|Je|, MA/m^2" label. Includes the circuit contribution GetJA adds,
  // from the .ans per-label circuit section (AnsFileIO::readAns). Still
  // MA/m^2: anything that integrates it into amperes must scale by 1e6
  // (#94).
  double jRe = 0, jIm = 0;

  // The potential's GRADIENT, per metre: grad V for electrostatics and
  // current flow, grad T for heat flow. The intensity classic plots is
  // its negative (E = -grad V, G = -grad T); only its magnitude reaches
  // the density plot (|E|, V/m; |G|, K/m). Filled by SolutionAdapter for those three, where B1/B2
  // already carry the flux (D, F, J); unused for magnetics (#107).
  double g1re = 0, g1im = 0, g2re = 0, g2im = 0;

  // Modified by Claude (Anthropic), noreply@anthropic.com: matches femm/
  // FemmviewDoc.cpp's isExt[] (GetBounds -- "exclude external region from
  // the extreme value calculation, otherwise flux in the external regions
  // can give a spurious indication of limits"). True when the element's
  // block label is flagged External, OR its material name matches
  // mi_makeABC's own "u1".."u9" naming convention (femm/femmeLua.cpp) for
  // its Kelvin-transform asymptotic boundary shells -- neither of which
  // represents real, undistorted physical space, so a naive B = dA/dx
  // there isn't a meaningful flux density (confirmed live: it can compute
  // to several times the true field's peak, badly skewing the density
  // plot's auto range even though the value matches classic FEMM's own
  // GetElementB exactly -- classic just excludes it from MIN/MAX the same
  // way this does, rather than computing it differently). Still rendered
  // (this only affects auto-range, not what gets painted), matching
  // PlotFluxDensity's own behavior.
  bool isExternal = false;

  // Modified by Claude (Anthropic), noreply@anthropic.com: matches femm/
  // FemmviewDoc.cpp's CElement::rsqr (GetBounds -- "max corner distance
  // from centroid, squared") -- a per-element SIZE metric used, alongside
  // isExternal above, to keep small/degenerate elements (common right at
  // sharp geometric corners, e.g. where this app's Draw Rectangle/Circle
  // wedge tools meet at a point) from setting the density plot's auto
  // range on their own: classic weights each max-candidate by
  // sqrt(rsqr)*value^2 rather than taking a plain max, so a tiny element
  // with a spuriously large value (real per GetElementB's own formula,
  // but not representative of anything you'd actually want the WHOLE
  // plot's color scale stretched to accommodate) loses to a
  // similar-or-lower value over a genuinely larger area. See
  // SolutionView.cpp's identical adaptation of this same heuristic.
  double rsqr = 0;
};

// The solved circuit correction for one block label, from the untagged
// section after [Elements] -- classic's blocklist[].Case/dVolts/J
// (femm/FemmviewDoc.cpp ~1077). Kept, rather than folded into jRe and
// discarded, because the block integrals need it per NODE, not per
// element (#108).
struct MeshLabelCircuit {
  int caseFlag = 1;              // 0: voltage-specified (dVolts), 1: current (J)
  std::complex<double> dVolts;   // as stored
  std::complex<double> J;        // MA/m^2, as stored
};

struct MeshSolution {
  QVector<MeshSolutionNode> nodes;
  QVector<MeshSolutionElement> elements;
  // One per block label, in FemmProblem::blockLabels order (#108).
  QVector<MeshLabelCircuit> labelCircuits;

  // |B| = sqrt(|B1|^2+|B2|^2) extremes across all elements, precomputed
  // once for the density plot's legend range.
  double bMagMin = 0, bMagMax = 0;

  // One entry per unique nonlinear material referenced by any element --
  // see MeshSolutionElement::bhMaterialIndex and BHCurve.h.
  QVector<BHCurve::Curve> nonlinearMaterials;
};
