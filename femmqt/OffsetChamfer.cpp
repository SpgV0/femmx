#define _USE_MATH_DEFINES

#include "OffsetChamfer.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <complex>

namespace {

using Complex = std::complex<double>;

constexpr double kRelTol = 1e-9;

Complex nodePos(const FemmProblem& p, int i)
{
  return Complex(p.nodes[i].x, p.nodes[i].y);
}

OffsetChamfer::Result fail(const QString& why)
{
  OffsetChamfer::Result r;
  r.ok = false;
  r.message = why;
  return r;
}

// One entity of a chain, as walked. `reversed` means the walk enters at
// n1 and leaves at n0, which flips both the side the offset goes to and
// the sign of an arc's sweep -- getting that wrong offsets half a chain
// the wrong way, which looks like a plausible shape and is not one.
struct Link {
  bool isArc = false;
  int index = -1;
  bool reversed = false;
};

// An offset piece, before it is written back as real geometry. Arcs
// carry a SIGNED sweep here (negative meaning clockwise); FemmArcSegment
// cannot, so the sign is resolved by swapping endpoints at emit time.
struct Piece {
  bool isArc = false;
  Complex start, end;
  // The direction this piece had BEFORE its ends were moved by the
  // corner joins, kept so an over-offset piece can be recognised -- see
  // the flip check in offsetSelection().
  Complex originalDir;
  double originalSweep = 0;
  Complex centre;
  double radius = 0;
  double sweepDeg = 0;
  // Copied from the entity this came from.
  int group = 0;
  double maxSideLength = -1;
  double arcMaxSideLength = 1;
  bool hidden = false;
};

// The chains present in the selection, each an ordered run of links.
QVector<QVector<Link>> buildChains(const FemmProblem& p, QVector<bool>& closedOut)
{
  QVector<Link> all;
  for (int i = 0; i < p.segments.size(); i++) {
    if (p.segments[i].isSelected) {
      Link l;
      l.isArc = false;
      l.index = i;
      all.push_back(l);
    }
  }
  for (int i = 0; i < p.arcSegments.size(); i++) {
    if (p.arcSegments[i].isSelected) {
      Link l;
      l.isArc = true;
      l.index = i;
      all.push_back(l);
    }
  }

  auto ends = [&](const Link& l) {
    return l.isArc ? QPair<int, int>(p.arcSegments[l.index].n0, p.arcSegments[l.index].n1)
                   : QPair<int, int>(p.segments[l.index].n0, p.segments[l.index].n1);
  };

  QHash<int, QVector<int>> byNode; // node -> indices into `all`
  for (int i = 0; i < all.size(); i++) {
    const auto e = ends(all[i]);
    byNode[e.first].push_back(i);
    byNode[e.second].push_back(i);
  }

  QVector<bool> used(all.size(), false);
  QVector<QVector<Link>> chains;
  closedOut.clear();

  auto walkFrom = [&](int startLink, int startNode) {
    QVector<Link> chain;
    int cur = startLink;
    int node = startNode;
    while (cur >= 0 && !used[cur]) {
      used[cur] = true;
      Link l = all[cur];
      const auto e = ends(l);
      l.reversed = (e.first != node);
      chain.push_back(l);
      const int nextNode = l.reversed ? e.first : e.second;
      int next = -1;
      for (int cand : byNode.value(nextNode)) {
        if (cand != cur && !used[cand]) {
          // A node with three or more selected entities on it is a
          // branch, not a chain. Stopping is the only honest answer --
          // continuing would silently pick one arm and offset the rest
          // as if the others were not there.
          if (next >= 0) {
            next = -1;
            break;
          }
          next = cand;
        }
      }
      cur = next;
      node = nextNode;
    }
    return chain;
  };

  // Open chains first, from their free ends, so a run is walked end to
  // end rather than started in the middle and split in two.
  for (int i = 0; i < all.size(); i++) {
    if (used[i])
      continue;
    const auto e = ends(all[i]);
    for (int startNode : { e.first, e.second }) {
      if (used[i])
        break;
      if (byNode.value(startNode).size() == 1) {
        chains.push_back(walkFrom(i, startNode));
        closedOut.push_back(false);
      }
    }
  }
  // Whatever is left is a closed loop (or an isolated entity whose ends
  // both have degree 1, already taken above).
  for (int i = 0; i < all.size(); i++) {
    if (used[i])
      continue;
    const auto e = ends(all[i]);
    QVector<Link> chain = walkFrom(i, e.first);
    if (chain.isEmpty())
      continue;
    const Link& last = chain.back();
    const auto le = last.isArc
        ? QPair<int, int>(p.arcSegments[last.index].n0, p.arcSegments[last.index].n1)
        : QPair<int, int>(p.segments[last.index].n0, p.segments[last.index].n1);
    const int tail = last.reversed ? le.first : le.second;
    chains.push_back(chain);
    closedOut.push_back(tail == e.first && chain.size() > 1);
  }
  return chains;
}

// Offset one link by `d`, to the left of the walk direction.
bool offsetLink(const FemmProblem& p, const Link& l, double d, Piece& out)
{
  out = Piece();
  if (!l.isArc) {
    const FemmSegment& s = p.segments[l.index];
    Complex a = nodePos(p, l.reversed ? s.n1 : s.n0);
    Complex b = nodePos(p, l.reversed ? s.n0 : s.n1);
    const double len = std::abs(b - a);
    if (len <= 0)
      return false;
    // Left of the direction of travel is the direction rotated +90
    // degrees, i.e. multiplied by i.
    const Complex nrm = Complex(0, 1) * (b - a) / len;
    out.isArc = false;
    out.start = a + d * nrm;
    out.end = b + d * nrm;
    out.originalDir = out.end - out.start;
    out.group = s.inGroup;
    out.maxSideLength = s.maxSideLength;
    out.hidden = s.hidden;
    return true;
  }

  const FemmArcSegment& arc = p.arcSegments[l.index];
  Complex c;
  double R = 0;
  if (!FemmProblemEdit::circleFromArc(p, arc, c, R) || R <= 0)
    return false;

  const double sweep = l.reversed ? -arc.arcLength : arc.arcLength;
  const Complex startPt = nodePos(p, l.reversed ? arc.n1 : arc.n0);
  const double th0 = std::arg(startPt - c);

  // Walking counterclockwise, the left of the direction of travel points
  // INWARD, at the centre -- so a positive offset shrinks the radius.
  // Walking the same arc backwards reverses both, which the sign of
  // `sweep` already carries.
  const double newR = (sweep > 0) ? (R - d) : (R + d);
  if (newR <= R * kRelTol)
    return false; // the offset has collapsed through the centre

  out.isArc = true;
  out.centre = c;
  out.radius = newR;
  out.sweepDeg = sweep;
  out.start = c + newR * std::polar(1.0, th0);
  out.end = c + newR * std::polar(1.0, th0 + sweep * M_PI / 180.0);
  out.originalDir = out.end - out.start;
  out.originalSweep = sweep;
  out.group = arc.inGroup;
  out.arcMaxSideLength = arc.maxSideLength;
  out.hidden = arc.hidden;
  return true;
}

// The direction of travel at a piece's start or end. For an arc that is
// the tangent, which is the radius turned 90 degrees the way the sweep
// goes -- the sign of the sweep is the whole of the difference between
// the two ends of a circle.
Complex tangentAt(const Piece& piece, bool atEnd)
{
  if (!piece.isArc)
    return piece.end - piece.start;
  const Complex radial = (atEnd ? piece.end : piece.start) - piece.centre;
  return Complex(0, piece.sweepDeg >= 0 ? 1.0 : -1.0) * radial;
}

// Where two offset pieces would meet if both were extended. Returns
// false when they do not meet at all, which is the outside of a convex
// turn and is what a fillet is for.
bool cornerIntersection(const Piece& a, const Piece& b, Complex near, Complex& out)
{
  QVector<Complex> candidates;

  if (!a.isArc && !b.isArc) {
    const Complex r = a.end - a.start, s = b.end - b.start;
    const double denom = r.real() * s.imag() - r.imag() * s.real();
    if (std::abs(denom) < kRelTol * std::abs(r) * std::abs(s))
      return false; // parallel: collinear pieces need no corner at all
    const Complex d = b.start - a.start;
    const double t = (d.real() * s.imag() - d.imag() * s.real()) / denom;
    candidates.push_back(a.start + t * r);
  } else if (a.isArc && b.isArc) {
    const double dd = std::abs(b.centre - a.centre);
    if (dd <= 0 || dd > a.radius + b.radius || dd < std::abs(a.radius - b.radius))
      return false;
    const double x = (dd * dd + a.radius * a.radius - b.radius * b.radius) / (2 * dd);
    const double h2 = a.radius * a.radius - x * x;
    const double h = h2 > 0 ? std::sqrt(h2) : 0.0;
    const Complex u = (b.centre - a.centre) / dd;
    const Complex mid = a.centre + x * u;
    const Complex perp = Complex(-u.imag(), u.real()) * h;
    candidates << mid + perp << mid - perp;
  } else {
    const Piece& line = a.isArc ? b : a;
    const Piece& circ = a.isArc ? a : b;
    const Complex dir = line.end - line.start;
    const double len2 = std::norm(dir);
    if (len2 <= 0)
      return false;
    const Complex f = line.start - circ.centre;
    const double qb = 2 * (f.real() * dir.real() + f.imag() * dir.imag());
    const double qc = std::norm(f) - circ.radius * circ.radius;
    const double disc = qb * qb - 4 * len2 * qc;
    if (disc < 0)
      return false;
    const double sq = std::sqrt(disc);
    candidates << line.start + ((-qb - sq) / (2 * len2)) * dir
               << line.start + ((-qb + sq) / (2 * len2)) * dir;
  }

  if (candidates.isEmpty())
    return false;
  // The corner is whichever candidate is nearest where the corner
  // obviously is -- the offset of the original shared node. Two circles
  // meet twice, and the far one is on the other side of the geometry.
  out = candidates[0];
  double best = std::abs(candidates[0] - near);
  for (const Complex& cand : candidates) {
    const double dist = std::abs(cand - near);
    if (dist < best) {
      best = dist;
      out = cand;
    }
  }
  return true;
}

// The angle a piece subtends, recomputed after its ends were moved by a
// corner join. Only arcs need it: a segment is still the segment through
// its two (new) ends, but an arc's included angle changes when either
// end slides around the circle.
void refreshArcSweep(Piece& piece)
{
  if (!piece.isArc)
    return;
  const double th0 = std::arg(piece.start - piece.centre);
  const double th1 = std::arg(piece.end - piece.centre);
  double delta = (th1 - th0) * 180.0 / M_PI;
  // Keep the direction the piece already had rather than always taking
  // the short way -- a major arc offsets to a major arc.
  while (delta <= 0 && piece.sweepDeg > 0)
    delta += 360.0;
  while (delta >= 0 && piece.sweepDeg < 0)
    delta -= 360.0;
  piece.sweepDeg = delta;
}

int emitNode(FemmProblem& p, Complex pt, double tol)
{
  for (int i = 0; i < p.nodes.size(); i++) {
    if (std::abs(nodePos(p, i) - pt) <= tol)
      return i;
  }
  return FemmProblemEdit::addNode(p, pt.real(), pt.imag());
}

} // namespace

// ---------------------------------------------------------------------------

bool OffsetChamfer::canChamfer(const FemmProblem& p, int n)
{
  if (n < 0 || n >= p.nodes.size())
    return false;
  int segs = 0, arcs = 0;
  for (const FemmSegment& s : p.segments)
    if (s.n0 == n || s.n1 == n)
      segs++;
  for (const FemmArcSegment& a : p.arcSegments)
    if (a.n0 == n || a.n1 == n)
      arcs++;
  return segs == 2 && arcs == 0;
}

OffsetChamfer::Result OffsetChamfer::offsetSelection(FemmProblem& p, double distance, CornerStyle corners)
{
  if (distance == 0)
    return fail(QStringLiteral("An offset of zero would draw the curve on top of itself."));

  QVector<bool> closed;
  const QVector<QVector<Link>> chains = buildChains(p, closed);
  if (chains.isEmpty())
    return fail(QStringLiteral("Select the lines and arcs to offset first."));

  // Scale for the coincident-node tolerance: the size of what is being
  // offset, so this behaves the same in millimetres and in metres.
  double scale = std::abs(distance);
  for (const FemmNode& n : p.nodes)
    scale = std::max(scale, std::hypot(n.x, n.y));
  const double tol = std::max(1e-12, 1e-9 * scale);

  Result r;
  int chainsSkipped = 0;

  for (int ci = 0; ci < chains.size(); ci++) {
    const QVector<Link>& chain = chains[ci];
    QVector<Piece> pieces;
    bool allOk = true;
    for (const Link& l : chain) {
      Piece piece;
      if (!offsetLink(p, l, distance, piece)) {
        allOk = false;
        break;
      }
      pieces.push_back(piece);
    }
    if (!allOk || pieces.isEmpty()) {
      chainsSkipped++;
      continue;
    }

    // Close each corner. A fillet is inserted where the two offsets no
    // longer reach each other; collected first and emitted afterwards so
    // the indices below stay simple.
    struct Fillet {
      int afterPiece;
      Complex centre, from, to;
      double sweepDeg;
      int group;
    };
    QVector<Fillet> fillets;

    const int joins = closed[ci] ? pieces.size() : pieces.size() - 1;
    for (int i = 0; i < joins; i++) {
      Piece& a = pieces[i];
      Piece& b = pieces[(i + 1) % pieces.size()];
      // Where the corner is, approximately: the shared original node
      // pushed out by the offset. Used only to choose between the two
      // intersections a pair of circles has.
      const Complex nearPt = 0.5 * (a.end + b.start);

      Complex meet;
      if (std::abs(a.end - b.start) <= tol) {
        continue; // already meet -- a tangent join needs nothing
      }

      // Inside or outside? The turn from one piece to the next is a left
      // turn when this is positive. Offsetting LEFT (a positive
      // distance) into a left turn lands on the inside, where the two
      // offsets overlap and must be cut back to their intersection --
      // rounding there would pull the new curve closer to the original
      // than the distance asked for. The signs disagreeing means the
      // outside, where they have come apart and the style applies.
      const double turn = std::arg(tangentAt(b, false) / tangentAt(a, true));
      const bool outside = turn * distance < 0;

      if (!(outside && corners == CornerStyle::Fillet)
          && cornerIntersection(a, b, nearPt, meet)) {
        a.end = meet;
        b.start = meet;
        refreshArcSweep(a);
        refreshArcSweep(b);
        continue;
      }

      // Either the user asked for rounded outside corners, or the two
      // offsets genuinely do not meet anywhere (which two arcs can
      // manage). A fillet of radius |distance| centred on the ORIGINAL
      // corner keeps the offset at a constant distance from it.
      const Link& la = chain[i];
      const int sharedNode = la.isArc ? (la.reversed ? p.arcSegments[la.index].n0 : p.arcSegments[la.index].n1)
                                      : (la.reversed ? p.segments[la.index].n0 : p.segments[la.index].n1);
      const Complex corner = nodePos(p, sharedNode);
      double delta = std::arg((b.start - corner) / (a.end - corner)) * 180.0 / M_PI;
      Fillet f;
      f.afterPiece = i;
      f.centre = corner;
      f.from = a.end;
      f.to = b.start;
      f.sweepDeg = delta;
      f.group = a.group;
      fillets.push_back(f);
      r.cornersFilleted++;
    }

    // A piece that now runs BACKWARDS relative to the direction it had
    // before the corners were closed has been cut past its own far end
    // by the joins on either side of it. That is the signature of an
    // offset too large for the shape -- inset a 10-by-6 rectangle by 4
    // and the short sides invert -- and emitting it gives a bow-tie that
    // looks like geometry and meshes as nonsense.
    //
    // Detected per chain and refused per chain: a chain with one
    // inverted piece is not partly right, and dropping just that piece
    // would leave a gap in a curve whose whole value is being closed.
    bool inverted = false;
    for (const Piece& piece : pieces) {
      if (!piece.isArc) {
        const Complex now = piece.end - piece.start;
        if ((now.real() * piece.originalDir.real() + now.imag() * piece.originalDir.imag()) <= 0)
          inverted = true;
      } else if (piece.sweepDeg * piece.originalSweep < 0) {
        inverted = true;
      }
    }
    if (inverted) {
      chainsSkipped++;
      continue;
    }

    for (const Piece& piece : pieces) {
      const int n0 = emitNode(p, piece.start, tol);
      const int n1 = emitNode(p, piece.end, tol);
      if (n0 == n1)
        continue; // collapsed to nothing by the corner joins
      if (!piece.isArc) {
        const int idx = FemmProblemEdit::addSegment(p, n0, n1);
        p.segments[idx].inGroup = piece.group;
        p.segments[idx].maxSideLength = piece.maxSideLength;
        p.segments[idx].hidden = piece.hidden;
        r.segmentsCreated++;
      } else {
        // FemmArcSegment has no sign: a clockwise sweep is the same arc
        // walked the other way, so the endpoints swap instead.
        const bool ccw = piece.sweepDeg >= 0;
        const int idx = FemmProblemEdit::addArcSegment(p, ccw ? n0 : n1, ccw ? n1 : n0,
            std::abs(piece.sweepDeg), piece.arcMaxSideLength);
        p.arcSegments[idx].inGroup = piece.group;
        p.arcSegments[idx].hidden = piece.hidden;
        r.arcsCreated++;
      }
    }

    for (const Fillet& f : fillets) {
      const int n0 = emitNode(p, f.from, tol);
      const int n1 = emitNode(p, f.to, tol);
      if (n0 == n1)
        continue;
      const bool ccw = f.sweepDeg >= 0;
      const int idx = FemmProblemEdit::addArcSegment(p, ccw ? n0 : n1, ccw ? n1 : n0,
          std::abs(f.sweepDeg), 1.0);
      p.arcSegments[idx].inGroup = f.group;
      r.arcsCreated++;
    }
  }

  if (r.segmentsCreated == 0 && r.arcsCreated == 0) {
    return fail(QStringLiteral("Nothing could be offset by that distance. The offset "
                               "runs into itself: a piece would end up shorter than "
                               "nothing once its corners are closed, or an arc would "
                               "collapse through its own centre. Try a smaller "
                               "distance."));
  }

  r.ok = true;
  QStringList notes;
  notes << QStringLiteral("Offset: %1 line(s) and %2 arc(s) created")
               .arg(r.segmentsCreated).arg(r.arcsCreated);
  if (r.cornersFilleted > 0)
    notes << QStringLiteral("%1 corner(s) rounded to keep the distance constant").arg(r.cornersFilleted);
  if (chainsSkipped > 0)
    notes << QStringLiteral("%1 chain(s) skipped as degenerate at that distance").arg(chainsSkipped);
  // Stated, not silent -- see the header for why the boundary condition
  // deliberately does not come along.
  notes << QStringLiteral("boundary conditions were NOT copied onto the new curve");
  r.message = notes.join(QStringLiteral("; ")) + QStringLiteral(".");
  return r;
}

// ---------------------------------------------------------------------------

namespace {

// The shared body of both chamfer forms: given the corner and the two
// trim distances, cut it. Mirrors FemmProblemEdit::createRadius's
// sequence exactly -- rewire both edges to the new trim points BEFORE
// deleting the corner node, because deleteNode cascades and removes
// anything still referencing it.
OffsetChamfer::Result applyChamfer(FemmProblem& p, int n, double len0, double len1)
{
  FemmSegment* seg0 = nullptr;
  FemmSegment* seg1 = nullptr;
  int i0 = -1, i1 = -1;
  for (int k = 0; k < p.segments.size(); k++) {
    if (p.segments[k].n0 == n || p.segments[k].n1 == n) {
      if (i0 < 0)
        i0 = k;
      else
        i1 = k;
    }
  }
  seg0 = &p.segments[i0];
  seg1 = &p.segments[i1];

  const Complex p0 = nodePos(p, n);
  const Complex p1 = nodePos(p, seg0->n0 == n ? seg0->n1 : seg0->n0);
  const Complex p2 = nodePos(p, seg1->n0 == n ? seg1->n1 : seg1->n0);

  const double phi = std::abs(std::arg((p2 - p0) / (p1 - p0)));
  if (phi > 179.0 * M_PI / 180.0 || phi < 1.0 * M_PI / 180.0) {
    return fail(QStringLiteral("Those two lines are nearly straight through that node "
                               "(%1 degrees apart). There is no corner there to cut.")
                    .arg(phi * 180.0 / M_PI, 0, 'f', 1));
  }

  const double avail0 = std::abs(p1 - p0), avail1 = std::abs(p2 - p0);
  if (len0 >= avail0 || len1 >= avail1) {
    return fail(QStringLiteral("The chamfer is longer than the lines it cuts "
                               "(%1 and %2 requested, %3 and %4 available). It would "
                               "run past their far ends.")
                    .arg(len0, 0, 'g', 4).arg(len1, 0, 'g', 4)
                    .arg(avail0, 0, 'g', 4).arg(avail1, 0, 'g', 4));
  }

  const Complex t1 = p0 + len0 * (p1 - p0) / avail0;
  const Complex t2 = p0 + len1 * (p2 - p0) / avail1;

  const int boundaryMarker = seg0->boundaryMarker;
  const int group = seg0->inGroup;

  int n1idx = FemmProblemEdit::addNode(p, t1.real(), t1.imag());
  int n2idx = FemmProblemEdit::addNode(p, t2.real(), t2.imag());

  // addNode may have reallocated p.nodes, but not p.segments -- the same
  // reasoning createRadius documents at this point. Re-resolve anyway:
  // it costs nothing and does not depend on that staying true.
  seg0 = &p.segments[i0];
  seg1 = &p.segments[i1];
  if (seg0->n0 == n)
    seg0->n0 = n1idx;
  else
    seg0->n1 = n1idx;
  if (seg1->n0 == n)
    seg1->n0 = n2idx;
  else
    seg1->n1 = n2idx;

  FemmProblemEdit::deleteNode(p, n);
  if (n1idx > n)
    n1idx--;
  if (n2idx > n)
    n2idx--;

  const int idx = FemmProblemEdit::addSegment(p, n1idx, n2idx);
  // A chamfer REPLACES the corner, unlike an offset, so the boundary
  // condition does come across -- the model goes on saying what it said.
  p.segments[idx].boundaryMarker = boundaryMarker;
  p.segments[idx].inGroup = group;

  OffsetChamfer::Result r;
  r.ok = true;
  r.segmentsCreated = 1;
  return r;
}

} // namespace

OffsetChamfer::Result OffsetChamfer::chamferDistances(FemmProblem& p, int n, double d0, double d1)
{
  if (!canChamfer(p, n)) {
    return fail(QStringLiteral("That node is not a corner this can chamfer -- it needs "
                               "exactly two straight lines meeting at it. Use Create "
                               "Radius for a corner involving an arc: \"a distance back "
                               "along the edge\" has no single meaning once the edge "
                               "curves, and guessing one would produce a chamfer that "
                               "is not the length you asked for."));
  }
  if (d0 <= 0 || d1 <= 0)
    return fail(QStringLiteral("Both chamfer distances have to be greater than zero."));
  return applyChamfer(p, n, d0, d1);
}

OffsetChamfer::Result OffsetChamfer::chamferDistanceAngle(FemmProblem& p, int n, double d0, double angleDeg)
{
  if (!canChamfer(p, n)) {
    return fail(QStringLiteral("That node is not a corner this can chamfer -- it needs "
                               "exactly two straight lines meeting at it."));
  }
  if (d0 <= 0)
    return fail(QStringLiteral("The chamfer distance has to be greater than zero."));
  if (angleDeg <= 0 || angleDeg >= 180)
    return fail(QStringLiteral("The chamfer angle has to be between 0 and 180 degrees."));

  // Find the corner's two edge directions, then solve the triangle:
  // the chamfer, the length d0 along the first edge and the unknown
  // length along the second make a triangle whose angles are `angleDeg`,
  // the corner angle, and whatever is left. The sine rule gives the
  // second length, so this reduces to the distance-distance case rather
  // than repeating the trimming logic.
  int i0 = -1, i1 = -1;
  for (int k = 0; k < p.segments.size(); k++) {
    if (p.segments[k].n0 == n || p.segments[k].n1 == n) {
      if (i0 < 0)
        i0 = k;
      else
        i1 = k;
    }
  }
  const Complex p0 = nodePos(p, n);
  const Complex p1 = nodePos(p, p.segments[i0].n0 == n ? p.segments[i0].n1 : p.segments[i0].n0);
  const Complex p2 = nodePos(p, p.segments[i1].n0 == n ? p.segments[i1].n1 : p.segments[i1].n0);

  const double corner = std::abs(std::arg((p2 - p0) / (p1 - p0)));
  const double a = angleDeg * M_PI / 180.0;
  const double third = M_PI - corner - a;
  if (third <= 1e-9) {
    return fail(QStringLiteral("A %1-degree chamfer cannot close against a %2-degree "
                               "corner -- the two angles already use up the triangle.")
                    .arg(angleDeg, 0, 'f', 1).arg(corner * 180.0 / M_PI, 0, 'f', 1));
  }
  if (std::abs(std::sin(third)) < 1e-12)
    return fail(QStringLiteral("That angle gives a degenerate chamfer."));

  // Sine rule: d1 / sin(angleDeg) = d0 / sin(third).
  const double d1 = d0 * std::sin(a) / std::sin(third);
  return applyChamfer(p, n, d0, d1);
}
