#include "ConstraintListDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ConstraintListDialog::ConstraintListDialog(Callbacks callbacks, QWidget* parent)
    : QDialog(parent)
    , m_cb(std::move(callbacks))
{
  setWindowTitle("Constraints");
  resize(360, 420);

  m_list = new QListWidget(this);
  connect(m_list, &QListWidget::currentRowChanged, this, &ConstraintListDialog::onSelectionChanged);

  auto* delBtn = new QPushButton("Delete", this);
  connect(delBtn, &QPushButton::clicked, this, &ConstraintListDialog::onDelete);

  auto* buttonCol = new QVBoxLayout;
  buttonCol->addWidget(delBtn);
  buttonCol->addStretch();

  auto* row = new QHBoxLayout;
  row->addWidget(m_list, 1);
  row->addLayout(buttonCol);

  auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(closeBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(row);
  layout->addWidget(closeBox);

  refreshList();
}

void ConstraintListDialog::refreshList()
{
  int prevRow = m_list->currentRow();
  m_list->clear();
  int n = m_cb.count();
  for (int i = 0; i < n; i++)
    m_list->addItem(m_cb.descriptionAt(i));
  if (prevRow >= 0 && prevRow < n)
    m_list->setCurrentRow(prevRow);
}

void ConstraintListDialog::onSelectionChanged()
{
  int row = m_list->currentRow();
  if (row >= 0)
    m_cb.selectOnCanvas(row);
}

void ConstraintListDialog::onDelete()
{
  int row = m_list->currentRow();
  if (row < 0)
    return;
  m_cb.remove(row);
  refreshList();
}
