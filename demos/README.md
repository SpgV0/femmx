# FEMMX demo models

A small library of worked models, one set per problem type, intended to be opened,
solved and read rather than edited in place. Every one of them is a case with a known
closed-form answer, so the demo is not just "a model that opens" -- it is a model whose
result you can check against physics.

These are the sources the planned **File > Demo Models** browser lists (issue #89).
Treat this directory as read-only: a demo should be opened as a working copy, and
solving must not write mesh or solution files back here. The demos are committed, so
`git status` will notice if something does.

Each model also carries its own description in its `[Comment]` field, which the
Problem Information dialog shows.

## Magnetics (`.fem`)

| Model | What it shows | Analytic reference |
|---|---|---|
| `straight_wire.fem` | A single 10 A conductor in open-boundary air. The simplest model here. | `B = mu0*I/(2*pi*r)` |
| `coax_cable.fem` | Coax with equal and opposite currents; external inductance from stored energy. | `L' = (mu0/2pi)*ln(b/a)` |
| `circular_loop_axi.fem` | Single-turn loop, axisymmetric -- the standard introduction to axisymmetric modelling. | `B = mu0*I*a^2/(2*(a^2+z^2)^(3/2))` |
| `solenoid_axi.fem` | A solenoid short enough that end effects pull the centre field well below `mu0*n*I`. | finite-solenoid on-axis form |
| `two_wires_force.fem` | Force between parallel conductors; Lorentz and weighted stress tensor on the same block. | `F/L = mu0*I1*I2/(2*pi*d)` |
| `saturating_core.fem` | Nonlinear B-H core driven into saturation -- relative permeability falls from ~13000 to ~100. | no closed form; check against the B-H curve |
| `skin_effect_ac.fem` | Time-harmonic at 100 kHz: skin depth ~0.21 mm in a 5 mm conductor. | `delta = sqrt(2/(omega*mu*sigma))` |

## Electrostatics (`.fee`)

| Model | What it shows | Analytic reference |
|---|---|---|
| `parallel_plate.fee` | Periodic side boundaries remove fringing, leaving a uniform field. | `C = eps*A/d` |
| `coax_capacitor_axi.fee` | Coaxial conductors at fixed potentials; logarithmic potential. | `C' = 2*pi*eps/ln(b/a)` |

## Heat flow (`.feh`)

| Model | What it shows | Analytic reference |
|---|---|---|
| `slab_1d.feh` | 400 K / 300 K faces: linear profile, uniform flux. | `q = k*dT/L` |
| `cylindrical_shell_axi.feh` | Radial conduction, logarithmic rather than linear. | `T(r)` logarithmic |
| `convection_wall.feh` | A convection boundary, so the outer face temperature is solved rather than prescribed. | `q'' = h*(T_s - T_inf)` |

## Current flow (`.fec`)

| Model | What it shows | Analytic reference |
|---|---|---|
| `conducting_bar.fec` | Uniform current density between two fixed potentials. | `R = rho*L/A` |

## Where these came from

Each model was taken from the analytic regression suite -- `test/analytic_fields_test.py`
and `test/magnetics_analytic_test.py` build them and assert the results against the
formulas above, typically to well under 1%. `demos.json` records the source path for
each in its `derivedFrom` field.

That shared origin is deliberate: the demo library and the analytic tests stay
meaningful together, and a demo that stopped matching its formula would fail the test
suite first. Note the test suite regenerates its own copies under `test/results/`
(gitignored) on every run -- these committed copies are independent of that.

`demos.json` is the machine-readable manifest: title, problem type, description,
analytic reference and source path per demo.
