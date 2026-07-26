#include "PropertyListDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

PropertyListDialog::PropertyListDialog(const QString& title, const QString& itemNounSingular, Callbacks callbacks, QWidget* parent)
    : QDialog(parent)
    , m_cb(std::move(callbacks))
    , m_itemNounSingular(itemNounSingular)
{
  setWindowTitle(title);
  resize(360, 420);

  m_list = new QListWidget(this);
  connect(m_list, &QListWidget::itemDoubleClicked, this, &PropertyListDialog::onEdit);

  auto* addBtn = new QPushButton("Add New", this);
  auto* dupBtn = new QPushButton("Duplicate", this);
  auto* editBtn = new QPushButton("Edit...", this);
  auto* delBtn = new QPushButton("Delete", this);
  connect(addBtn, &QPushButton::clicked, this, &PropertyListDialog::onAdd);
  connect(dupBtn, &QPushButton::clicked, this, &PropertyListDialog::onDuplicate);
  connect(editBtn, &QPushButton::clicked, this, &PropertyListDialog::onEdit);
  connect(delBtn, &QPushButton::clicked, this, &PropertyListDialog::onDelete);

  auto* buttonCol = new QVBoxLayout;
  buttonCol->addWidget(addBtn);
  buttonCol->addWidget(dupBtn);
  buttonCol->addWidget(editBtn);
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

void PropertyListDialog::refreshList()
{
  int prevRow = m_list->currentRow();
  m_list->clear();
  int n = m_cb.count();
  for (int i = 0; i < n; i++)
    m_list->addItem(m_cb.nameAt(i));
  if (prevRow >= 0 && prevRow < n)
    m_list->setCurrentRow(prevRow);
}

void PropertyListDialog::onAdd()
{
  m_cb.addNew();
  refreshList();
  m_list->setCurrentRow(m_list->count() - 1);
}

void PropertyListDialog::onDuplicate()
{
  int row = m_list->currentRow();
  if (row < 0)
    return;
  m_cb.duplicate(row);
  refreshList();
  m_list->setCurrentRow(m_list->count() - 1);
}

void PropertyListDialog::onEdit()
{
  int row = m_list->currentRow();
  if (row < 0)
    return;
  m_cb.editAt(row);
  refreshList();
}

void PropertyListDialog::onDelete()
{
  int row = m_list->currentRow();
  if (row < 0)
    return;

  int refs = m_cb.referenceCount(row);
  QString message = QString("Delete \"%1\"?").arg(m_cb.nameAt(row));
  if (refs > 0) {
    message += QString("\n\n%1 %2 in the geometry currently reference this %3 -- "
                        "they will be reset to have no %3 assigned.")
                   .arg(refs)
                   .arg(refs == 1 ? "entity" : "entities")
                   .arg(m_itemNounSingular);
  }
  auto result = QMessageBox::question(this, "Delete", message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (result != QMessageBox::Yes)
    return;

  m_cb.remove(row);
  refreshList();
}
