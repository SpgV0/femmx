#pragma once

#include <QDialog>
#include <QString>
#include <functional>

class QListWidget;

// Modified by Claude (Anthropic), noreply@anthropic.com: a CAD-style
// "relations panel" listing every constraint currently in the sketch --
// per direct user request ("make a list in a dialog with all constraints
// that you can select on the side"), matching Fusion 360's own Sketch
// Palette relations list. Deliberately simpler than PropertyListDialog
// (no Add New/Edit -- a constraint isn't created by naming/editing a
// struct in a form, it's created by selecting geometry and choosing a
// Constraints-menu action, see MainWindow::applyConstraint) -- just
// Select (highlights the constraint's on-canvas glyph, mirroring a CAD
// relations panel's own click-to-highlight behavior) and Delete. Callback-
// driven rather than taking a FemmProblem/GeometryScene directly, same
// reasoning as PropertyListDialog::Callbacks: the caller (MainWindow) owns
// undo/re-solve/rebuild/markEdited around the actual mutation.
class ConstraintListDialog : public QDialog {
  Q_OBJECT

  public:
  struct Callbacks {
    std::function<int()> count;
    std::function<QString(int)> descriptionAt;
    // Highlights constraint `index`'s on-canvas glyph -- called on every
    // list selection change.
    std::function<void(int)> selectOnCanvas;
    // Deletes constraint `index`. The caller handles undo/re-solve/
    // rebuild (see MainWindow::onConstraintListTriggered).
    std::function<void(int)> remove;
  };

  explicit ConstraintListDialog(Callbacks callbacks, QWidget* parent = nullptr);

  private slots:
  void refreshList();
  void onSelectionChanged();
  void onDelete();

  private:
  Callbacks m_cb;
  QListWidget* m_list = nullptr;
};
