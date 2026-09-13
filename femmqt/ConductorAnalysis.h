#pragma once

// ConductorAnalysis.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// The conductor counterpart of CircuitAnalysis.
//
// Magnetics drives a REGION with a circuit, and CircuitAnalysis reports
// its current, voltage drop and flux linkage. The other three physics
// hold a SURFACE at a potential with a conductor, and the questions are
// the mirror image: what is that surface sitting at, and what is
// crossing it.
//
//   electrostatics   potential in volts, total charge in coulombs/metre
//   heat flow        temperature in kelvin, heat flow in watts/metre
//   current flow     potential in volts, current in amperes/metre
//
// A conductor is defined by its NODES: the solution file tags each mesh
// node with the conductor it belongs to (the trailing column magnetics
// does not have). Its surface is therefore every element edge whose two
// endpoints carry the same tag, and the flux crossing it is the field
// through those edges.
//
// Per metre of depth, like everything else a 2D solution determines.

#include <QString>
#include <QVector>

#include "SolutionField.h"
#include "SolutionFileIO.h"

struct FemmProblem;

namespace ConductorAnalysis {

struct Result {
  bool ok = false;
  QString name;

  // The conductor is an equipotential, so every node on it should carry
  // the same value; this is their average. `potentialSpread` is the
  // difference between the extremes, which should be near zero -- a
  // large one means the tag spans things that are not actually one
  // conductor, and reporting the average alone would hide that.
  double potentialRe = 0, potentialIm = 0;
  double potentialSpread = 0;

  // Total flux leaving the conductor's surface: charge, heat or current
  // depending on the physics.
  double fluxRe = 0, fluxIm = 0;

  double surfaceLength = 0; // m
  int nodeCount = 0;
};

// `conductor` is the 1-based tag as it appears in the solution file;
// 0 means "no conductor" and is never a valid argument.
Result analyse(const SolvedMesh& mesh, const FemmProblem& p, int conductor);

// What the two numbers are called in this physics.
SolutionField::Quantity potentialQuantity(FemmProblemKind kind);
SolutionField::Quantity fluxQuantity(FemmProblemKind kind);

} // namespace ConductorAnalysis
