#include "PreferencesDialog.h"

#include "AppPreferences.h"
#include "AppTheme.h"

#include "SnapEngine.h"

#include <iterator> // std::size

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle("Preferences");

  AppPreferences prefs = AppPreferences::load();

  auto* layout = new QVBoxLayout(this);

  m_smartMesh = new QCheckBox("Use smart mesh refinement by default (new problems)", this);
  m_smartMesh->setChecked(prefs.smartMesh);
  layout->addWidget(m_smartMesh);

  m_separatePlots = new QCheckBox("Open XY plots in a separate window", this);
  m_separatePlots->setChecked(prefs.separatePlots);
  layout->addWidget(m_separatePlots);

  m_showOutputWindow = new QCheckBox("Show the Output Window", this);
  m_showOutputWindow->setChecked(prefs.showOutputWindow);
  layout->addWidget(m_showOutputWindow);

  m_darkTheme = new QCheckBox("Dark theme", this);
  m_darkTheme->setChecked(AppTheme::isDark());
  layout->addWidget(m_darkTheme);

  // Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12 (#28):
  // per-type object-snap toggles. Grouped and in two columns because
  // nine more checkboxes stacked under the four above would bury them.
  auto* snapGroup = new QGroupBox("Object snapping", this);
  auto* snapGrid = new QGridLayout(snapGroup);

  // Priority order, i.e. the order SnapEngine itself resolves ties in --
  // reading the list top-to-bottom tells you which snap wins when two
  // are in range at once.
  const struct {
    const char* label;
    unsigned flag;
  } kSnapItems[] = {
    { "Endpoint", SnapEngine::SnapEndpoint },
    { "Intersection", SnapEngine::SnapIntersection },
    { "Centre", SnapEngine::SnapCentre },
    { "Midpoint", SnapEngine::SnapMidpoint },
    { "Quadrant", SnapEngine::SnapQuadrant },
    { "Perpendicular", SnapEngine::SnapPerpendicular },
    { "Tangent", SnapEngine::SnapTangent },
    { "Nearest point on edge", SnapEngine::SnapOnEdge },
    { "Grid", SnapEngine::SnapGrid },
  };
  const int rows = (int(std::size(kSnapItems)) + 1) / 2;
  for (int i = 0; i < int(std::size(kSnapItems)); i++) {
    auto* box = new QCheckBox(kSnapItems[i].label, snapGroup);
    box->setChecked((prefs.snapFlags & kSnapItems[i].flag) != 0);
    snapGrid->addWidget(box, i % rows, i / rows);
    m_snapBoxes.append(qMakePair(box, kSnapItems[i].flag));
  }
  // These two measure FROM the point the current operation started, so
  // they do nothing at all unless something is being drawn. Saying so
  // here beats a user ticking one, seeing no change while merely
  // hovering, and concluding snapping is broken.
  snapGroup->setToolTip("Perpendicular and Tangent apply only while drawing, "
                        "since they are measured from the point the current "
                        "operation started.\n"
                        "Hold Alt to suspend snapping for a single point.");
  layout->addWidget(snapGroup);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void PreferencesDialog::onAccept()
{
  AppPreferences prefs = AppPreferences::load(); // re-load so showConsole/defaultDocType (not shown here) round-trip unchanged
  prefs.smartMesh = m_smartMesh->isChecked();
  prefs.separatePlots = m_separatePlots->isChecked();
  prefs.showOutputWindow = m_showOutputWindow->isChecked();
  prefs.darkTheme = m_darkTheme->isChecked();
  unsigned snapFlags = SnapEngine::SnapNone;
  for (const auto& entry : m_snapBoxes) {
    if (entry.first->isChecked())
      snapFlags |= entry.second;
  }
  prefs.snapFlags = snapFlags;
  prefs.save();

  AppTheme::setDark(prefs.darkTheme); // caller checks AppTheme::isDark() to decide whether to refresh its own view

  accept();
}
