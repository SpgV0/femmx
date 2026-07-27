#pragma once

#include <QDialog>

class QLineEdit;
class QRadioButton;

// Qt port of femm/CopyDlg.h/.cpp's CCopyDlg (IDD_COPYDLG, CAPTION
// "Copy/Move") -- ONE dialog shared by both Move and Copy, exactly like
// classic's: a Rotation/Translation radio picks which field group is
// active (About Point + Angular Shift, or Horizontal/Vertical Shift), plus
// a Number of Copies field that's disabled (fixed at 0, meaning "just
// move/rotate in place, no copies") for Move and enabled (default 1) for
// Copy. Previously this was a sequence of separate single-value
// QInputDialogs with no Rotate option and no way to make more than one
// copy at all -- replaced to match classic's real capability, not just
// its field count.
class MoveCopyDialog : public QDialog {
  Q_OBJECT

  public:
  enum class TransformMode { Rotate, Translate };

  explicit MoveCopyDialog(bool isMove, QWidget* parent = nullptr);

  TransformMode transformMode() const;
  double aboutX() const;
  double aboutY() const;
  double shiftAngleDeg() const;
  double deltaX() const;
  double deltaY() const;
  // 0 for Move (the field is disabled and ignored); >=1 for Copy.
  int numCopies() const;

  private slots:
  void onRotateChecked();
  void onTranslateChecked();

  private:
  bool m_isMove;
  QRadioButton* m_rotate = nullptr;
  QRadioButton* m_translate = nullptr;
  QLineEdit* m_shiftAngle = nullptr;
  QLineEdit* m_aboutX = nullptr;
  QLineEdit* m_aboutY = nullptr;
  QLineEdit* m_deltaX = nullptr;
  QLineEdit* m_deltaY = nullptr;
  QLineEdit* m_numCopies = nullptr;
};
