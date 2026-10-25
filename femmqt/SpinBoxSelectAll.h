#pragma once

class QApplication;

// Makes every spin box in the application select its whole value when it
// is clicked into or double-clicked, so typing replaces the number
// (#122). Out of the box a click puts a caret in the value and a
// double-click selects one "word" of it -- "75" of "-75.90" -- so typing
// 90 produced -90.90 or worse. Tab focus already selected all.
//
// Idempotent: installing twice adds one filter.
void installSpinBoxSelectAll(QApplication* app);
