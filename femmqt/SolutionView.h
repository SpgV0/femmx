#pragma once

#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QMainWindow>
#include <QPair>
#include <QVector>

#include "MeshSolution.h"

#include <complex>

class QGraphicsScene;
class QAction;
class QDockWidget;
class QToolBar;
class QPlainTextEdit;

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

  // Density (colored |B| bands, the default), Contour (evenly-spaced
  // equipotential/A-contour lines), or Vector (arrows sampled at element
  // centroids) -- mirrors femm.rc's View > Density/Contour/Vector Plot,
  // though this is a first pass at each rather than the classic dialogs'
  // full configurability (band count, arrow scale, etc.).
  enum class PlotMode { Density, Contour, Vector };
  void setPlotMode(PlotMode mode);
  void setSmoothing(bool smooth);
  void setShowMesh(bool show);
  void setShowPoints(bool show);

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
  // (BHpoints==0, LamType==0/LamFill==1, H_c==0) -- femm/Problem.cpp's
  // CMaterialProp::GetMu has additional cases (nonlinear BH-curve
  // materials, laminations, incremental permeability for DC-offset AC
  // problems) not ported here; see elementQuantity()'s .cpp comment for
  // why that's a separate, larger follow-up rather than a silently-wrong
  // approximation for those material types. J = total current density
  // (source + eddy + solved circuit correction, femm/FemmviewDoc.cpp's
  // GetJA, ported faithfully including the circuit term since it reads
  // fkn.exe's own solved output rather than re-deriving it) -- MA/m^2,
  // matching femm.rc's own "|Js+Je|, MA/m^2" label and units.
  enum class DensityQuantity { BMag, BReMag, BImMag, LogBMag, HMag, JMag };
  void setDensityQuantity(DensityQuantity q);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // exposed so SolutionLegendWidget (SolutionView.cpp) can draw the
  // color-band legend femm.rc's Density Plot always shows (femm/
  // FemmviewView.cpp's "Draw Legend" block) -- that's a DEVICE-space
  // overlay fixed to the viewport corner, not part of this item's own
  // scene-space paint(), so it needs its own small widget rather than
  // being drawn from within paint() itself.
  static int legendBandCount();
  static QColor legendBandColor(int band);
  void legendRange(double& lo, double& hi) const;
  QString legendTitle() const;
  PlotMode plotMode() const { return m_mode; }

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
  // rather than just skipping their rendering cost -- a further
  // improvement, not implemented here.
  void paintDensity(QPainter* painter, const QRectF& exposedRect);
  void paintContour(QPainter* painter, const QRectF& exposedRect);
  void paintVector(QPainter* painter, const QRectF& exposedRect);
  void paintMeshOverlay(QPainter* painter, const QRectF& exposedRect);

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
  bool m_showMesh = false;
  bool m_showPoints = false;

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
  // Indexed by DensityQuantity's underlying int value -- kept in sync
  // with that enum's value count by hand (6 now that HMag/JMag exist).
  QuantityData m_quantityData[6];

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // the actual band range paintDensity() used the last time it ran --
  // starts as the global range (matches pre-zoom-rescale behavior until
  // the first paint), updated every paintDensity() call to whatever the
  // CURRENTLY VISIBLE elements' local range is (see that method's own
  // comment). legendRange() reports this instead of the raw global
  // min/max so the legend always matches what's actually on screen.
  double m_lastDensityLo = 0, m_lastDensityHi = 0;

  // femm/FemmviewView.cpp's "Smooth" option colors each triangle using
  // its 3 corner nodes' values (via GDI's GradientFill) instead of one
  // flat per-element value; Qt's QPainter has no equivalent triangle-
  // gradient primitive, so this approximates it by banding on the
  // *average* of the 3 corner nodes' values instead of the element's own
  // single value -- softens the element-to-element steps at shared edges
  // without needing per-pixel rasterization.
  double elementQuantity(const MeshSolutionElement& e, DensityQuantity q) const;
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

  // Updates the floating cursor-following tooltip's text (position is
  // maintained internally, following the last mouse move) -- called by
  // SolutionWindow::onCanvasHovered once it's computed the field value,
  // which is throttled (see that function's comment), unlike the
  // tooltip's own position tracking below, which isn't.
  void setTooltipText(const QString& text);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // color-band legend overlay, matching femm/FemmviewView.cpp's own
  // Density Plot legend ("add a colourmap bar on the side, similar to
  // old gui"). setLegendItem is called once (SolutionWindow::openAnsFile,
  // right after constructing m_item); refreshLegend() re-evaluates
  // visibility/content and must be called after anything that could
  // change what it shows -- plot mode, density quantity, or the
  // Show Legend toggle itself.
  void setLegendItem(MeshSolutionItem* item);
  void setLegendVisible(bool visible);
  void refreshLegend();

  signals:
  void clickedAt(QPointF scenePos);
  // Emitted on every mouse move over the canvas (setMouseTracking is on),
  // for the status bar's live field-value-under-cursor readout -- unlike
  // clickedAt, this isn't gated on the current tool mode; SolutionWindow
  // decides whether/how to use it.
  void hoveredAt(QPointF scenePos);

  protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
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
  void onPlotXYTriggered();
  void onIntegrateTriggered();
  void onProblemInfoTriggered();
  void onCircuitPropsTriggered();
  void onBhCurvesTriggered();
  void onZoomIn();
  void onZoomOut();
  void onZoomNatural();
  void onPanLeft();
  void onPanRight();
  void onPanUp();
  void onPanDown();
  void onCopyBitmapTriggered();
  void onPrintTriggered();
  void onPrintPreviewTriggered();
  void onPrintSetupTriggered();
  void onSwitchToClassicTriggered();
  void onOpenRecentFile();
  void onHelpTopicsTriggered();
  void onLicenseTriggered();
  void onAboutTriggered();

  private:
  // Returns the index of the element containing `pt`, or -1. Linear scan
  // (see the .cpp for why that's an acceptable trade-off here).
  int findContainingElement(QPointF pt) const;
  // Barycentric-interpolates nodal A within element `elementIndex`
  // (assumed to already contain `pt`, i.e. from findContainingElement).
  std::complex<double> interpolateA(QPointF pt, int elementIndex) const;
  void updateContourVisual();
  void addToRecentFiles(const QString& path);
  void updateRecentFilesMenu();
  QAction* addThemedAction(class QToolBar* bar, const QString& iconPath, const QString& text, const QString& tooltip, void (SolutionWindow::*slot)());
  void refreshToolbarIcons();
  void showContourIntegral();
  // Echoes a Point/Contour/Area result into the persistent Output Window
  // dock, mirroring femm/FemmviewView.cpp's OutputWindowText/IDC_OUTBOX --
  // classic FEMM keeps the *last* result visible in a docked bar instead
  // of only a popup dialog; this keeps a scrollback of all of them, which
  // is strictly more useful and no harder to implement.
  void appendOutput(const QString& text);

  QGraphicsScene* m_scene = nullptr;
  SolutionGraphicsView* m_view = nullptr;
  MeshSolution m_solution;
  MeshSolutionItem* m_item = nullptr;

  SolutionToolMode m_toolMode = SolutionToolMode::None;
  QAction* m_pointToolAction = nullptr;
  QAction* m_contourToolAction = nullptr;
  QAction* m_areaToolAction = nullptr;

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
