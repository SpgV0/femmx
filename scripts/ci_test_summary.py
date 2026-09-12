"""
Render a pytest JUnit XML file as a GitHub Actions job summary.

Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12 (issue
#22). CI already produced junit.xml and uploaded it as an artifact, which
means the only way to find out which test failed was to download a zip
and open it -- so in practice nobody did, and a red run was read as "CI
is broken" rather than "this assertion failed".

Writes Markdown to the file named by $GITHUB_STEP_SUMMARY (or stdout when
run locally), so the result appears on the run's own page.

Usage:
    python scripts/ci_test_summary.py test/results/junit.xml [more.xml ...]
"""

import os
import sys
import xml.etree.ElementTree as ET

# Failure text can be a full traceback; a summary page is not the place
# for all of it, and GitHub truncates the whole summary at 1 MiB.
MAX_MESSAGE_CHARS = 500
MAX_FAILURES_SHOWN = 25


def _iter_suites(root):
    if root.tag == "testsuites":
        for suite in root:
            yield suite
    else:
        yield root


def collect(paths):
    totals = {"tests": 0, "failures": 0, "errors": 0, "skipped": 0, "time": 0.0}
    problems = []
    skipped = []
    for path in paths:
        if not os.path.exists(path):
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as exc:
            problems.append(("(could not parse %s)" % path, "ParseError",
                             str(exc)))
            continue
        for suite in _iter_suites(root):
            for key in ("tests", "failures", "errors", "skipped"):
                totals[key] += int(suite.get(key, 0) or 0)
            totals["time"] += float(suite.get("time", 0) or 0)
            for case in suite.iter("testcase"):
                name = "%s::%s" % (case.get("classname", ""), case.get("name", ""))
                for kind in ("failure", "error"):
                    node = case.find(kind)
                    if node is not None:
                        text = (node.get("message") or node.text or "").strip()
                        problems.append((name, kind, text))
                node = case.find("skipped")
                if node is not None:
                    skipped.append((name, (node.get("message") or "").strip()))
    return totals, problems, skipped


def render(totals, problems, skipped, title="Test results"):
    passed = totals["tests"] - totals["failures"] - totals["errors"] - totals["skipped"]
    bad = totals["failures"] + totals["errors"]
    icon = "x" if bad else ("warning" if not totals["tests"] else "white_check_mark")

    out = []
    out.append("## :%s: %s" % (icon, title))
    out.append("")
    out.append("| Passed | Failed | Errors | Skipped | Total | Time |")
    out.append("| ---: | ---: | ---: | ---: | ---: | ---: |")
    out.append("| %d | %d | %d | %d | %d | %.1fs |"
               % (passed, totals["failures"], totals["errors"],
                  totals["skipped"], totals["tests"], totals["time"]))
    out.append("")

    if problems:
        out.append("### Failures")
        out.append("")
        for name, kind, text in problems[:MAX_FAILURES_SHOWN]:
            snippet = text.replace("\r", "")
            if len(snippet) > MAX_MESSAGE_CHARS:
                snippet = snippet[:MAX_MESSAGE_CHARS] + "\n... (truncated)"
            out.append("<details><summary><code>%s</code> &mdash; %s</summary>"
                       % (name, kind))
            out.append("")
            out.append("```")
            out.append(snippet)
            out.append("```")
            out.append("</details>")
            out.append("")
        if len(problems) > MAX_FAILURES_SHOWN:
            out.append("_...and %d more; see the uploaded test-results "
                       "artifact._" % (len(problems) - MAX_FAILURES_SHOWN))
            out.append("")

    if skipped:
        # Skips are listed because in this suite they are load-bearing: a
        # test that skips because COM is unavailable looks exactly like a
        # passing run unless someone says so.
        out.append("<details><summary>%d skipped</summary>" % len(skipped))
        out.append("")
        for name, reason in skipped:
            out.append("- `%s` &mdash; %s" % (name, reason or "no reason given"))
        out.append("")
        out.append("</details>")
        out.append("")

    return "\n".join(out)


def main(argv):
    paths = argv[1:]
    if not paths:
        print("usage: ci_test_summary.py <junit.xml> [...]", file=sys.stderr)
        return 2
    totals, problems, skipped = collect(paths)
    if totals["tests"] == 0 and not problems:
        text = ("## :warning: Test results\n\nNo test results were found in: "
                + ", ".join("`%s`" % p for p in paths) + "\n")
    else:
        text = render(totals, problems, skipped)

    target = os.environ.get("GITHUB_STEP_SUMMARY")
    if target:
        with open(target, "a", encoding="utf-8") as fh:
            fh.write(text + "\n")
    else:
        sys.stdout.write(text + "\n")
    # Always 0: this reports on the run, it does not decide it. The pytest
    # step already set the job's exit code, and a summary that could fail
    # the job would mean a formatting bug masquerading as a test failure.
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
