#include "PlotStateArgs.h"

namespace {

// In MeshSolutionItem::DensityQuantity order: BMag, BReMag, BImMag,
// HMag, HReMag, HImMag, JMag, JReMag, JImMag, LogBMag.
//
// The aliases are the ones femm/femmviewLua.cpp's lua_showdensity
// accepts, so a script and a command line can say the same words.
struct QuantityName {
  const char* name;
  int index;
};

const QuantityName kQuantityNames[] = {
  { "bmag", 0 }, { "mag", 0 },
  { "breal", 1 }, { "real", 1 },
  { "bimag", 2 }, { "imag", 2 },
  { "hmag", 3 },
  { "hreal", 4 },
  { "himag", 5 },
  { "jmag", 6 },
  { "jreal", 7 },
  { "jimag", 8 },
  { "logb", 9 },
};

// The canonical spelling per index -- the first entry above for each.
const char* const kCanonical[10] = { "bmag", "breal", "bimag", "hmag", "hreal",
  "himag", "jmag", "jreal", "jimag", "logb" };

bool takeDouble(const QStringList& args, int& i, const QString& option,
    double& out, QString& error)
{
  if (i + 1 >= args.size()) {
    error = QStringLiteral("%1 needs a number").arg(option);
    return false;
  }
  bool ok = false;
  const double v = args.at(++i).toDouble(&ok);
  if (!ok) {
    error = QStringLiteral("%1: \"%2\" is not a number").arg(option, args.at(i));
    return false;
  }
  out = v;
  return true;
}

bool takeFlag(const QStringList& args, int& i, const QString& option,
    int& out, QString& error)
{
  if (i + 1 >= args.size()) {
    error = QStringLiteral("%1 needs 0 or 1").arg(option);
    return false;
  }
  const QString v = args.at(++i);
  if (v == QLatin1String("0") || v.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
    out = 0;
    return true;
  }
  if (v == QLatin1String("1") || v.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0) {
    out = 1;
    return true;
  }
  error = QStringLiteral("%1: expected 0 or 1, got \"%2\"").arg(option, v);
  return false;
}

} // namespace

int PlotStateArgs::quantityFromName(const QString& name)
{
  const QString key = name.trimmed().toLower();
  for (const QuantityName& q : kQuantityNames) {
    if (key == QLatin1String(q.name))
      return q.index;
  }
  return -1;
}

QString PlotStateArgs::nameForQuantity(int index)
{
  if (index < 0 || index >= 10)
    return QString();
  return QString::fromLatin1(kCanonical[index]);
}

bool PlotStateArgs::parse(const QStringList& args, PlotState& out, QString& error)
{
  for (int i = 0; i < args.size(); i++) {
    const QString a = args.at(i);
    // The size and crop arguments are positional and share this list.
    if (!a.startsWith(QLatin1String("--")))
      continue;

    if (a == QLatin1String("--density")) {
      out.mode = PlotState::Mode::Density;
    } else if (a == QLatin1String("--contour")) {
      out.mode = PlotState::Mode::Contour;
    } else if (a == QLatin1String("--quantity")) {
      if (i + 1 >= args.size()) {
        error = QStringLiteral("--quantity needs a name");
        return false;
      }
      const QString name = args.at(++i);
      const int q = quantityFromName(name);
      if (q < 0) {
        error = QStringLiteral("--quantity: \"%1\" is not one of bmag, breal, "
                               "bimag, hmag, hreal, himag, jmag, jreal, jimag, "
                               "logb")
                    .arg(name);
        return false;
      }
      out.quantity = q;
    } else if (a == QLatin1String("--bounds")) {
      double lo = 0, hi = 0;
      if (!takeDouble(args, i, a, lo, error) || !takeDouble(args, i, a, hi, error))
        return false;
      // Same normalisation lua_showdensity does before storing them, so
      // a script that passes them the other way round gets the same
      // picture from either GUI rather than an empty range from one.
      out.lower = qMin(lo, hi);
      out.upper = qMax(lo, hi);
      out.haveBounds = true;
    } else if (a == QLatin1String("--contour-bounds")) {
      double lo = 0, hi = 0;
      if (!takeDouble(args, i, a, lo, error) || !takeDouble(args, i, a, hi, error))
        return false;
      out.contourLower = qMin(lo, hi);
      out.contourUpper = qMax(lo, hi);
      out.haveContourBounds = true;
    } else if (a == QLatin1String("--greyscale") || a == QLatin1String("--grayscale")) {
      if (!takeFlag(args, i, a, out.greyscale, error))
        return false;
    } else if (a == QLatin1String("--legend")) {
      if (!takeFlag(args, i, a, out.legend, error))
        return false;
    } else if (a == QLatin1String("--contours")) {
      double n = 0;
      if (!takeDouble(args, i, a, n, error))
        return false;
      if (n < 1) {
        error = QStringLiteral("--contours: %1 contour lines is not a plot")
                    .arg(args.at(i));
        return false;
      }
      out.numContours = (int)n;
    } else {
      // Deliberately fatal. The whole point of this option set is that
      // a render silently ignoring what it was asked for is the defect;
      // a typo that fell through to defaults would be the same bug.
      error = QStringLiteral("unknown option \"%1\"").arg(a);
      return false;
    }
  }
  return true;
}

QStringList PlotStateArgs::toArguments(const PlotState& state)
{
  QStringList args;
  if (state.mode == PlotState::Mode::Density)
    args << QStringLiteral("--density");
  else if (state.mode == PlotState::Mode::Contour)
    args << QStringLiteral("--contour");

  if (state.quantity >= 0)
    args << QStringLiteral("--quantity") << nameForQuantity(state.quantity);

  if (state.haveBounds) {
    args << QStringLiteral("--bounds")
         << QString::number(state.lower, 'g', 10)
         << QString::number(state.upper, 'g', 10);
  }
  if (state.greyscale >= 0)
    args << QStringLiteral("--greyscale") << QString::number(state.greyscale);
  if (state.legend >= 0)
    args << QStringLiteral("--legend") << QString::number(state.legend);
  if (state.numContours > 0)
    args << QStringLiteral("--contours") << QString::number(state.numContours);
  if (state.haveContourBounds) {
    args << QStringLiteral("--contour-bounds")
         << QString::number(state.contourLower, 'g', 10)
         << QString::number(state.contourUpper, 'g', 10);
  }
  return args;
}
