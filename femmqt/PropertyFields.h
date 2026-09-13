#pragma once

// PropertyFields.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #81).
//
// What a single property record looks like in a dialog, described as
// data instead of as a dialog class.
//
// The classic GUI solves this by duplication: MatDlg, bd_MatDlg,
// hd_MatDlg and cd_MatDlg, and the same fourfold split for boundaries,
// point properties and sources -- sixteen dialog classes that differ
// only in which fields they show. #81 asks for one shell per property
// class instead, parameterised by the document's kind, and a field list
// is what parameterises it.
//
// WHERE THESE FIELD SETS COME FROM. The names, units and the exact
// enable/disable behaviour of each boundary-condition type were taken
// from the four classic dialogs (femm/BdryDlg.cpp, bd_BdryDlg.cpp,
// hd_BdryDlg.cpp, cd_BdryDlg.cpp) and from the format tags in
// PropertyCodec.cpp -- not from memory.
//
// One honest gap: the classic BC-type COMBO ITEM NAMES are not
// recoverable from this repository. The combos are CBS_DROPDOWNLIST
// controls that no code ever populates, so their strings lived in the
// original resource's combo data and did not survive into the .rc here.
// What IS recoverable, and what actually matters, is each type's INDEX
// and which fields it enables -- the index is what goes into <BdryType>
// and drives the solver. The indices and gating below are copied from
// those switch statements; the display names follow FEMM's documented
// vocabulary.

#include <QString>
#include <QVector>

#include <functional>

struct FemmProblem;

namespace ProblemKind {
enum class Category;
}

namespace PropertyFields {

// A single editable value in a property record.
struct Field {
  QString label;   // includes the unit, as the classic dialogs do
  QString tooltip; // optional

  // Reading and writing go through the problem so a spec stays valid
  // across a list edit; the index is captured when the spec is built.
  std::function<QString()> get;
  std::function<void(const QString&)> set;

  // Which selector options enable this field. Empty means "always
  // enabled" -- most fields, in most records.
  //
  // This is how the boundary-condition types work: a heat-flow Fixed
  // Temperature boundary enables Tset and nothing else, while Radiation
  // enables five fields. Disabling rather than hiding matches the
  // classic dialogs, and keeps the layout from jumping as the type
  // changes.
  QVector<int> enabledForOptions;

  bool integer = false; // render/validate as a whole number
};

// An optional leading selector -- the boundary-condition type combo, or
// the point-property "prescribed value / point source" choice. `label`
// empty means this record has no selector.
struct Selector {
  QString label;
  QStringList options;
  std::function<int()> get;
  std::function<void(int)> set;

  bool isEmpty() const { return label.isEmpty() || options.isEmpty(); }
};

// Everything a dialog needs to edit record `index` of `category`.
struct Spec {
  QString title;
  QString nameLabel = QStringLiteral("Name:");
  std::function<QString()> getName;
  std::function<void(const QString&)> setName;
  Selector selector;
  QVector<Field> fields;
};

// Builds the spec for one record of the problem's own kind. The returned
// closures hold a reference to `p`, so it must outlive the spec -- which
// it does: the caller is a modal dialog over the open document.
Spec specFor(FemmProblem& p, ProblemKind::Category category, int index);

// Whether a dialog can be built for this (kind, category) yet. Magnetics
// still uses its own hand-written dialogs, which already match the
// classic ones field for field and have behaviour a flat field list does
// not describe (the BH curve editor, the A/I radio pair).
bool hasSpec(const FemmProblem& p, ProblemKind::Category category);

} // namespace PropertyFields
