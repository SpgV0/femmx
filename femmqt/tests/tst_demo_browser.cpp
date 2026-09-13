// tst_demo_browser.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #90).
//
// The Demo Models browser, and the two things it must not do.
//
// It must not open the installed original -- that is #91's guarantee,
// and the browser is the one code path that would casually break it by
// calling openFile() with a manifest path, which looks entirely
// reasonable and is not.
//
// And it must degrade quietly. A build with no demos directory, or a
// manifest that is not JSON, or a manifest naming files nobody
// installed, all have to end in a disabled menu item -- never an error
// dialog, which on a headless run is a hang (#85). None of those is
// reachable in a tree that has a perfectly good demos/ in it, which is
// why DemoLibrary::directory() honours FEMMQT_DEMOS_DIR.

#include <QtTest>

#include "DemoBrowserDialog.h"
#include "DemoLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace {

QString repoRoot()
{
  return QFileInfo(QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath())
      .absolutePath();
}

// The leaf items, in display order. Group headings carry no demo index.
QVector<QTreeWidgetItem*> leaves(QTreeWidget* tree)
{
  QVector<QTreeWidgetItem*> out;
  for (int i = 0; i < tree->topLevelItemCount(); i++) {
    QTreeWidgetItem* group = tree->topLevelItem(i);
    for (int j = 0; j < group->childCount(); j++)
      out << group->child(j);
  }
  return out;
}

void writeFile(const QString& path, const QByteArray& content)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write(content);
  f.close();
}

} // namespace

class TestDemoBrowser : public QObject
{
  Q_OBJECT

  private slots:
  void cleanup();

  void theBrowserListsEveryDemoGroupedByProblemType();
  void aGroupHeadingIsNotSomethingYouCanOpen();
  void selectingADemoShowsWhatItDemonstrates();
  void openingReturnsTheSelectedDemo();

  void aMissingLibraryIsEmptyAndExplained();
  void aManifestThatIsNotJsonIsEmptyAndExplained();
  void aManifestNamingAbsentFilesListsOnlyWhatIsThere();

  void theMenuOpensACopyAndNeverTheOriginal();

  private:
  QVector<DemoLibrary::Demo> realDemos();
};

void TestDemoBrowser::cleanup()
{
  qunsetenv("FEMMQT_DEMOS_DIR");
}

QVector<DemoLibrary::Demo> TestDemoBrowser::realDemos()
{
  qputenv("FEMMQT_DEMOS_DIR", (repoRoot() + "/demos").toLocal8Bit());
  QString error;
  const QVector<DemoLibrary::Demo> demos = DemoLibrary::load(error);
  return demos;
}

// ---------------------------------------------------------------------------
// The browser
// ---------------------------------------------------------------------------

void TestDemoBrowser::theBrowserListsEveryDemoGroupedByProblemType()
{
  const QVector<DemoLibrary::Demo> demos = realDemos();
  QVERIFY2(!demos.isEmpty(), "the demo corpus did not load");

  DemoBrowserDialog dialog(demos);
  QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
  QVERIFY(tree);

  // Every demo is reachable. A browser that quietly drops one is worse
  // than a submenu, because nothing about it looks wrong.
  QCOMPARE(leaves(tree).size(), demos.size());

  QSet<QString> types;
  for (const DemoLibrary::Demo& d : demos)
    types << d.problemType.toLower().replace('_', ' ');
  QCOMPARE(tree->topLevelItemCount(), types.size());

  // Order is the manifest's, not alphabetical: the corpus is ordered
  // deliberately -- simplest model first -- and sorting would put
  // "Circular loop" ahead of "Straight wire" and lose that.
  QStringList shown;
  for (QTreeWidgetItem* item : leaves(tree))
    shown << item->text(0);
  QStringList expected;
  for (int i = 0; i < tree->topLevelItemCount(); i++) {
    const QString heading = tree->topLevelItem(i)->text(0).toLower();
    for (const DemoLibrary::Demo& d : demos) {
      if (d.problemType.toLower().replace('_', ' ') == heading)
        expected << d.title;
    }
  }
  QCOMPARE(shown, expected);
}

void TestDemoBrowser::aGroupHeadingIsNotSomethingYouCanOpen()
{
  const QVector<DemoLibrary::Demo> demos = realDemos();
  QVERIFY(!demos.isEmpty());

  DemoBrowserDialog dialog(demos);
  QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
  QVERIFY(tree);

  QPushButton* open = nullptr;
  for (QPushButton* b : dialog.findChildren<QPushButton*>()) {
    if (b->text().contains("Open"))
      open = b;
  }
  QVERIFY2(open, "no Open button");

  // Nothing selected yet.
  QVERIFY2(!open->isEnabled(), "Open was enabled with nothing selected");

  // A heading is a category, not a model. Selecting one and pressing
  // Open would either do nothing or open whatever happened to be first.
  QTreeWidgetItem* heading = tree->topLevelItem(0);
  QVERIFY(heading);
  QVERIFY2(!(heading->flags() & Qt::ItemIsSelectable),
      "a problem-type heading is selectable");
  tree->setCurrentItem(heading);
  QVERIFY2(!open->isEnabled(), "Open was enabled for a group heading");
}

void TestDemoBrowser::selectingADemoShowsWhatItDemonstrates()
{
  const QVector<DemoLibrary::Demo> demos = realDemos();
  QVERIFY(!demos.isEmpty());

  DemoBrowserDialog dialog(demos);
  QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
  QVERIFY(tree);
  const QVector<QTreeWidgetItem*> items = leaves(tree);
  QVERIFY(!items.isEmpty());

  // Find a demo that has an analytic reference -- that is the thing
  // that makes this library checkable rather than decorative, and the
  // reason for a browser instead of a submenu.
  int withReference = -1;
  for (int i = 0; i < demos.size(); i++) {
    if (!demos[i].analyticReference.isEmpty()) {
      withReference = i;
      break;
    }
  }
  QVERIFY2(withReference >= 0, "no demo carries an analytic reference");

  QTreeWidgetItem* target = nullptr;
  for (QTreeWidgetItem* item : items) {
    if (item->text(0) == demos[withReference].title)
      target = item;
  }
  QVERIFY(target);
  tree->setCurrentItem(target);

  QStringList labelText;
  for (QLabel* l : dialog.findChildren<QLabel*>())
    labelText << l->text();
  const QString all = labelText.join('\n');

  QVERIFY2(all.contains(demos[withReference].description),
      qPrintable("the description is not on screen: " + all));
  QVERIFY2(all.contains(demos[withReference].analyticReference),
      qPrintable("the analytic reference is not on screen: " + all));
}

void TestDemoBrowser::openingReturnsTheSelectedDemo()
{
  const QVector<DemoLibrary::Demo> demos = realDemos();
  QVERIFY(demos.size() >= 2);

  DemoBrowserDialog dialog(demos);
  QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
  QVERIFY(tree);
  const QVector<QTreeWidgetItem*> items = leaves(tree);
  QVERIFY(items.size() >= 2);

  // The second one, not the first: picking the first would pass even if
  // selected() always returned demos.first().
  tree->setCurrentItem(items[1]);

  QPushButton* open = nullptr;
  for (QPushButton* b : dialog.findChildren<QPushButton*>()) {
    if (b->text().contains("Open"))
      open = b;
  }
  QVERIFY(open && open->isEnabled());
  open->click();

  QCOMPARE(dialog.result(), (int)QDialog::Accepted);
  QCOMPARE(dialog.selected().title, items[1]->text(0));
  QVERIFY(!dialog.selected().file.isEmpty());
}

// ---------------------------------------------------------------------------
// Degrading quietly
// ---------------------------------------------------------------------------

void TestDemoBrowser::aMissingLibraryIsEmptyAndExplained()
{
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  qputenv("FEMMQT_DEMOS_DIR", tmp.filePath("not-there").toLocal8Bit());

  QString error;
  const QVector<DemoLibrary::Demo> demos = DemoLibrary::load(error);
  QVERIFY2(demos.isEmpty(), "demos appeared out of an empty directory");
  QVERIFY2(!error.isEmpty(),
      "no reason given -- the menu item's tooltip is built from this, so an "
      "empty string would leave the user with a greyed-out item and no clue");
}

void TestDemoBrowser::aManifestThatIsNotJsonIsEmptyAndExplained()
{
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath("demos.json"), "this is not json {{{");
  qputenv("FEMMQT_DEMOS_DIR", tmp.path().toLocal8Bit());

  QString error;
  const QVector<DemoLibrary::Demo> demos = DemoLibrary::load(error);
  QVERIFY2(demos.isEmpty(), "a corrupt manifest produced demos");
  QVERIFY2(error.contains("JSON", Qt::CaseInsensitive),
      qPrintable("the reason does not mention the manifest: " + error));
}

void TestDemoBrowser::aManifestNamingAbsentFilesListsOnlyWhatIsThere()
{
  // A row that cannot be opened is worse than a shorter list: the user
  // clicks it and gets an error for something they did not do.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writeFile(tmp.filePath("magnetics/present.fem"), "[Format] = 4.0\n");
  writeFile(tmp.filePath("demos.json"), R"({
    "formatVersion": 1,
    "demos": [
      {"file": "magnetics/present.fem", "title": "Present",
       "problemType": "magnetics", "description": "here"},
      {"file": "magnetics/absent.fem", "title": "Absent",
       "problemType": "magnetics", "description": "not here"}
    ]
  })");
  qputenv("FEMMQT_DEMOS_DIR", tmp.path().toLocal8Bit());

  QString error;
  const QVector<DemoLibrary::Demo> demos = DemoLibrary::load(error);
  QCOMPARE(demos.size(), 1);
  QCOMPARE(demos.first().title, QStringLiteral("Present"));
}

// ---------------------------------------------------------------------------
// The line back to #91
// ---------------------------------------------------------------------------

void TestDemoBrowser::theMenuOpensACopyAndNeverTheOriginal()
{
  // The browser hands back a manifest path, and calling openFile() with
  // it is the single most natural mistake available here -- it would
  // compile, work, and quietly undo #91's entire guarantee, because
  // three separate code paths write beside whatever model is open.
  QFile f(QStringLiteral(FEMMQT_SOURCE_DIR) + "/MainWindow.cpp");
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "MainWindow.cpp did not read");
  const QString code = QString::fromUtf8(f.readAll());

  const int at = code.indexOf(QStringLiteral("void MainWindow::onDemoModelsTriggered"));
  QVERIFY2(at > 0, "the Demo Models handler is gone");
  int end = code.indexOf(QStringLiteral("\nvoid MainWindow::"), at + 10);
  if (end < 0)
    end = code.size();
  QString body = code.mid(at, end - at);
  body.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));

  QVERIFY2(body.contains(QStringLiteral("openDemo(")),
      "the Demo Models handler does not call openDemo");
  QVERIFY2(!body.contains(QStringLiteral("openFile(")),
      "the Demo Models handler calls openFile with a manifest path, which opens "
      "the installed original -- the .femx cache, the .fes sidecar and the "
      "whole solve pipeline would then write into the demos directory (#91)");
}

// Widgets, so a QPA platform is needed; the CMake harness runs this
// offscreen like the other widget suites.
QTEST_MAIN(TestDemoBrowser)
#include "tst_demo_browser.moc"
