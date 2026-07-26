#pragma once

#include <QString>

struct FemmProblem;
struct MeshSolution;

// Reads a solved .anh file: the thermal geometry/property header (byte-
// for-byte the same format as .feh, see HeatFileIO's header comment) plus
// the appended [Solution] mesh section (hsolv/prob1big.cpp's
// Chsolvdoc::WriteResults, ~line 405 onward -- confirmed directly, not
// re-derived). Independent reimplementation, mirroring AnsFileIO::readAns'
// own structure.
//
// Deliberately reuses MeshSolution (not a separate ThermalMeshSolution
// type) so the existing, already-proven SolutionView rendering pipeline
// (Density/Contour plots, pan/zoom, legend, spatial index, Point/Contour/
// Area tools) works unchanged for thermal results -- see
// SolutionWindow::openAnhFile's comment for the full reasoning. Fields are
// deliberately REPURPOSED, not literally what their names say:
//   MeshSolutionNode::Are  <- nodal temperature T, K (Aim always 0 --
//                             steady-state heat flow has no imaginary part)
//   MeshSolutionElement::B1re/B2re <- heat flux Gx/Gy, W/m^2 (computed
//                             from -k*grad(T), .anh doesn't store flux
//                             directly any more than .ans stores B --
//                             both are recovered from nodal values after
//                             load, see computeElementFlux below)
//   MeshSolutionElement::muX/muY   <- thermal conductivity Kx/Ky, W/(m*K)
//   MeshSolution::bMagMin/bMagMax  <- |heat flux| extremes
// jRe/jIm/jSrcRe/jSrcIm/sigma are left at 0 (no thermal equivalent) --
// callers must gate any J/H-derived display off when in thermal mode
// (see SolutionWindow::m_thermalMode) rather than showing these as if
// they were real current-density/field-strength values.
namespace AnhFileIO {

bool readAnh(const QString& path, FemmProblem& problem, MeshSolution& solution, QString& errorMessage);

}
