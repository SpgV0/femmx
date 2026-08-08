#pragma once

// Modified by Claude (Anthropic), noreply@anthropic.com: small,
// self-contained dense linear algebra used by ConstraintSolver.h --
// femmqt links no linear-algebra library (only Qt6), and sketch-scale
// constraint systems (tens to low hundreds of unknowns) don't need one;
// a hand-rolled dense solve is simpler to reason about and keeps this
// feature dependency-free. Mirrors BHCurve.h's precedent in this same
// codebase: one focused, heavily-commented numerical-algorithm header.

#include <QVector>

#include <algorithm>
#include <cmath>

namespace DenseLinAlg {

// Row-major dense matrix, deliberately minimal -- only the operations
// ConstraintSolver.cpp actually needs (construct, index, solve, rank).
class Matrix {
public:
  Matrix() = default;
  Matrix(int rows, int cols) : m_rows(rows), m_cols(cols), m_data(rows * cols, 0.0) {}

  int rows() const { return m_rows; }
  int cols() const { return m_cols; }

  double& operator()(int r, int c) { return m_data[r * m_cols + c]; }
  double operator()(int r, int c) const { return m_data[r * m_cols + c]; }

private:
  int m_rows = 0, m_cols = 0;
  QVector<double> m_data;
};

// Returns A^T * A (cols x cols, where A is rows x cols).
inline Matrix multiplyATA(const Matrix& A) {
  const int rows = A.rows(), cols = A.cols();
  Matrix result(cols, cols);
  for (int i = 0; i < cols; i++) {
    for (int j = 0; j < cols; j++) {
      double sum = 0;
      for (int k = 0; k < rows; k++)
        sum += A(k, i) * A(k, j);
      result(i, j) = sum;
    }
  }
  return result;
}

// Returns A^T * b (length cols, where A is rows x cols, b is length rows).
inline QVector<double> multiplyATb(const Matrix& A, const QVector<double>& b) {
  const int rows = A.rows(), cols = A.cols();
  QVector<double> result(cols, 0.0);
  for (int i = 0; i < cols; i++) {
    double sum = 0;
    for (int k = 0; k < rows; k++)
      sum += A(k, i) * b[k];
    result[i] = sum;
  }
  return result;
}

// Solves the SQUARE system A*x = b via Gaussian elimination with partial
// pivoting (operates on copies of A/b, caller's originals untouched).
// Returns false (leaving x untouched) if A is numerically singular (no
// pivot found above kTol for some column) -- callers (ConstraintSolver's
// LM step) must treat that as an expected, not exceptional, state (e.g.
// by growing the damping term and retrying), since a singular
// normal-equations matrix is a normal outcome for an under/
// over-constrained sketch, not a bug.
inline bool solveLinearSystem(Matrix A, QVector<double> b, QVector<double>& x) {
  const int n = A.rows();
  constexpr double kTol = 1e-12;

  for (int col = 0; col < n; col++) {
    int pivotRow = col;
    double pivotVal = std::abs(A(col, col));
    for (int r = col + 1; r < n; r++) {
      double v = std::abs(A(r, col));
      if (v > pivotVal) {
        pivotVal = v;
        pivotRow = r;
      }
    }
    if (pivotVal < kTol)
      return false;

    if (pivotRow != col) {
      for (int c = 0; c < n; c++)
        std::swap(A(col, c), A(pivotRow, c));
      std::swap(b[col], b[pivotRow]);
    }

    for (int r = col + 1; r < n; r++) {
      double factor = A(r, col) / A(col, col);
      if (factor == 0.0)
        continue;
      for (int c = col; c < n; c++)
        A(r, c) -= factor * A(col, c);
      b[r] -= factor * b[col];
    }
  }

  x.resize(n);
  for (int row = n - 1; row >= 0; row--) {
    double sum = b[row];
    for (int c = row + 1; c < n; c++)
      sum -= A(row, c) * x[c];
    x[row] = sum / A(row, row);
  }
  return true;
}

// Estimates rank(A) (A need not be square) via Gaussian elimination with
// partial pivoting, counting pivots whose magnitude exceeds a tolerance
// relative to A's own largest entry (scale-independent). Used by
// ConstraintSolver for DOF analysis (DOF = unknowns - rank(J)) and
// redundancy detection (rank(J) < row count at a converged solution) --
// a full SVD would be more numerically robust right at near-degenerate
// configurations, but this reuses no algorithm beyond the elimination
// the LM solve step already needs, simpler to get right at sketch scale
// (see the plan's own tradeoff note for why this was chosen over SVD).
inline int estimateRank(Matrix A) {
  const int rows = A.rows();
  const int cols = A.cols();
  if (rows == 0 || cols == 0)
    return 0;

  double maxAbs = 0;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      maxAbs = std::max(maxAbs, std::abs(A(r, c)));
  if (maxAbs == 0.0)
    return 0;
  const double tol = maxAbs * 1e-9;

  int rank = 0;
  for (int col = 0; col < cols && rank < rows; col++) {
    int pivotRow = rank;
    double pivotVal = std::abs(A(rank, col));
    for (int r = rank + 1; r < rows; r++) {
      double v = std::abs(A(r, col));
      if (v > pivotVal) {
        pivotVal = v;
        pivotRow = r;
      }
    }
    if (pivotVal < tol)
      continue; // this column contributes no new rank

    if (pivotRow != rank)
      for (int c = 0; c < cols; c++)
        std::swap(A(rank, c), A(pivotRow, c));

    for (int r = rank + 1; r < rows; r++) {
      double factor = A(r, col) / A(rank, col);
      if (factor == 0.0)
        continue;
      for (int c = col; c < cols; c++)
        A(r, c) -= factor * A(rank, c);
    }
    rank++;
  }
  return rank;
}

} // namespace DenseLinAlg
