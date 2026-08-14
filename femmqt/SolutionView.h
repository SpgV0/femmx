#pragma once

#include <QImage>
#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMainWindow>
#include <QPainterPath>
#include <QPair>
#include <QSet>
#include <QVector>

#include "FemmProblem.h"
#include "MeshSolution.h"
#include "ViewPanning.h"

#include <complex>

class QAction;
class QDockWidget;
class QToolBar;
class QPlainTextEdit;
class QKeyEvent;

// Paints the whole solved mesh (potentially millions of triangles) in a
// handful of QPainter calls -- one filled QPainterPath per color band,
// batched the same way femm/FemmviewView.cpp's PlotFluxDensity/
// FlushDensityBand batch into PolyPolygon() calls, and for the same
// reason: one draw call per element does not scale to meshes this app is
// meant to handle. A single QGraphicsItem (not one per triangle) so it
// still gets QGraphicsView's pan/zoom/rubber-band plumbing for free.
class MeshSolutionItem : public QGraphicsItem {
  public:
  explicit MeshSolutionItem(const MeshSolution* solution);

  QRectF boundingRect() const override;
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

  // Density (colored |B| bands, the default) or Contour (evenly-spaced
  // equipotential/A-contour lines) -- mirrors femm.rc's View > Density/
  // Contour Plot, though this is a first pass at each rather than the
  // classic dialogs' full configurability (band count, etc.). No
  // separate Vector Plot mode -- removed per direct user request ("I do
  // not want the vector plot at all, remove it").
  enum class PlotMode { Density, Contour };
  void setPlotMode(PlotMode mode);
  void setSmoothing(bool smooth);
  void setShowMesh(bool show);
  void setShowPoints(bool show);
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
  // user report ("the edges and nodes of the geometry do not show up") --
  // the classic GUI's post-processor (femm/FemmviewView.cpp) always draws
  // the ORIGINAL problem geometry (the nodes/segments/arcs drawn in the
  // pre-processor, e.g. "20 nodes, 20 arcs") as a thin overlay on top of
  // whichever plot is active, distinct from "Show Mesh"/"Show Points"
  // above (those are the solved FE mesh's own triangulation/nodes,
  // typically thousands to millions of them -- a completely different,
  // much smaller list). femmqt had no equivalent at all. Not gated by a
  // toggle, matching the classic GUI's own always-on behavior for
  // segments/arcs; nullptr (the default, e.g. before a file is loaded)
  // just means nothing to draw yet.
  void setProblemGeometry(const FemmProblem* problem);
  // Matches femm.rc's IDR_FEMMVIEWTYPE View > Show Block Names -- off by
  // default, same as GeometryScene::m_showBlockNames.
  void setShowBlockNames(bool show);

  // Modified by Claude (Anthropic), noreply@anthropic.com: per user report
  // ("when an area is selected it does not show up in the screen") --
  // matches femm/FemmviewView.cpp's Area tool exactly: clicking inside a
  // block-label's region TOGGLES its selection (femm's CBlockLabel::
  // ToggleSelect) rather than instantly popping up a result, selections
  // persist and accumulate across multiple clicks/regions, and every
  // selected region is highlighted on screen (femm's PlotSelectedElm/
  // RegionColor) until cleared or toggled off again. SolutionWindow reads
  // the accumulated set when the user asks for a result (Integrate),
  // rather than this class computing anything itself -- same split of
  // responsibility as the Contour tool's m_contourPoints/showContourIntegral.
  void toggleBlockLabelSelected(int lbl);
  void clearBlockLabelSelection();
  bool hasBlockLabelSelection() const { return !m_selectedBlockLabels.isEmpty(); }
  const QSet<int>& selectedBlockLabels() const { return m_selectedBlockLabels; }

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request for "all the different heatmap possibilities" the
  // classic GUI offers (femm/FemmviewView.cpp's DensityPlot 1-10 for AC,
  // 1-4 for DC -- see that file's legend-label switch for the full list).
  // |B|, |B_re|, |B_im|, and log10(|B|) are directly computable from the
  // complex Bx/By components MeshSolutionElement already stores.
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // HMag/JMag added -- per user correction ("in the material properties
  // you should have permeability"), the .ans text format's [BlockProps]
  // section (byte-for-byte the same as .fem's) DOES carry muX/muY/sigma/
  // JsrcRe/JsrcIm, and a third, untagged section right after [Elements]
  // carries fkn.exe's own solved per-block-label circuit correction --
  // AnsFileIO::readAns now resolves and bakes both into every
  // MeshSolutionElement (see that struct's own comment). H = B/(mu*mu0)
  // is exact for linear, unlaminated, non-permanent-magnet materials
  // (BHpoints==0, LamType==0/LamFill==1, H_c==0). Nonlinear (BHpoints>0),
  // unlaminated, DC (real-valued) materials are now also exact -- see
  // BHCurve.h and elementQuantity()'s .cpp comment -- ported from femm/
  // Problem.cpp's CMaterialProp::GetSlopes/GetH/GetMu. Still NOT ported
  // (a separate, larger follow-up, not a silently-wrong approximation
  // for those material types -- see BHCurve.h's own scope note): AC/
  // harmonic nonlinear materials, laminated nonlinear materials,
  // incremental permeability for DC-offset AC problems, and permanent-
  // magnet (H_c != 0) coercive-force shifting. J = total current density
  // (source + eddy + solved circuit correction, femm/FemmviewDoc.cpp's
  // GetJA, ported faithfully including the circuit term since it reads
  // fkn.exe's own solved output rather than re-deriving it) -- MA/m^2,
  // matching femm.rc's own "|Js+Je|, MA/m^2" label and units.
  // Modified by Claude (Anthropic), noreply@anthropic.com: extended to all
  // 10 quantities femm/cv_DPlotDlg2.cpp's OnInitDialog listtype==2 (AC)
  // case offers (was missing Re/Im of H and J) and reordered to match its
  // combo exactly -- |B|, Re(B), Im(B), |H|, Re(H), Im(H), |J|, Re(J),
  // Im(J), log10(|B|) -- so DensityPlotOptionsDialog's AC combo can be a
  // direct 1:1 port instead of a subset. Nothing outside this app
  // persists these values (no .ans/.fem field, no QSettings key), so
  // reordering the existing 6 is safe.
  enum class DensityQuantity { BMag, BReMag, BImMag, HMag, HReMag, HImMag, JMag, JReMag, JImMag, LogBMag };
  // Modified by Claude (Anthropic), noreply@anthropic.com: named count for
  // DensityQuantity, used to size/fill m_quantityData below -- the enum
  // grew from 6 to 10 values without this (still hardcoded as 6 in both
  // places), leaving m_quantityData[6..9] both out of the fixed array's
  // bounds AND never precomputed. Selecting |J|, Re(J), Im(J), or
  // log10(|B|) in the Density Plot Options dialog read past the end of
  // m_quantityData and dereferenced whatever garbage QVector<double>
  // happened to be there -- confirmed live as a real crash (access
  // violation reading an invalid address) via a Windows crash dump on a
  // real model. A named constant kept in lockstep with the enum, rather
  // than a second hardcoded number, is what actually prevents this class
  // of bug from recurring the next time a quantity is added.
  static constexpr int kDensityQuantityCount = 10;
  void setDensityQuantity(DensityQuantity q);
  DensityQuantity densityQuantity() const { return m_densityQuantity; }

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: per
  // user request ("I think the density plots have more options (greyscale,
  // range ...)") -- ports femm/cv_DPlotDlg2.h's cvCDPlotDlg2 dialog fields
  // (m_gscale, PlotBounds[]/d_PlotBounds[]) onto the Qt side. Greyscale is
  // a global toggle (matches classic's GreyContours, one setting for
  // whichever quantity is plotted); the custom range is tracked PER
  // quantity (matches classic's PlotBounds[quantity][0/1], persisted
  // separately per quantity so switching Density Quantity doesn't clobber
  // a range set for a different one) and is an OPT-IN override -- when not
  // set, paintDensity() keeps its existing zoom-adaptive auto-range
  // behavior (see that method's own long-standing comment) unchanged.
  void setGrayscale(bool on);
  bool grayscale() const { return m_grayscale; }
  bool hasCustomRange(DensityQuantity q) const;
  void customRange(DensityQuantity q, double& lo, double& hi) const;
  void setCustomRange(DensityQuantity q, double lo, double hi);
  void clearCustomRange(DensityQuantity q);
  // The quantity's full (whole-mesh) auto range -- what "Reset Bounds"
  // reverts to in classic FEMM's dialog (cvCDPlotDlg2::OnResbtn2) and what
  // this dialog prefills a custom-range field with the first time it's
  // opened for a quantity that has no custom range set yet.
  void densityQuantityAutoRange(DensityQuantity q, double& lo, double& hi) const;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // exposed so SolutionLegendWidget (SolutionView.cpp) can draw the
  // color-band legend femm.rc's Density Plot always shows (femm/
  // FemmviewView.cpp's "Draw Legend" block) -- that's a DEVICE-space
  // overlay fixed to the viewport corner, not part of this item's own
  // scene-space paint(), so it needs its own small widget rather than
  // being drawn from within paint() itself.
  static int legendBandCount();
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
  // static -- now depends on m_grayscale (the greyscale toggle above), so
  // it needs an instance. SolutionLegendWidget already holds a
  // MeshSolutionItem* (m_item) and calls through it instead.
  QColor legendBandColor(int band) const;
  void legendRange(double& lo, double& hi) const;
  QString legendTitle() const { return legendTitle(m_densityQuantity); }
  // Overload taking an explicit quantity -- lets DensityPlotOptionsDialog
  // ask what label a quantity OTHER than the currently-active one would
  // get (e.g. while the user is still picking one in its combo box,
  // before OK commits the change) without duplicating this method's
  // switch statement.
  QString legendTitle(DensityQuantity q) const;
  PlotMode plotMode() const { return m_mode; }

  // Matches femm/FemmviewView.cpp's OnCplot/IDD_CPLOTDLG(2) -- Number of
  // Contours (default 20, previously hardcoded as paintContour's own
  // kNumLevels constant) and an opt-in custom Lower/Upper Bound override
  // (same "auto range unless overridden" pattern as the Density Plot's
  // per-quantity custom range above; "Restore Default Range" reverts to
  // the whole mesh's Are extremes, i.e. m_aMin/m_aMax).
  void setNumContours(int n);
  int numContours() const { return m_numContours; }
  bool hasCustomContourRange() const { return m_useCustomContourRange; }
  void contourRange(double& lo, double& hi) const;
  void setContourRange(double lo, double hi);
  void clearContourRange();
  void contourAutoRange(double& lo, double& hi) const { lo = m_aMin; hi = m_aMax; }
  // AC solutions only (see ContourPlotOptionsDialog, gated the same way
  // classic gates IDD_CPLOTDLG's "Imaginary component of A" checkbox vs
  // IDD_CPLOTDLG2 not having one at all) -- draws a second set of contour
  // lines for Im(A), in a distinct color, alongside the always-drawn
  // Re(A) lines.
  void setShowImagContour(bool show);
  bool showImagContour() const { return m_showImagContour; }

  private:
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // exposedRect (QStyleOptionGraphicsItem::exposedRect, in this item's
  // own coordinate system -- same as the scene's, this item has no
  // transform of its own) added to every paint* method, per user request
  // ("everything is so slow especially in large geometries" / "is there
  // a way to accelerate the graphics"). The real cost for a huge mesh
  // isn't GPU-vs-CPU rasterization -- it's that every paint call was
  // already iterating and processing every element in the WHOLE mesh
  // regardless of how much of it is actually visible at the current pan/
  // zoom. Skipping any element whose triangle doesn't overlap the
  // exposed rect turns "cost proportional to total mesh size" into "cost
  // proportional to what's on screen", which is where the real win is
  // for a zoomed-in view of a multi-million-element mesh -- switching to
  // a QOpenGLWidget viewport (the literal "GPU acceleration" ask) would
  // speed up the final rasterization step, but not this dominant
  // per-element CPU cost, so it's addressed here first as the higher-
  // value fix. A spatial index (quadtree/grid buckets) would let the
  // exposed-rect check skip iterating off-screen elements entirely
  // rather than just skipping their rendering cost.
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // implemented -- see m_spatialIndex/elementsOverlapping below. Without
  // it, every paint() call (i.e. every frame during interactive pan/zoom)
  // still linearly scanned the WHOLE mesh just to find which elements
  // pass the exposedRect check above; paintDensity alone does that scan
  // twice (local-range pass + fill pass). For a multi-million-element
  // mesh that turns "cost proportional to what's on screen" (the
  // exposedRect skip's stated goal) back into "cost proportional to
  // total mesh size" the moment you zoom in, which is exactly backwards.
  void paintDensity(QPainter* painter, const QRectF& exposedRect);
  void paintContour(QPainter* painter, const QRectF& exposedRect);
  // Matches femm/FemmviewView.cpp's PlotSelectedElm -- drawn unconditionally
  // (like paintProblemGeometry below), regardless of plot mode, so a
  // selected area's highlight is never hidden by whatever fill is active.
  void paintSelectedBlocks(QPainter* painter, const QRectF& exposedRect);
  // Builds one component's (Re or Im) marching-triangle contour path --
  // factored out of paintContour so it can be called twice (once per
  // component) without duplicating the per-element/per-level loop.
  QPainterPath contourPath(const QRectF& exposedRect, double aMin, double span, int numLevels, bool useImag) const;
  void paintMeshOverlay(QPainter* painter, const QRectF& exposedRect);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // uniform-grid spatial index over element bounding boxes, same design
  // as SolutionWindow's own (see its declaration for why a grid beats a
  // quadtree/KD-tree here) but built once in the constructor rather than
  // lazily -- m_solution never changes for this item's lifetime (a fresh
  // MeshSolutionItem is constructed per opened file, see
  // SolutionWindow::openAnsFile), so there's no reload/invalidate case
  // to handle. elementsOverlapping() returns every element whose bbox
  // overlaps a rect, deduped even though one element can span several
  // cells -- dedup uses a generation-stamped m_visitedMark array instead
  // of a QSet so repeated calls (every paint) don't pay hashing/
  // allocation cost proportional to visible element count.
  struct SpatialIndex {
    double minX = 0, minY = 0, cellSize = 1;
    int cols = 0, rows = 0;
    QVector<QVector<int>> cells;
  };
  void buildSpatialIndex();
  QVector<int> elementsOverlapping(const QRectF& rect) const;
  SpatialIndex m_spatialIndex;
  mutable QVector<int> m_visitedMark;
  mutable int m_visitedGen = 0;

  const MeshSolution* m_solution;
  QRectF m_bounds;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // was PlotMode::Density -- per user request, Contour (field lines) is
  // now the default a freshly-opened solution shows, not the filled
  // density plot. Density is still one click/menu-item away, just no
  // longer shown before the user asks for it. Keep in sync with
  // SolutionWindow's densityAction/contourAction setChecked() calls,
  // which drive the View menu/toolbar's initial checked state to match.
  PlotMode m_mode = PlotMode::Contour;
  DensityQuantity m_densityQuantity = DensityQuantity::BMag;
  bool m_smooth = true;
  // See setGrayscale/setCustomRange's declarations above.
  bool m_grayscale = false;
  bool m_useCustomRange[10] = {};
  double m_customLo[10] = {};
  double m_customHi[10] = {};
  bool m_showMesh = false;
  bool m_showPoints = false;
  bool m_showBlockNames = false;
  QSet<int> m_selectedBlockLabels;
  // See setNumContours/setContourRange/setShowImagContour's declarations.
  int m_numContours = 20;
  bool m_useCustomContourRange = false;
  double m_customContourLo = 0, m_customContourHi = 0;
  bool m_showImagContour = false;
  const FemmProblem* m_problemGeometry = nullptr;
  void paintProblemGeometry(QPainter* painter, const QRectF& exposedRect);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // was a single m_nodeBMagAvg (|B| only) -- generalized to one
  // precomputed {per-node average, min, max} triple per DensityQuantity,
  // all computed once in the constructor (bounded, one-time cost, same
  // as the original |B|-only version) rather than rescanning the mesh
  // every time the user switches which quantity is plotted.
  struct QuantityData {
    QVector<double> nodeAvg; // per-node average of touching elements' value -- see below for why
    double vMin = 0, vMax = 0;
  };
  // Indexed by DensityQuantity's underlying int value -- sized via
  // kDensityQuantityCount (see its own comment) rather than a second
  // hand-synced number.
  QuantityData m_quantityData[kDensityQuantityCount];

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // the actual band range paintDensity() used the last time it ran --
  // starts as the global range (matches pre-zoom-rescale behavior until
  // the first paint), updated every paintDensity() call to whatever the
  // CURRENTLY VISIBLE elements' local range is (see that method's own
  // comment). legendRange() reports this instead of the raw global
  // min/max so the legend always matches what's actually on screen.
  double m_lastDensityLo = 0, m_lastDensityHi = 0;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
  // "via GDI's GradientFill" -- confirmed FALSE via an exhaustive grep of
  // femm/ for GradientFill/Gouraud/TRIVERTEX (zero matches). classic's
  // "Smooth" option (femm/FemmviewView.cpp's PlotFluxDensity) instead
  // feeds each triangle's 3 corner NODE-AVERAGED values (vs. the
  // element's own single raw value when off) into a marching-triangle
  // band slicer that cuts the triangle into several flat-colored
  // sub-polygons along the exact iso-value lines between those 3 corner
  // values -- see paintDensity()'s use of sliceTriangleIntoBands
  // (SolutionView.cpp) for the ported version of that slicer.
  double elementQuantity(const MeshSolutionElement& e, DensityQuantity q) const;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // global min/max of nodal Are, precomputed once (paintContour's levels
  // are deliberately fixed to the whole mesh's range, not zoom-adaptive
  // like Density's -- see that method for why) instead of rescanning
  // every node on every paint call.
  double m_aMin = 0, m_aMax = 0;
};

// Matches femm.rc's IDR_FEMMVIEWTYPE View > Show Grid/Snap Grid/Set Grid
// -- a plain QGraphicsScene subclass just for the grid-dot overlay,
// mirroring GeometryScene::drawBackground's own dot-grid pattern exactly
// (same AppTheme::gridLine() color, same fixed-screen-pixel-radius
// drawEllipse() approach -- see that method's comment for why a plain
// QPen-drawn point doesn't render reliably on this app's QOpenGLWidget
// viewports). Snap-to-grid has no effect on anything the Solution Viewer
// lets you place (Point/Contour/Area clicks land on the nearest MESH
// ELEMENT, not a new point) -- matches classic's own CFemmviewView::
// OnSnapGrid, which likewise just flips SnapFlag with no snapping logic
// anywhere in the post-processor's own click handlers; kept as a toggle
// here purely for menu/toolbar-state parity with classic, not because it
// changes any click behavior.
class SolutionGraphicsScene : public QGraphicsScene {
  Q_OBJECT

  public:
  using QGraphicsScene::QGraphicsScene;

  void setShowGrid(bool show);
  bool showGrid() const { return m_showGrid; }
  void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
  bool snapToGrid() const { return m_snapToGrid; }
  void setGridSize(double size);
  double gridSize() const { return m_gridSize; }
  // Read-only: only used so drawBackground can tell whether the opened
  // file's own Coordinates tag was polar, to draw the matching grid style
  // -- see GeometryScene::drawBackground's identical addition/comment.
  // Not a pointer into anything this scene owns or edits (SolutionWindow
  // owns m_problemGeometry's storage); just kept alive by the caller for
  // this scene's lifetime, same convention as MeshSolutionItem's own
  // identically-named pointer.
  void setProblemGeometry(const FemmProblem* problem) { m_problemGeometry = problem; }

  protected:
  void drawBackground(QPainter* painter, const QRectF& rect) override;

  private:
  void resetViewBackgroundCache();

  const FemmProblem* m_problemGeometry = nullptr;
  // Modified by Claude (Anthropic), noreply@anthropic.com: was `true`
  // (matching femm.rc's IDR_FEMMVIEWTYPE Show Grid, checked by default) --
  // see GeometryScene::m_showGrid's identical change for the direct user
  // request behind this.
  bool m_showGrid = false;
  bool m_snapToGrid = false;
  double m_gridSize = 1.0;
};

// Routes plain left-clicks (used by the Point/Contour/Area analysis
// tools) back to SolutionWindow -- QGraphicsView has no built-in "clicked
// at this scene position" signal, and subclassing here is simpler than
// installing an event filter for a single event type.
class SolutionGraphicsView : public QGraphicsView {
  Q_OBJECT

  public:
  explicit SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent = nullptr);

  // Antialiasing was originally gated off below 8x zoom (large meshes:
  // rasterizing millions of triangles with AA on is real, measurable
  // extra cost), re-enabled past that threshold to fix a "swiss cheese"
  // seam artifact between adjacent triangles once each covered only a
  // few screen pixels. Modified by Claude (Anthropic),
  // noreply@anthropic.com, 2026-07-20: that same off-by-default state
  // also meant every zoom level *below* 8x rendered every triangle edge
  // -- both mesh-internal seams and density-band boundaries -- fully
  // aliased, which reads as "rough/undetailed" at completely ordinary
  // zoom levels, not just some extreme case (confirmed: this is what a
  // user actually reported). Per user request, AA is now unconditional
  // here rather than zoom-gated -- the performance concern that
  // motivated gating it off in the first place is much less pressing now
  // that Density (the expensive one to rasterize) defaults to off and is
  // only ever on when a user deliberately turned it on (see
  // MeshSolutionItem::m_mode's default). Revisit with a real element-
  // count-based heuristic instead of this blanket always-on if a huge
  // mesh's Density view is ever reported as sluggish.
  // Call after any operation that changes the view's scale.
  void updateAntialiasingForScale();

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // same fix as GeometryView::fitInViewSafe (see that declaration's
  // comment for the full root-cause writeup, confirmed against a real
  // 88k-node model) -- plain fitInView() still honors this view's
  // AnchorUnderMouse transformationAnchor (needed for wheel-zoom), which
  // re-centers on wherever the mouse happens to be, not the target rect,
  // making "fit to view" silently wrong (in the reported case, "does
  // nothing" visible) whenever the cursor isn't already over the canvas
  // -- exactly the case right after opening a file. Swaps to
  // AnchorViewCenter for the duration of the call.
  void fitInViewSafe(const QRectF& rect);

  // Updates the floating cursor-following tooltip's text (position is
  // maintained internally, following the last mouse move) -- called by
  // SolutionWindow::onCanvasHovered once it's computed the field value,
  // which is throttled (see that function's comment), unlike the
  // tooltip's own position tracking below, which isn't.
  void setTooltipText(const QString& text);

  // Color-band legend overlay, matching femm/FemmviewView.cpp's own
  // Density Plot legend and cv_DPlotDlg2's "Show Legend" checkbox
  // (IDC_CV_SHOW_LEG2) -- see DensityPlotOptionsDialog, which owns that
  // checkbox now rather than this window having its own top-level View >
  // Show Legend toggle. setLegendItem is called once (SolutionWindow::
  // openAnsFile, right after constructing m_item); refreshLegend()
  // re-evaluates visibility/content and must be called after anything
  // that could change what it shows -- plot mode, density quantity, or
  // the legend-visible flag itself.
  void setLegendItem(MeshSolutionItem* item);
  void setLegendVisible(bool visible);
  bool legendVisible() const { return m_legendEnabled; }
  void refreshLegend();

  // Matches femm.rc's IDR_FEMMVIEWTYPE Zoom > Window -- one-shot rubber-
  // band drag (via QRubberBand, screen-space, not a scene item) that
  // fits the view to whatever rectangle was dragged and then reverts to
  // normal click handling. Orthogonal to SolutionToolMode (Point/Contour/
  // Area) rather than folded into it, same as GeometryScene's own
  // ZoomWindow tool mode is deliberately excluded from its exclusive
  // toolGroup -- so starting a Zoom Window drag doesn't disturb whatever
  // Operation-menu tool was active before or after it.
  void startZoomWindow();

  signals:
  void clickedAt(QPointF scenePos);
  // Emitted on every mouse move over the canvas (setMouseTracking is on),
  // for the status bar's live field-value-under-cursor readout -- unlike
  // clickedAt, this isn't gated on the current tool mode; SolutionWindow
  // decides whether/how to use it.
  void hoveredAt(QPointF scenePos);
  // Emitted once a startZoomWindow() drag completes -- see that method's
  // comment.
  void zoomWindowSelected(QRectF sceneRect);
  // femm/FemmeView.cpp's OnKeyDown: Delete removes the last-placed contour
  // point, Escape clears the whole contour. Emitted unconditionally on
  // every Delete/Escape press regardless of tool mode (this view doesn't
  // track SolutionToolMode) -- SolutionWindow's connected slots check
  // m_toolMode == Contour before acting, same division of responsibility
  // as clickedAt/hoveredAt above.
  void removeLastContourPointRequested();
  void clearContourRequested();

  protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20: the
  // legend's numbers now track the CURRENTLY VISIBLE elements' range (see
  // MeshSolutionItem::paintDensity), which changes on every pan/zoom, not
  // just on a plot-mode/quantity switch -- scrollContentsBy is QGraphicsView's
  // one common path for all of those (scrollbar drags, Pan L/R/U/D, wheel
  // zoom's re-centering, fitInView), so re-querying the legend here (a
  // cheap widget repaint, not a mesh rescan) catches all of them from one
  // place instead of threading a refresh call through every zoom/pan entry
  // point individually.
  void scrollContentsBy(int dx, int dy) override;

  private:
  class QLabel* m_cursorTooltip = nullptr;
  class SolutionLegendWidget* m_legend = nullptr;
  MeshSolutionItem* m_legendItem = nullptr;
  bool m_legendEnabled = true;
  class QRubberBand* m_rubberBand = nullptr;
  bool m_zoomWindowActive = false;
  QPoint m_rubberBandOrigin;
  DragPanState m_pan;
};

enum class SolutionToolMode {
  None,
  Point,
  Contour,
  Area,
};

class SolutionWindow : public QMainWindow {
  Q_OBJECT

  public:
  explicit SolutionWindow(QWidget* parent = nullptr);

  void openAnsFile(const QString& path);

  // Renders the loaded solution offscreen to an image, for the
  // `femmqt.exe --render-png` CLI mode that Lua's mi_savepng/mo_savepng
  // shell out to when a script has called setgui("qt"). Draws the SCENE
  // rather than grabbing the view: the viewport is a QOpenGLWidget when
  // built with OpenGL, and grabbing one that was never shown is not
  // reliable. Returns a null image if nothing is loaded.
  QImage renderToImage(QSize size, QRectF source = QRectF());
  // The viewer opens in Contour mode; the offscreen --render-png
  // path needs a way to ask for Density without a menu.
  void selectDensityPlot();


  private slots:
  void onOpenTriggered();
  void onReloadTriggered();
  void onCanvasClicked(QPointF scenePos);
  void onCanvasHovered(QPointF scenePos);
  void onPointToolTriggered();
  void onContourToolTriggered();
  void onAreaToolTriggered();
  void onFinishContourTriggered();
  void onClearContourTriggered();
  void onRemoveLastContourPointTriggered();
  void onClearAreaSelectionTriggered();
  void onPlotXYTriggered();
  void onIntegrateTriggered();
  void onProblemInfoTriggered();
  void onCircuitPropsTriggered();
  void onBhCurvesTriggered();
  void onDensityOptionsTriggered();
  void onContourOptionsTriggered();
  void onZoomIn();
  void onZoomOut();
  void onZoomNatural();
  void onZoomWindowTriggered();
  void onZoomWindowSelected(QRectF sceneRect);
  void onKbdZoomTriggered();
  void onSetGridTriggered();
  void onPanLeft();
  void onPanRight();
  void onPanUp();
  void onPanDown();
  void onCopyBitmapTriggered();
  void onPreferencesTriggered();
  void onPrintTriggered();
  void onPrintPreviewTriggered();
  void onPrintSetupTriggered();
  void onSwitchToClassicTriggered();
  void onOpenRecentFile();
  void onHelpTopicsTriggered();
  void onKeyboardShortcutsTriggered();
  void onLicenseTriggered();
  void onAboutTriggered();

  private:
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // was a linear scan over every element, justified at the time as "only
  // runs once per deliberate click, not per frame" -- true for the
  // Point/Area tools, but onPlotXYTriggered() calls this once per sample
  // point (up to 500), which turns "milliseconds per click" into
  // "500x a full mesh scan" on a huge (multi-million-element) mesh --
  // the exact "if this ever needs to run in a loop" case that comment's
  // own caveat flagged. Now backed by a uniform-grid spatial index
  // (buildSpatialIndex()), built lazily on first use and invalidated by
  // openAnsFile -- see that method's own comment for why a simple grid
  // (not a quadtree/KD-tree) is enough here.
  int findContainingElement(QPointF pt) const;
  struct SpatialIndex {
    double minX = 0, minY = 0, cellSize = 1;
    int cols = 0, rows = 0;
    QVector<QVector<int>> cells; // element indices whose bounding box overlaps each cell
  };
  // Bucket a triangle's bounding box into every grid cell it overlaps --
  // shared between building the index and (implicitly, via the same
  // math) looking a point up in it.
  void buildSpatialIndex() const;
  mutable SpatialIndex m_spatialIndex;
  mutable bool m_spatialIndexBuilt = false;
  // Barycentric-interpolates nodal A within element `elementIndex`
  // (assumed to already contain `pt`, i.e. from findContainingElement).
  std::complex<double> interpolateA(QPointF pt, int elementIndex) const;
  void updateContourVisual();
  void addToRecentFiles(const QString& path);
  void updateRecentFilesMenu();
  QAction* addThemedAction(class QToolBar* bar, const QString& iconPath, const QString& text, const QString& tooltip, void (SolutionWindow::*slot)());
  void refreshToolbarIcons();
  void showContourIntegral();
  void showAreaIntegral();
  // Echoes a Point/Contour/Area result into the persistent Output Window
  // dock, mirroring femm/FemmviewView.cpp's OutputWindowText/IDC_OUTBOX --
  // classic FEMM keeps the *last* result visible in a docked bar instead
  // of only a popup dialog; this keeps a scrollback of all of them, which
  // is strictly more useful and no harder to implement.
  void appendOutput(const QString& text);

  SolutionGraphicsScene* m_scene = nullptr;
  SolutionGraphicsView* m_view = nullptr;
  MeshSolution m_solution;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // the original problem's geometry (nodes/segments/arcs), read once in
  // openAnsFile() via FemmFileIO::readFem() (already proven safe against
  // an .ans file -- see onProblemInfoTriggered()'s identical use) and fed
  // to m_item->setProblemGeometry() so the solution viewer can overlay it
  // like the classic GUI always does. Kept alive here for m_item's
  // lifetime (it only stores a raw pointer to this).
  FemmProblem m_problemGeometry;
  // Set (non-empty) when the fast .ansx-cache load path's best-effort
  // geometry re-read fails -- see openAnsFile()'s comment. Reported in
  // the post-load status-bar message instead of failing silently, so a
  // missing geometry overlay isn't indistinguishable from "there was
  // never any geometry to show."
  QString m_geometryOverlayError;
  MeshSolutionItem* m_item = nullptr;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // classic FEMM labels the raw solved nodal potential differently by
  // coordinate system -- "A ... Wb/m" (planar) vs "Flux ... Wb"
  // (axisymmetric, femm/FemmviewView.cpp's DisplayPointProperties) --
  // same underlying value (MeshSolutionNode::Are/Aim), no unit
  // conversion needed, just the right label. Set once in openAnsFile
  // from whichever load path ran (the slow .ans path already has a
  // FemmProblem; the fast .ansx path gets it via readAnsx's coordSystem
  // out-param) rather than re-reading the source file's header on every
  // hover.
  bool m_axisymmetric = false;
  // Set the same way/place as m_axisymmetric above -- lets
  // onContourOptionsTriggered gate Real/Imaginary component toggles
  // (AC-only, matching femm/FemmviewView.cpp's OnCplot IDD_CPLOTDLG vs
  // IDD_CPLOTDLG2 split on pDoc->Frequency) without a second file read.
  double m_frequency = 0;

  SolutionToolMode m_toolMode = SolutionToolMode::None;
  QAction* m_pointToolAction = nullptr;
  QAction* m_contourToolAction = nullptr;
  QAction* m_areaToolAction = nullptr;
  // Needed by onDensityOptionsTriggered (not just the constructor's own
  // lambdas) to restore the exclusive plot-mode radio-checkmark
  // correctly if the user cancels that dialog after Qt's QActionGroup
  // has already auto-checked densityAction from the click itself.
  QAction* m_densityAction = nullptr;
  QAction* m_contourAction = nullptr;

  QVector<QPointF> m_contourPoints;
  QGraphicsItem* m_contourVisual = nullptr;
  QString m_currentPath;
  QMenu* m_recentFilesMenu = nullptr;

  QDockWidget* m_outputDock = nullptr;
  QPlainTextEdit* m_outputText = nullptr;
  class QLabel* m_positionLabel = nullptr;
  QElapsedTimer m_hoverThrottle; // see onCanvasHovered's comment
  class QPrinter* m_printer = nullptr; // lazily created, shared by Print/Print Preview/Print Setup
  QVector<QPair<QAction*, QString>> m_themedActions; // see MainWindow's identically-named member/refreshToolbarIcons for why
};
