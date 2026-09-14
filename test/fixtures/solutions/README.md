# Solved-solution fixtures

Two small solved problems, one per physics that had no committed
solution anywhere in the tree:

| file | physics | what it is |
|---|---|---|
| `parallel_plate.fee` / `.res` | electrostatics | a 20 x 10 mm dielectric block held at 10 V across its two vertical edges |
| `current_bar.fec` / `.anc` | current flow | the same geometry in a 1 MS/m conductor, 1 V across it |

The `.fee`/`.fec` inputs are committed alongside the solutions so the
fixtures can be regenerated and so it is obvious what they are; the
tests read only the solutions.

## Why these exist

`femmqt/tests/tst_plot_state.cpp` checks that every problem type
renders the plot mode it was asked for — a density plot fills its area
with colour bands, a contour plot draws thin lines across it. Magnetics
and heat flow had committed solutions to point at
(`manual_qt/images/example.ans` and `.anh`); electrostatics and current
flow did not.

It originally pointed at `test/results/analytic_fields/coax.res` and
`bar.anc`. Those are **gitignored** — the pytest suite regenerates them
on every run — so they exist on a developer machine that has run the
tests and are absent from a fresh checkout. The C++ tests run *before*
pytest in CI, so the rows failed there and passed everywhere the author
looked. That is the whole reason a fixture belongs in git rather than
being picked up from whatever happens to be lying around.

## Why they are larger than the other fixtures

`render_fixture.ans` next door is 28 KB; these are around 330 KB each.
That is not for want of trying: `belasolv` and `csolv` refine the mesh
adaptively, so the node count lands near 3,500 whatever mesh size the
model asks for — coarsening the block and the boundary segments by 3x
changed it by 15 nodes. They are plain text and compress well.
