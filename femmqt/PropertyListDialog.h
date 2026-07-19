#pragma once

#include <QDialog>
#include <QString>
#include <functional>

class QListWidget;

// Generic list-management UI (Add New / Duplicate / Edit / Delete / Close)
// shared by the Material/Boundary/Circuit/PointProp property libraries --
// each is, at this UI layer, the same CRUD-over-a-named-list pattern; only
// the item type and its edit dialog differ, so those are supplied as
// callbacks instead of writing four nearly-identical QDialog subclasses.
// Not templated on the item type itself (a QObject-derived class can't be
// a class template in Qt without extra moc machinery) -- type erasure via
// std::function is the simpler option here.
class PropertyListDialog : public QDialog {
  Q_OBJECT

  public:
  struct Callbacks {
    std::function<int()> count;
    std::function<QString(int)> nameAt;
    // Opens a modal edit dialog for the item at `index`, mutating it in
    // place; returns once the dialog closes (accepted or cancelled).
    std::function<void(int)> editAt;
    std::function<void()> addNew;
    std::function<void(int)> duplicate;
    // Number of geometry entities (segments/arcs/block labels/nodes)
    // currently referencing the item at `index` -- shown in the delete
    // confirmation prompt so the user knows what un-assigning will affect.
    std::function<int(int)> referenceCount;
    // Removes the item at `index` and renumbers/clears every reference to
    // it and to items after it (see FemmProblemEdit's delete*Prop
    // functions) -- called only after the user confirms.
    std::function<void(int)> remove;
  };

  PropertyListDialog(const QString& title, const QString& itemNounSingular, Callbacks callbacks, QWidget* parent = nullptr);

  private slots:
  void refreshList();
  void onAdd();
  void onDuplicate();
  void onEdit();
  void onDelete();

  private:
  Callbacks m_cb;
  QString m_itemNounSingular;
  QListWidget* m_list = nullptr;
};
