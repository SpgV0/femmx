#include "SolutionAdapter.h"

#include "FemmProblem.h"
#include "MeshSolution.h"
#include "ProblemKind.h"
#include "SolutionField.h"
#include "SolutionFileIO.h"

#include <algorithm>
#include <cmath>

bool SolutionAdapter::toMeshSolution(const SolvedMesh& mesh, const FemmProblem& p,
    MeshSolution& out)
{
  out = MeshSolution();
  if (mesh.elements.isEmpty())
    return false;

  out.nodes.resize(mesh.nodes.size());
  for (int i = 0; i < mesh.nodes.size(); i++) {
    out.nodes[i].x = mesh.nodes[i].x;
    out.nodes[i].y = mesh.nodes[i].y;
    // The stored potential, whatever this physics calls it.
    out.nodes[i].Are = mesh.nodes[i].potentialRe;
    out.nodes[i].Aim = mesh.nodes[i].potentialIm;
  }

  out.elements.resize(mesh.elements.size());

  for (int i = 0; i < mesh.elements.size(); i++) {
    const SolutionElement& src = mesh.elements[i];
    MeshSolutionElement& dst = out.elements[i];
    dst.p0 = src.p0;
    dst.p1 = src.p1;
    dst.p2 = src.p2;
    dst.lbl = src.label;

    SolutionField::Vector2 f;
    // A degenerate element keeps a zero field rather than being dropped:
    // removing it would renumber everything after it, and the element
    // index is what the hover readout and block selection refer to.
    if (SolutionField::elementField(mesh, p, i, f)) {
      dst.B1re = f.xRe;
      dst.B1im = f.xIm;
      dst.B2re = f.yRe;
      dst.B2im = f.yIm;
    }
    // The intensity rides along so a density plot can show it too: |E|
    // or |G|, the quantity classic offers beside the flux (#107). Only
    // its magnitude is plotted, so the sign convention does not matter.
    SolutionField::Vector2 g;
    if (SolutionField::potentialGradient(mesh, p, i, g)) {
      dst.g1re = g.xRe;
      dst.g1im = g.xIm;
      dst.g2re = g.yRe;
      dst.g2im = g.yIm;
    }

    const int n = out.nodes.size();
    if (src.p0 >= 0 && src.p0 < n && src.p1 >= 0 && src.p1 < n && src.p2 >= 0 && src.p2 < n) {
      dst.ctrX = (out.nodes[src.p0].x + out.nodes[src.p1].x + out.nodes[src.p2].x) / 3.0;
      dst.ctrY = (out.nodes[src.p0].y + out.nodes[src.p1].y + out.nodes[src.p2].y) / 3.0;

      // Max corner distance from the centroid, squared -- the renderer's
      // size weight for auto-ranging, so a tiny element with a large
      // value cannot stretch the whole legend on its own.
      double rsqr = 0;
      for (int k : { src.p0, src.p1, src.p2 }) {
        const double dx = out.nodes[k].x - dst.ctrX;
        const double dy = out.nodes[k].y - dst.ctrY;
        rsqr = std::max(rsqr, dx * dx + dy * dy);
      }
      dst.rsqr = rsqr;
    }

    // An exterior region, or an IABC shell -- the other physics' makeABC
    // names its shells "u1".."u9" as magnetics' does (#127).
    if (src.label >= 0 && src.label < p.blockLabels.size()) {
      const FemmBlockLabel& label = p.blockLabels[src.label];
      dst.isExternal = label.isExternal
          || isAbcShellMaterialName(ProblemKind::name(p, ProblemKind::Category::Material, label.blockTypeIndex - 1));
    }
  }

  setBMagRange(out);
  return true;
}
