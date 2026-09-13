#include "DemoBrowserDialog.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// The manifest's problemType, as a heading. It arrives as
// "current flow" or "current_flow" depending on how an entry was
// written, and neither is a heading.
QString headingFor(const QString& problemType)
{
  QString s = problemType;
  s.replace('_', ' ');
  if (s.isEmpty())
    return QStringLiteral("Other");
  s[0] = s[0].toUpper();
  return s;
}

const int kDemoIndexRole = Qt::UserRole + 1;

} // namespace

DemoBrowserDialog::DemoBrowserDialog(const QVector<DemoLibrary::Demo>& demos,
    QWidget* parent)
    : QDialog(parent)
    , m_demos(demos)
{
  setWindowTitle(QStringLiteral("Demo Models"));
  resize(720, 460);

  auto* layout = new QVBoxLayout(this);

  m_tree = new QTreeWidget(this);
  m_tree->setColumnCount(2);
  m_tree->setHeaderLabels({ QStringLiteral("Model"), QStringLiteral("File") });
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  layout->addWidget(m_tree, 1);

  // One group per problem type, in the order the manifest lists them --
  // the corpus is ordered deliberately (the simplest magnetics model
  // first), and re-sorting alphabetically would put "Circular loop"
  // ahead of "Straight wire" and lose that.
  QMap<QString, QTreeWidgetItem*> groups;
  QStringList groupOrder;
  for (int i = 0; i < m_demos.size(); i++) {
    const DemoLibrary::Demo& d = m_demos[i];
    const QString heading = headingFor(d.problemType);
    if (!groups.contains(heading)) {
      auto* g = new QTreeWidgetItem(m_tree);
      g->setText(0, heading);
      g->setFirstColumnSpanned(true);
      g->setFlags(Qt::ItemIsEnabled); // a heading is not selectable
      QFont f = g->font(0);
      f.setBold(true);
      g->setFont(0, f);
      groups.insert(heading, g);
      groupOrder << heading;
    }
    auto* item = new QTreeWidgetItem(groups.value(heading));
    item->setText(0, d.title);
    item->setText(1, QFileInfo(d.file).fileName());
    item->setData(0, kDemoIndexRole, i);
  }
  m_tree->expandAll();

  // The two things that make a demo worth opening, and that a flat
  // submenu could not show.
  m_description = new QLabel(this);
  m_description->setWordWrap(true);
  m_description->setMinimumHeight(48);
  m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_description);

  m_reference = new QLabel(this);
  m_reference->setWordWrap(true);
  m_reference->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_reference);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  m_openButton = buttons->addButton(QStringLiteral("Open Copy"),
      QDialogButtonBox::AcceptRole);
  m_openButton->setEnabled(false);
  // Says what will happen, because it is not what a user expects from a
  // file browser and the window title will say "(demo copy)".
  m_openButton->setToolTip(
      QStringLiteral("Opens a working copy. The installed demo is never "
                     "modified, so Save will ask where to keep your changes."));
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &DemoBrowserDialog::onOpen);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
      &DemoBrowserDialog::onSelectionChanged);
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
      [this](QTreeWidgetItem* item, int) {
        if (item && item->data(0, kDemoIndexRole).isValid())
          onOpen();
      });

  onSelectionChanged();
}

void DemoBrowserDialog::onSelectionChanged()
{
  const QList<QTreeWidgetItem*> chosen = m_tree->selectedItems();
  const QVariant index = chosen.isEmpty() ? QVariant()
                                          : chosen.first()->data(0, kDemoIndexRole);
  if (!index.isValid()) {
    m_description->setText(QStringLiteral("Select a model to see what it "
                                          "demonstrates."));
    m_reference->clear();
    m_openButton->setEnabled(false);
    return;
  }

  const DemoLibrary::Demo& d = m_demos.at(index.toInt());
  m_description->setText(d.description);
  m_reference->setText(d.analyticReference.isEmpty()
          ? QString()
          : QStringLiteral("Analytic reference:  %1").arg(d.analyticReference));
  m_openButton->setEnabled(true);
}

void DemoBrowserDialog::onOpen()
{
  const QList<QTreeWidgetItem*> chosen = m_tree->selectedItems();
  if (chosen.isEmpty())
    return;
  const QVariant index = chosen.first()->data(0, kDemoIndexRole);
  if (!index.isValid())
    return; // a group heading
  m_selected = m_demos.at(index.toInt());
  accept();
}
