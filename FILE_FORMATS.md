# FEMMX file formats

FEMMX uses four file types. `.fem`/`.ans` are the original, plain-text
formats shared by every FEMMX component (both GUIs, the solvers, and
every language binding). `.femx`/`.ansx` are newer, pure-binary **caches**
of that same data, introduced to make loading fast for large models —
never a second source of truth, and never shell-registered or meant to
be edited by hand.

Both GUIs (the classic MFC app, `femm/`, and the Qt app, `femmqt/`) read
and write all four formats independently — there is no shared parsing
code between them, matching this project's existing convention that each
consumer (GUI or solver) owns its own reader/writer for these formats.

## Relationship between the four formats

```
   .fem  (geometry + properties, text)
     |
     |  triangle.exe (mesh) + fkn.exe (solve)
     v
   .ans  (.fem's own header, plus the solved mesh, text)

   .femx = binary cache of .fem, regenerated whenever stale
   .ansx = binary cache of .ans's MESH data only, regenerated whenever stale
```

- A `.femx`/`.ansx` file is always named after its source
  (`foo.fem` → `foo.femx`, `foo.ans` → `foo.ansx`) and lives next to it.
- **Freshness check**: each cache's header records its source file's
  size (bytes) and mtime (seconds since epoch). A GUI opening `foo.fem`
  compares `foo.femx`'s recorded size/mtime against `foo.fem`'s current
  size/mtime on disk; any mismatch (or a missing/corrupt/wrong-version
  cache) means "stale" — the GUI falls back to the normal text parser
  and, on success, rewrites the cache for next time. This is always a
  **best-effort** step: a write failure (e.g. a read-only directory)
  never fails the operation the user actually asked for, it just means
  no speedup next time.
- Every numeric field in `.femx`/`.ansx` is a byte-for-byte copy of what
  the text parser would have produced from `.fem`/`.ans` — opening a
  file via its cache and via its text source must always produce
  identical in-memory state.

---

## `.fem` — geometry + properties (plain text)

Written by `CFemmeDoc::OnSaveDocument` (`femm/FemmeDoc.cpp`) /
`FemmFileIO::writeFem` (`femmqt/FemmFileIO.cpp`); read by
`OnOpenDocument` / `FemmFileIO::readFem`. Tag-value lines
(`[Tag]  =  value` or, inside a property block, `<Tag> = value`),
whitespace around `=` is not significant. Numbers are written with
`%.17g` (exact `double` round-trip). Every table below is 0-indexed in
memory but 1-indexed on disk (`0` = "none" for an optional reference; an
in-file index of `N` means the `N`th entry, 1-based, of the referenced
list).

### Header (top-level `[Tag] = value` lines, in this order)

| Tag | Field | Notes |
|---|---|---|
| `[Format]` | file format version | `4.0` currently |
| `[Frequency]` | Hz | 0 = magnetostatic |
| `[Precision]` | solver precision | e.g. `1e-8` |
| `[MinAngle]` | degrees | triangle.exe mesh quality constraint |
| `[DoSmartMesh]` | 0/1 | auto-refine mesh near small features |
| `[Depth]` | into-the-page depth | planar problems only |
| `[LengthUnits]` | `inches`\|`millimeters`\|`centimeters`\|`meters`\|`mils`\|`microns` | |
| `[ProblemType]` | `planar`\|`axisymmetric` | |
| `[Coordinates]` | `cartesian`\|`polar` | |
| `[ACSolver]` | 0=successive approx, 1=Newton | |
| `[GPUAccel]` | 0/1 | ask fkn.exe to try its CUDA solve; no effect unless fkn.exe was built with CUDA support |
| `[PrevType]` / `[PrevSoln]` | previous-solution info, for incremental permeability problems | |
| `[Comment]` | quoted string | free-text problem note |
| `[extZo]` / `[extRo]` / `[extRi]` | axisymmetric exterior-region parameters | only written if `extRo`/`extRi` are both non-zero |

### Property sections

Each is `[XProps]  = <count>` followed by `<count>` `<BeginX>...<EndX>`
blocks.

**`[PointProps]`** (`<BeginPoint>`): `<PointName>`, `<I_re>`/`<I_im>`
(applied point current, A), `<A_re>`/`<A_im>` (prescribed nodal value).

**`[BdryProps]`** (`<BeginBdry>`): `<BdryName>`, `<BdryType>` (0=fixed
A, 1=small-skin-depth eddy current, 2=mixed, 3=SDI, 4/5=periodic/
antiperiodic, 6/7=periodic/antiperiodic air-gap), `<A_0>`/`<A_1>`/
`<A_2>`/`<Phi>` (BdryType=0), `<c0>`/`<c0i>`/`<c1>`/`<c1i>` (mixed BC
coefficients), `<Mu_ssd>`/`<Sigma_ssd>` (eddy BC), `<innerangle>`/
`<outerangle>` (air-gap element rotation).

**`[BlockProps]`** (`<BeginBlock>`): `<BlockName>`, `<Mu_x>`/`<Mu_y>`
(relative permeability), `<H_c>` (magnetization, A/m), `<H_cAngle>`
(magnetization direction, degrees), `<J_re>`/`<J_im>` (applied current
density, MA/m²), `<Sigma>` (conductivity, MS/m), `<d_lam>` (lamination
thickness, mm), `<Phi_h>`/`<Phi_hx>`/`<Phi_hy>` (hysteresis angles,
degrees), `<LamType>` (0=none/in-plane, 1=x-laminated, 2=y-laminated,
≥3=stranded/litz), `<LamFill>` (lamination fill factor), `<NStrands>`,
`<WireD>` (strand diameter, mm), `<BHPoints>` followed by that many
`B<tab>H` lines (a linear material has `BHPoints = 0` and uses `Mu_x`/
`Mu_y` directly instead).

**`[CircuitProps]`** (`<BeginCircuit>`): `<CircuitName>`,
`<TotalAmps_re>`/`<TotalAmps_im>`, `<CircuitType>` (0=parallel,
1=series).

### Geometry sections (in this order)

- **`[NumPoints] = N`**, then `N` lines: `x  y  pointPropIndex  inGroup`
  (`pointPropIndex`: 0=none, else 1-based into `[PointProps]`).
- **`[NumSegments] = N`**, then `N` lines: `n0  n1  maxSideLength
  boundaryMarker  hidden  inGroup` (`maxSideLength`: `-1` = no mesh
  constraint; `boundaryMarker`: 0=none, else 1-based into `[BdryProps]`).
- **`[NumArcSegments] = N`**, then `N` lines: `n0  n1  arcLength
  maxSideLength  boundaryMarker  hidden  inGroup  mySideLength`
  (`arcLength`/`mySideLength` in degrees).
- **`[NumHoles] = N`**, then `N` lines: `x  y  inGroup` — block labels
  with no assigned material ("`<No Mesh>`" in the classic GUI's own
  in-memory model).
- **`[NumBlockLabels] = N`**, then `N` lines: `x  y  blockTypeIndex
  meshSizeDiameter  circuitIndex  magDir  inGroup  turns
  isExternal+isDefault  ["magDirFctn"]` (`blockTypeIndex`: 1-based into
  `[BlockProps]`; `meshSizeDiameter`: `-1` = no constraint, else
  `sqrt(4*maxArea/pi)` — the *area* constraint, converted to a
  *diameter* only for this on-disk field; `circuitIndex`: 0=none, else
  1-based into `[CircuitProps]`; trailing quoted string is a custom Lua
  `MagDirFctn` expression, present only if non-empty).

---

## `.ans` — solved mesh (plain text)

Written by `fkn.exe` (`fkn/prob1big.cpp`); read by `CFemmviewDoc::
OnOpenDocument` (`femm/FemmviewDoc.cpp`) / `AnsFileIO::readAns`
(`femmqt/AnsFileIO.cpp`). The header and property sections are **byte-
for-byte the same tag format as `.fem`** (both readers tolerate the
other file type's trailing sections they don't recognize) — solving
never changes the original geometry/properties, so `fkn.exe` just
carries them through unchanged ahead of its own solved-mesh section.

After the properties sections and geometry sections (same as `.fem`,
above) comes an untagged `[Solution]` marker, then:

1. **Mesh nodes**: `<count>`, then that many lines:
   `x  y  A_re  [A_im]` (`A_im` only present if `Frequency != 0`) —
   the complex nodal vector potential (planar) or `r·A` flux function
   (axisymmetric), in Wb/m or Wb respectively.
2. **Mesh elements**: `<count>`, then that many lines:
   `p0  p1  p2  lbl` (0-based node indices; `lbl` = 0-based index into
   the block-label list from the header section above).
3. **Per-block-label circuit correction**: `<count>` (always equal to
   the block-label count — every label gets a row, even ones with no
   circuit assigned), then that many lines: `Case  value_re  [value_im]`
   — `fkn.exe`'s own solved correction for circuits with a prescribed
   voltage (`Case=0`, `value` = the solved voltage-drop `dVolts`) or
   prescribed current (`Case=1`, `value` = the solved current-density
   correction `J`); a label with no circuit gets a harmless placeholder
   row (`Case=1`, `value=0`). This section is easy to miss reading the
   format casually — it's untagged and comes immediately after the
   element list with no header of its own.
4. **PBC (periodic boundary) data** — not actively used by either
   post-processor GUI today, but must still be read (to reach the data
   after it): `<count>`, then that many lines (skipped).
5. **Air gap elements**: `<count>`, then for each: a name line, a
   parameter line (`BdryFormat  InnerAngle  OuterAngle  ri  ro
   totalArcLength  agc_re  agc_im  totalArcElements  InnerShift
   OuterShift`), and (if `totalArcElements > 0`) further per-element
   sample data. Used for rotating-machine "moving band" simulations —
   an advanced, comparatively rare feature; not yet supported by the Qt
   GUI.

**Derived data, recomputed after loading, not stored in the file**:
each element's centroid, `rsqr` (max corner distance from centroid,
used for point-location queries), magnetization direction (evaluates
any custom `MagDirFctn` Lua expression), and `B1`/`B2` (flux density,
from nodal `A` via shape-function gradients) — this recomputation is
`.ans`'s single most expensive load step for a large mesh, and is
exactly what `.ansx` exists to cache (below).

---

## `.femx` — binary cache of `.fem`

Defined in `femmqt/FemxFileIO.h`/`.cpp`; ported to the classic GUI in
`femm/FemxFileIO.h`/`.cpp` (byte-for-byte the same format, independent
implementation — Qt types on one side, MFC/plain C++ on the other,
matching this project's established pattern of independent format
readers per consumer). Current version: **2**.

All multi-byte fields are little-endian (native x86/x64), and every
struct is `#pragma pack(push, 1)` — no padding.

```
FemxHeader {
  char     magic[8];            "FEMMFEMX"
  uint32_t version;              2
  uint32_t headerSize;           sizeof(FemxHeader) -- lets a future
                                  version add trailing fields a reader
                                  built against an older version can
                                  still skip past
  uint32_t problemType;          0=planar, 1=axisymmetric
  uint32_t lengthUnits;          matches .fem's [LengthUnits] encoding
  uint8_t  coordsPolar;
  uint8_t  smartMesh;
  uint8_t  reserved[6];
  double   frequency, precision, minAngle, depth;
  double   extZo, extRo, extRi;
  int32_t  acSolver, gpuAccel, prevType;
  char     prevSoln[260];        fixed-size, NUL-padded
  char     comment[512];         fixed-size, NUL-padded
  uint64_t sourceFemSize;        source .fem's size, for the freshness check
  uint64_t sourceFemMtimeSecs;   source .fem's mtime, for the freshness check
  uint64_t pointPropCount, boundaryPropCount, materialPropCount, circuitPropCount;
  uint64_t nodeCount, segmentCount, arcCount, blockLabelCount;
}                                 -- 952 bytes
```

Followed by flat arrays, in this order, each `count` from the header
above: point-prop records, boundary-prop records, material-prop records,
circuit-prop records, node records, segment records, arc records,
block-label records. Every property/geometry field maps directly to its
`.fem` text-format counterpart (see the `.fem` section above); a few
notable on-disk differences from the in-memory model:

- **BH curve data** (`FemxMaterialPropRecord`) is a fixed
  `double[256][2]` array (`bhPointCount` says how many are actually
  used) rather than a dynamic list — 256 comfortably exceeds every
  material in `bin/matlib.dat` (the largest ships 149 points); classic
  FEMM's own in-memory model has no fixed cap, so this is a cache-format
  limit only, not a modeled one. **Changing this cap changes the
  record's byte size and requires a version bump** — a real bug this
  session (cap silently truncating past-64-point curves) was caused by
  extending the in-memory field without remembering this.
- **Point/boundary/circuit references** (a node's point-prop, a
  segment/arc's boundary marker, a block label's material/circuit) are
  stored as the same 0/1-based index convention as the `.fem` text
  format, not as names — even though the classic GUI's own in-memory
  model stores these as name strings (`CString`) internally; the reader/
  writer resolves name↔index against the property arrays exactly like
  `.fem`'s own text reader/writer already does.
- **Block label area vs. diameter**: `.femx`'s `maxArea` field stores
  the *area* directly (unlike `.fem` text, which stores a derived
  diameter) — matching the in-memory field it mirrors, since there's no
  text-format round-trip step to justify the conversion here.

---

## `.ansx` — binary cache of `.ans`'s mesh

Defined in `femmqt/AnsxFileIO.h`/`.cpp`; ported to the classic GUI in
`femm/AnsxFileIO.h`/`.cpp`. Current version: **3**.

**Deliberately not a serialization of the whole `.ans` file** — only the
mesh-node and mesh-element arrays (the two sections whose size scales
with mesh size and dominates load time for a large solved model).
Geometry/properties, the per-block-label circuit correction section, and
the air gap element section are always still read from the source
`.ans` text regardless of whether the mesh cache is used — all three are
bounded by geometry complexity (not mesh density), so cheap even for a
multi-million-element mesh, and re-reading them keeps this format simple
and its scope obviously "mesh only."

```
AnsxHeader {
  char     magic[8];            "FEMMANSX"
  uint32_t version;              3
  uint32_t headerSize;           sizeof(AnsxHeader)
  uint32_t coordSystem;          0=planar, 1=axisymmetric
  uint32_t lengthUnits;
  double   frequency;
  double   bMagMin, bMagMax;     precomputed |B| range, for the density-plot legend
  uint64_t sourceSize;           source .ans's size, for the freshness check
  uint64_t sourceMtimeSecs;      source .ans's mtime, for the freshness check
  uint64_t nodeCount;
  uint64_t elementCount;
}                                 -- 80 bytes

AnsxNodeRecord {
  double x, y, Are, Aim;
}                                 -- 32 bytes

AnsxElementRecord {
  int64_t  p0, p1, p2, lbl;      node/label indices (int64 purely to
                                  keep every field 8-byte aligned, not
                                  because indices need the range)
  double   B1re, B1im, B2re, B2im;
  double   ctrX, ctrY;
  double   muX, muY;             resolved material properties (see below)
  double   sigma;
  double   jSrcRe, jSrcIm;
  double   jRe, jIm;             total current density: source + eddy +
                                  solved circuit correction, MA/m^2
}                                 -- 136 bytes
```

Followed by `nodeCount` node records then `elementCount` element
records.

- **B1/B2/centroid are precomputed**, not recomputed from nodal `A` on
  every load — this is the actual point of the format (see `.ans`'s own
  "derived data" note above). `rsqr` and magnetization direction are
  *not* cached (cheap, in-memory-only recomputation, no text re-parse
  needed either way) — a reader still derives those itself after loading
  either the text or the binary path.
- **muX/muY/sigma/jSrcRe/jSrcIm/jRe/jIm are precomputed too**, resolved
  once from each element's block label → material lookup (plus, for
  jRe/jIm, `fkn.exe`'s own solved circuit-correction data) — this is
  what drives the `|H|` and `|Js+Je|` density plots without needing a
  live material-list lookup at render time. The classic GUI's own
  post-processor doesn't need these cached (it already has fast,
  direct, in-memory material-list access at render time) but still
  writes them correctly, purely so a `.ansx` written by the classic GUI
  behaves identically when later opened by the Qt GUI.
  `CFemmviewDoc::GetJA()`'s return value is scaled to A/m² internally
  (despite its own comment claiming MA/m²) — the writer divides by 1e6
  to match every other current-density field's actual MA/m² units.
