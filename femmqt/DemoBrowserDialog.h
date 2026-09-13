#pragma once

// DemoBrowserDialog.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #90, part of #89).
//
// File > Demo Models. The shipped library, grouped by problem type.
//
// A browser rather than a flat submenu, for a reason that is about the
// content and not about taste: what makes these demos worth opening is
// that each has a closed-form answer to check against. A submenu shows
// thirteen file names and hides every word of that. So the description
// and the analytic reference are on screen next to the selection.
//
// Grouped by problem type because there are four of them now, and a
// heat-flow model and a magnetics model are not alternatives to each
// other in any sense a user cares about.
//
// Opening goes through MainWindow::openDemo, never a plain openFile of
// the installed original -- see DemoLibrary.h for why that distinction
// is the whole point of the feature.

#include "DemoLibrary.h"

#include <QDialog>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

class DemoBrowserDialog : public QDialog {
  Q_OBJECT

  public:
  explicit DemoBrowserDialog(const QVector<DemoLibrary::Demo>& demos,
      QWidget* parent = nullptr);

  // The demo the user chose, valid only after exec() returned Accepted.
  const DemoLibrary::Demo& selected() const { return m_selected; }

  private slots:
  void onSelectionChanged();
  void onOpen();

  private:
  QVector<DemoLibrary::Demo> m_demos;
  DemoLibrary::Demo m_selected;

  QTreeWidget* m_tree = nullptr;
  QLabel* m_description = nullptr;
  QLabel* m_reference = nullptr;
  QPushButton* m_openButton = nullptr;
};
