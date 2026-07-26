#pragma once

#include "FemmProblem.h"
#include "MeshSolution.h"

#include <QString>
#include <QVector>

#include <complex>

// Circuit Props (femm.rc's View > Circuit Props, femm/CircDlg.cpp) for
// SOLID conductors only -- see Result::error for the specific cases this
// rejects rather than guessing at. Every formula here was empirically
// validated (not just read off the classic source) against femmx.exe's
// own Circuit Properties dialog on test/results/straight_wire_field/
// straight_wire_field.ans (a 10A, DC, single-solid-conductor series
// circuit): Voltage Drop matched to 6 significant figures
// (5.48984e-05 V) and Flux Linkage to 5 (1.1653e-08 Wb) -- see this
// file's .cpp for the exact derivation and why a literal, careful port of
// femm/FemmviewDoc.cpp's GetJA/GetVoltageDrop/GetFluxLinkage (rather than
// a plausible-looking re-derivation) was necessary: an earlier
// from-first-principles attempt at the Depth/length-unit handling was
// subtly wrong and caught only by this empirical check.
//
// NOT supported, by design (each rejected with a clear Result::error
// rather than silently producing a wrong number):
// - Stranded/litz-wire conductors (FemmMaterialProp::lamType >= 3) --
//   needs femm/FemmviewDoc.cpp's GetFillFactor (frequency-dependent
//   effective conductivity/permeability curve fits for 6 wire types) and
//   GetStrandedVoltageDrop/GetStrandedLinkage, none of which are ported
//   here.
// - Circuits with zero prescribed current -- classic FEMM's
//   zero-current flux linkage (from mutual inductance alone) needs a
//   separate cascade of special cases, not implemented.
namespace CircuitAnalysis {

// One line of the .ans block-label circuit-info section (written by
// fkn/prob1big.cpp's WriteStatic2D right after the node/element lists,
// one per FemmProblem::blockLabels entry in order): caseType 0 means
// `value` is the solved voltage-like unknown for a solid conductor
// (femm/FemmviewDoc.cpp's blocklist[].dVolts); caseType 1 means `value`
// is a directly-specified additional current density (blocklist[].J) --
// every block NOT in a circuit gets a dummy caseType=1/value=0 line.
struct BlockCircuitInfo {
  int caseType = 1;
  double value = 0;
};

// Reads just that section (skipping past the node and element lists,
// whose counts/format are already handled elsewhere by AnsFileIO.cpp --
// this re-reads the file independently rather than threading a new
// output through AnsFileIO's own contract, matching how Problem Info
// already does its own independent re-read for the same file).
bool readBlockCircuitInfo(const QString& ansPath, QVector<BlockCircuitInfo>& out, QString& errorMessage);

struct Result {
  bool ok = false;
  QString error;
  std::complex<double> amps;
  std::complex<double> voltsDrop;
  std::complex<double> fluxLinkage;
};

// `circuitIndex1Based` matches FemmBlockLabel::circuitIndex's own
// convention (1-based into problem.circuitProps).
Result compute(const FemmProblem& problem, const MeshSolution& solution,
    const QVector<BlockCircuitInfo>& blockCircuitInfo, int circuitIndex1Based);

} // namespace CircuitAnalysis
