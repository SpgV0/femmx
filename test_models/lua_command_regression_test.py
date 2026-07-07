"""
Regression test sweep over FEMM's Lua-scripting command surface, as
exposed by pyfemm.

FEMM has four problem types, each with a near-identical pair of Lua
"prefixes" -- an input/pre-processor (mi_/ei_/hi_/ci_) and an output/
post-processor (mo_/eo_/ho_/co_):

    magnetics       mi_ / mo_
    electrostatics  ei_ / eo_
    heat flow       hi_ / ho_
    current flow    ci_ / co_

This script drives a realistic modeling workflow (draw geometry, assign
properties, select/copy/move/mirror/scale, mesh, solve, post-process,
save/round-trip) through each problem type's command set, and tracks
every single command call: PASS (no exception), FAIL (raised an
exception -- message recorded), or SKIP (not attempted, with a reason --
e.g. it opens a blocking modal dialog, or it's pure Python-side plumbing
in pyfemm rather than an actual Lua command).

This is deliberately not a correctness test of FEMM's solvers -- it is a
smoke/regression test that the Lua command surface still works after a
source change (e.g. the redraw-suppression and EnforcePSLG fixes in this
fork, which touch the shared node/segment/arc/block-label editing code
paths used by every problem type's editor).

Coverage is most rigorous for magnetics (mi_/mo_), since that is the
editor this fork actually modifies (femm/femmeLua.cpp, femm/MOVECOPY.CPP,
femm/FemmeView.cpp). The electrostatics/heat-flow/current-flow argument
lists follow the FEMM Lua manual's documented conventions but are less
exhaustively hand-verified; genuine argument mismatches will show up as
FAIL entries in the report rather than being silently swallowed.

Requirements: pip install pyfemm pywin32; a built + COM-registered
femm.exe.

Usage:
    python lua_command_regression_test.py
"""

import os
import time

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "lua_command_regression")
RESULTS_DIR = OUTPUT_DIR
RESULTS_PATH = os.path.join(RESULTS_DIR, "lua_command_regression.txt")

# Not real Lua commands -- pure Python-side helpers/plumbing inside pyfemm.
NOT_LUA_COMMANDS = {
    "callfemm", "callfemm_noeval", "fixpath", "doargs", "num", "numc",
    "quote", "quotec", "complex2str",
}

# Real Lua commands that open a blocking modal dialog or otherwise cannot
# run unattended.
BLOCKING_COMMANDS = {"prompt", "messagebox", "create"}


class Tracker:
    def __init__(self):
        self.records = []  # (label, status, detail)

    def call(self, label, fn, *args):
        if fn is None:
            self.records.append((label, "SKIP", "not exposed by pyfemm"))
            return None
        try:
            result = fn(*args)
            self.records.append((label, "PASS", ""))
            return result
        except Exception as e:  # noqa: BLE001 - deliberately broad: this is a call-sweep
            self.records.append((label, "FAIL", str(e)))
            return None

    def skip(self, label, reason):
        self.records.append((label, "SKIP", reason))

    def summary(self):
        n_pass = sum(1 for _, s, _ in self.records if s == "PASS")
        n_fail = sum(1 for _, s, _ in self.records if s == "FAIL")
        n_skip = sum(1 for _, s, _ in self.records if s == "SKIP")
        return n_pass, n_fail, n_skip


def c(tracker, prefix, name, *args):
    """Look up f"{prefix}_{name}" on the femm module and call it through the tracker."""
    label = f"{prefix}_{name}"
    fn = getattr(femm, label, None)
    return tracker.call(label, fn, *args)


TYPES = [
    dict(
        key="magnetics",
        doc_type=0,
        ip="mi",
        op="mo",
        material="Air",
        probdef=(0, "millimeters", "planar", 1e-8, 1, 30),
        circuit_args=("test_circuit", 0, 1),
        boundprop_args=("test_bound", 0, 0, 0, 0, 0, 0, 0, 0, 0),
        pointprop_args=("test_point", 0, 0),
        setblockprop_args=lambda mat, circuit: (mat, 1, 0, circuit, 0, 0, 1),
        setsegmentprop_args=lambda bound: (bound, 0, 1, 0, 0),
        setnodeprop_args=lambda pt: (pt, 0),
        conductor_query="getcircuitproperties",
        conductor_name="test_circuit",
        field_getters=[("getb", (10, 10))],
    ),
    dict(
        key="electrostatics",
        doc_type=1,
        ip="ei",
        op="eo",
        material="Air",
        probdef=("millimeters", "planar", 1e-8, 1, 30),
        conductorprop_args=("test_conductor", 0, 0, 1),
        boundprop_args=("test_bound", 0, 0, 0, 0, 0),
        pointprop_args=("test_point", 0, 0),
        setblockprop_args=lambda mat: (mat, 1, 0, 0),
        setsegmentprop_args=lambda bound: (bound, 0, 1, 0, 0, ""),
        setnodeprop_args=lambda pt: (pt, 0, ""),
        conductor_query="getconductorproperties",
        conductor_name="test_conductor",
        field_getters=[("getd", (10, 10)), ("gete", (10, 10))],
    ),
    dict(
        key="heatflow",
        doc_type=2,
        ip="hi",
        op="ho",
        material="Air",
        probdef=("millimeters", "planar", 1e-8, 1, 30),
        conductorprop_args=("test_conductor", 0, 0, 1),
        boundprop_args=("test_bound", 0, 0, 0, 0, 0, 0),
        pointprop_args=("test_point", 0, 0),
        setblockprop_args=lambda mat: (mat, 1, 0, 0),
        setsegmentprop_args=lambda bound: (bound, 0, 1, 0, 0, ""),
        setnodeprop_args=lambda pt: (pt, 0, ""),
        conductor_query="getconductorproperties",
        conductor_name="test_conductor",
        field_getters=[("getf", (10, 10))],
    ),
    dict(
        key="currentflow",
        doc_type=3,
        ip="ci",
        op="co",
        material="Copper",
        probdef=("millimeters", "planar", 1e-8, 1, 30),
        conductorprop_args=("test_conductor", 0, 0, 1),
        boundprop_args=("test_bound", 0, 0, 0, 0, 0),
        pointprop_args=("test_point", 0, 0),
        setblockprop_args=lambda mat: (mat, 1, 0, 0),
        setsegmentprop_args=lambda bound: (bound, 0, 1, 0, 0, ""),
        setnodeprop_args=lambda pt: (pt, 0, ""),
        conductor_query="getconductorproperties",
        conductor_name="test_conductor",
        field_getters=[("getj", (10, 10))],
    ),
]


def run_standalone_commands(tracker):
    """Commands that don't need a document open."""
    tracker.call("AWG", getattr(femm, "AWG", None), 20)
    tracker.call("IEC", getattr(femm, "IEC", None), 2.5)

    for name in ("showconsole", "hideconsole", "showpointprops", "hidepointprops",
                 "main_maximize", "main_restore", "main_minimize", "main_restore"):
        fn = getattr(femm, name, None)
        tracker.call(name, fn)

    for name in BLOCKING_COMMANDS:
        tracker.skip(name, "opens a blocking modal dialog; cannot run unattended")


def run_problem_type(tracker, cfg):
    ip, op = cfg["ip"], cfg["op"]
    model_path = os.path.join(OUTPUT_DIR, f"lua_regression_{cfg['key']}.fem")
    dxf_path = os.path.join(OUTPUT_DIR, f"lua_regression_{cfg['key']}.dxf")
    bmp_path = os.path.join(OUTPUT_DIR, f"lua_regression_{cfg['key']}.bmp")

    femm.newdocument(cfg["doc_type"])
    c(tracker, ip, "probdef", *cfg["probdef"])

    # materials / properties -----------------------------------------
    c(tracker, ip, "getmaterial", cfg["material"])
    if ip == "mi":
        c(tracker, ip, "addcircprop", *cfg["circuit_args"])
    else:
        c(tracker, ip, "addconductorprop", *cfg["conductorprop_args"])
    c(tracker, ip, "addboundprop", *cfg["boundprop_args"])
    c(tracker, ip, "addpointprop", *cfg["pointprop_args"])

    # geometry ----------------------------------------------------------
    c(tracker, ip, "drawrectangle", 0, 0, 20, 20)
    c(tracker, ip, "addblocklabel", 10, 10)
    c(tracker, ip, "addnode", 30, 0)
    c(tracker, ip, "addnode", 30, 10)
    c(tracker, ip, "addarc", 30, 0, 30, 10, 90, 1)
    c(tracker, ip, "addsegment", 40, 0, 40, 10)  # isolated, for delete tests

    # node/segment/block properties -------------------------------------
    c(tracker, ip, "selectnode", 0, 0)
    c(tracker, ip, "setnodeprop", *cfg["setnodeprop_args"]("test_point"))
    c(tracker, ip, "clearselected")

    c(tracker, ip, "selectsegment", 10, 0)
    c(tracker, ip, "setsegmentprop", *cfg["setsegmentprop_args"]("test_bound"))
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 10, 20)
    c(tracker, ip, "setsegmentprop", *cfg["setsegmentprop_args"]("test_bound"))
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 0, 10)
    c(tracker, ip, "setsegmentprop", *cfg["setsegmentprop_args"]("test_bound"))
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 20, 10)
    c(tracker, ip, "setsegmentprop", *cfg["setsegmentprop_args"]("test_bound"))
    c(tracker, ip, "clearselected")

    c(tracker, ip, "selectlabel", 10, 10)
    if ip == "mi":
        c(tracker, ip, "setblockprop", *cfg["setblockprop_args"](cfg["material"], "test_circuit"))
    else:
        c(tracker, ip, "setblockprop", *cfg["setblockprop_args"](cfg["material"]))
    c(tracker, ip, "clearselected")

    c(tracker, ip, "selectarcsegment", 30, 5)
    if ip == "mi":
        c(tracker, ip, "setarcsegmentprop", 2.5, "<None>", 0, 0)
    else:
        c(tracker, ip, "setarcsegmentprop", 2.5, "<None>", 0, 0, "")
    c(tracker, ip, "clearselected")

    # selection variants --------------------------------------------------
    c(tracker, ip, "selectrectangle", 29, -1, 41, 11)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectcircle", 35, 5, 20)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectgroup", 0)
    c(tracker, ip, "clearselected")

    # radius (attempt on a rectangle corner) -------------------------------
    c(tracker, ip, "createradius", 0, 0, 1)

    # edit ops on the throwaway isolated segment ---------------------------
    c(tracker, ip, "selectsegment", 40, 5)
    c(tracker, ip, "copytranslate2", 20, 0, 1, 1)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 40, 5)
    c(tracker, ip, "copyrotate2", 0, 0, 15, 1, 1)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 40, 5)
    c(tracker, ip, "movetranslate2", 0, 30, 1)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 40, 35)
    c(tracker, ip, "moverotate2", 40, 35, 10, 1)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 60, 5)
    c(tracker, ip, "mirror2", 55, -5, 55, 15, 1)
    c(tracker, ip, "clearselected")
    c(tracker, ip, "selectsegment", 40, 5)
    c(tracker, ip, "scale2", 40, 5, 1.1, 1)
    c(tracker, ip, "clearselected")

    c(tracker, ip, "selectrectangle", 25, -5, 75, 45)
    c(tracker, ip, "deleteselectedsegments")
    c(tracker, ip, "deleteselectedarcsegments")
    c(tracker, ip, "deleteselectednodes")
    c(tracker, ip, "clearselected")

    # outer-space helpers (not present for current flow) --------------------
    if hasattr(femm, f"{ip}_defineouterspace"):
        c(tracker, ip, "defineouterspace", 0, 0, 0)
        c(tracker, ip, "attachouterspace")
        c(tracker, ip, "detachouterspace")
    else:
        for name in ("defineouterspace", "attachouterspace", "detachouterspace"):
            tracker.skip(f"{ip}_{name}", "not exposed for this problem type")
    c(tracker, ip, "attachdefault")
    c(tracker, ip, "detachdefault")

    # view / grid ---------------------------------------------------------
    c(tracker, ip, "zoomnatural")
    c(tracker, ip, "zoomin")
    c(tracker, ip, "zoomout")
    c(tracker, ip, "zoom", -5, -5, 45, 45)
    c(tracker, ip, "showgrid")
    c(tracker, ip, "setgrid", 1, "cart")
    c(tracker, ip, "snapgridon" if hasattr(femm, f"{ip}_snapgridon") else "gridsnap")
    c(tracker, ip, "hidegrid")
    c(tracker, ip, "shownames")
    c(tracker, ip, "hidenames")
    c(tracker, ip, "setcomment", "regression test model")
    tracker.skip(f"{ip}_setfocus", "requires an already-open document's exact window title; not attempted")
    c(tracker, ip, "setgroup", 0)
    c(tracker, ip, "refreshview")

    # save (must happen before createmesh/analyze -- both require the
    # document to already be saved to disk) ---------------------------------
    c(tracker, ip, "saveas", model_path)
    c(tracker, ip, "savebitmap", bmp_path)
    c(tracker, ip, "savedxf", dxf_path)

    # mesh ------------------------------------------------------------------
    c(tracker, ip, "createmesh")
    c(tracker, ip, "showmesh")
    c(tracker, ip, "smartmesh", 1)
    c(tracker, ip, "purgemesh")

    # analyze + solution ------------------------------------------------------
    # loadsolution shows a *blocking* MessageBox (not a catchable Lua error)
    # if the solver didn't produce a solution file, which would hang this
    # script forever. So: check the solution file landed on disk first, and
    # only call loadsolution (and everything downstream that depends on a
    # loaded solution) if it did.
    c(tracker, ip, "analyze", 1)
    base, _ = os.path.splitext(model_path)
    solution_path = None
    for ext in (".ans", ".res"):
        if os.path.exists(base + ext):
            solution_path = base + ext
            break

    if solution_path is None:
        tracker.records.append((
            f"{ip}_loadsolution", "FAIL",
            f"no .ans/.res solution file found next to {model_path} after analyze "
            "-- solve did not succeed; skipping all post-processing for this type",
        ))
        for name in (
            "numnodes", "numelements", "getnode", "getelement", "getpointvalues",
            "getprobleminfo", "selectblock", "blockintegral", "clearblock",
            "groupselectblock", "addcontour", "lineintegral", "bendcontour",
            "clearcontour", "selectpoint", cfg["conductor_query"],
            "showdensityplot", "hidedensityplot", "showcontourplot",
            "hidecontourplot", "showvectorplot", "showmesh", "hidemesh",
            "showgrid", "hidegrid", "shownames", "hidenames", "showpoints",
            "hidepoints", "smoothon", "smoothoff", "smooth", "zoomnatural",
            "zoomin", "zoomout", "zoom", "savebitmap", "refreshview",
            "reload", "close",
        ):
            tracker.skip(f"{op}_{name}", "no solution loaded (see loadsolution failure above)")
        for getter, _pt in cfg["field_getters"]:
            tracker.skip(f"{op}_{getter}", "no solution loaded (see loadsolution failure above)")
    else:
        c(tracker, ip, "loadsolution")

        # post-processing ------------------------------------------------------
        c(tracker, op, "numnodes")
        c(tracker, op, "numelements")
        c(tracker, op, "getnode", 0)
        c(tracker, op, "getelement", 0)
        c(tracker, op, "getpointvalues", 10, 10)
        c(tracker, op, "getprobleminfo")
        for getter, pt in cfg["field_getters"]:
            c(tracker, op, getter, *pt)

        c(tracker, op, "selectblock", 10, 10)
        c(tracker, op, "blockintegral", 0)
        c(tracker, op, "clearblock")
        c(tracker, op, "groupselectblock", 0)
        c(tracker, op, "clearblock")

        c(tracker, op, "addcontour", 0, 0)
        c(tracker, op, "addcontour", 20, 20)
        c(tracker, op, "lineintegral", 0)
        c(tracker, op, "bendcontour", 90, 1)
        c(tracker, op, "clearcontour")

        c(tracker, op, "selectpoint", 10, 10)
        c(tracker, op, "clearblock")

        c(tracker, op, cfg["conductor_query"], cfg["conductor_name"])

        c(tracker, op, "showdensityplot", 1, 0, 0, 1, "bmag" if op == "mo" else "ez")
        c(tracker, op, "hidedensityplot")
        c(tracker, op, "showcontourplot", 1, 0, 1, "real")
        c(tracker, op, "hidecontourplot")
        c(tracker, op, "showvectorplot", 1, 1)
        c(tracker, op, "showmesh")
        c(tracker, op, "hidemesh")
        c(tracker, op, "showgrid")
        c(tracker, op, "hidegrid")
        c(tracker, op, "shownames")
        c(tracker, op, "hidenames")
        c(tracker, op, "showpoints")
        c(tracker, op, "hidepoints")
        c(tracker, op, "smoothon")
        c(tracker, op, "smoothoff")
        c(tracker, op, "smooth", "on")
        c(tracker, op, "zoomnatural")
        c(tracker, op, "zoomin")
        c(tracker, op, "zoomout")
        c(tracker, op, "zoom", -5, -5, 45, 45)
        c(tracker, op, "savebitmap", bmp_path)
        c(tracker, op, "refreshview")
        c(tracker, op, "reload")
        c(tracker, op, "close")

    # round-trip: reopen the saved model in a fresh input document -----------
    reopened = tracker.call(f"opendocument({cfg['key']})", femm.opendocument, model_path)
    if reopened is not None or os.path.exists(model_path):
        c(tracker, ip, "close")


def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    tracker = Tracker()
    femm.openfemm()
    try:
        run_standalone_commands(tracker)
        for cfg in TYPES:
            run_problem_type(tracker, cfg)
    finally:
        femm.closefemm()

    n_pass, n_fail, n_skip = tracker.summary()
    total = len(tracker.records)

    exercised_names = {label.split("(")[0] for label, _, _ in tracker.records}
    all_pyfemm_names = {
        name for name in dir(femm)
        if not name.startswith("_") and callable(getattr(femm, name))
    }
    not_exercised = sorted(
        all_pyfemm_names - exercised_names - NOT_LUA_COMMANDS - {"openfemm", "closefemm"}
    )

    lines = []
    lines.append("FEMM Lua command regression sweep")
    lines.append("==================================")
    lines.append(f"Total calls attempted: {total}")
    lines.append(f"  PASS: {n_pass}")
    lines.append(f"  FAIL: {n_fail}")
    lines.append(f"  SKIP: {n_skip}")
    lines.append("")
    lines.append("--- FAIL details ---")
    for label, status, detail in tracker.records:
        if status == "FAIL":
            lines.append(f"  {label}: {detail}")
    if n_fail == 0:
        lines.append("  (none)")
    lines.append("")
    lines.append("--- SKIP details ---")
    for label, status, detail in tracker.records:
        if status == "SKIP":
            lines.append(f"  {label}: {detail}")
    lines.append("")
    lines.append(f"--- pyfemm functions never exercised by this sweep ({len(not_exercised)}) ---")
    for name in not_exercised:
        lines.append(f"  {name}")
    lines.append("")
    lines.append("--- Full call log ---")
    for label, status, detail in tracker.records:
        suffix = f"  ({detail})" if detail else ""
        lines.append(f"  [{status}] {label}{suffix}")

    report = "\n".join(lines)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    with open(RESULTS_PATH, "w") as f:
        f.write(report + "\n")

    print(f"Total: {total}  PASS: {n_pass}  FAIL: {n_fail}  SKIP: {n_skip}")
    print(f"Full report written to {RESULTS_PATH}")

    if n_fail > 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
