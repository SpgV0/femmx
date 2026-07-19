#include "PreferencesDialog.h"

#include "AppPreferences.h"
#include "AppTheme.h"

#include <QCheckBox>
#include <QDialogButtonBox>
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
  prefs.save();

  AppTheme::setDark(prefs.darkTheme); // caller checks AppTheme::isDark() to decide whether to refresh its own view

  accept();
}
