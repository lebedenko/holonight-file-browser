# Isolated raster previews by default

## Consumer contract

Preview requires installed `HolonightImages::Isolated` and
`HolonightThumbnails::Isolated`. Core retains its independent package boundary.
PNG, JPEG, BMP, TIFF, WebP and GIF first frames use the synchronous isolated client
for inspection, structured metadata and decoding. Encoded input is limited to
256 MiB; named or untagged color spaces are accepted. Other formats and color
profiles fail explicitly. There is no in-process raster fallback.

`ImageRuntimeOptions` selects one explicit runtime directory. PreviewService and
ThumbnailProvider capture it by value before dispatch. Synchronous entry points
accept the same value. Defaults are compiled from
`HOLONIGHT_FILE_BROWSER_IMAGE_RUNTIME`, initially the installation prefix's
`libexec/holonight-images/runtime`; DESTDIR is never part of that default.
Staged/relocated callers pass the installed Images package's prefix-relative
`HolonightImages_RUNTIME_DIRECTORY`. No mutable global or environment discovery
is introduced. Runtime-dependent tests use explicit CMake test inputs.

Caller-owned verified source descriptors remain open and owned by the consumer.
Workers, thread counts, generation suppression, source identity, memory caches,
resize debounce and the three-second preview deadline retain their existing
roles. Inspection metadata is reused for EXIF presentation instead of starting
an additional helper operation. Progressive pixels precede metadata publication.

Runtime status travels separately from image outcomes. Appended error kinds are
RuntimeUnavailable, RuntimeFailure and CleanupFailure; existing enum values are
unchanged. Runtime timeout maps to DecodeTimeout. Infrastructure messages are
translated and do not label the image damaged. Provider cleanup completes before
workers return; cleanup failure is retained even when cancellation is set.
Stale worker lifecycle failures are logged, while stale pixels remain invisible.

Both raster and SVG-derived disk thumbnails use isolated PNG lookup. SVG source
policy, inspection and rendering remain unchanged. Legacy Thumbnails consumers,
including Viewer, retain their original API and decoding path. Cache writes stay
in process. This milestone does not cover animation playback or linked SVG codecs.

## Acceptance and reproduction

Use an explicit development prefix containing host-native Images with isolation
enabled and a prepared runtime, then Thumbnails with
`HOLONIGHT_THUMBNAILS_ISOLATED=ON`. File Browser uses installed packages only.
Configure `HOLONIGHT_FILE_BROWSER_TEST_RUNTIME` and optionally
`HOLONIGHT_FILE_BROWSER_TEST_FIXTURES` (Images' generated fixture directory).
Run CTest with a working user systemd manager and bubblewrap namespaces.
`HOLONIGHT_THUMBNAILS_TEST_RUNTIME` enables the shared cache acceptance matrix
through the isolated API. Tests without a native runtime explicitly skip that
matrix; they still exercise unavailable-runtime errors and package contracts.

Native tests cover six formats, eight orientations, bounds/no upscaling,
alpha/16-bit precision, metadata formatting, unsupported profiles, the encoded
input limit, native filename identity, cache cancellation, and generation changes
at the cache commit boundary. The installed PreviewQuick consumer renders an
image through the actual async provider and checks final window-capture pixels.
Core-only installed consumers remain covered. Existing Images codec/protocol and
lifecycle fault evidence is reused; no persistent helpers or codecs were added.

Current sibling container acceptance must be requested explicitly with
`task check:integration -- --siblings ..`. Provider revision pins are deliberately
unchanged while the dependency changes are unpublished.

## Local evidence (2026-11-08)

Images `task check:integration` passes after the two scoped experimental formatting
fixes and narrowly required fuzz-harness lint corrections. Native cache and
installed Preview/PreviewQuick checks pass, including QML pixel delivery. The
current-sibling container builds all components and passes the Preview installed
consumers, but its two existing live-appearance tests fail with a stale body color.
Their icon/appearance behavior was not changed. Pinned static analysis also finds
pre-existing diagnostics in `src/icon_image_provider.cpp` and
`tests/consumer/appearance.cpp`; these are outside this migration.

A host-native Debug comparison against an archive of the pre-migration File Browser
HEAD used 20 samples per case, 1600×1200 solid PNG sources and a 256-pixel preview
bound. Each cold sample had a new source/cache identity; disk samples used a new
service after warming the disk entry; memory samples reused the service; rapid
samples issued 20 superseding target/clear pairs before the final target.
Times include completion and GUI event delivery, not just codec work.

| Case | Baseline median / p95 (ms) | Isolated median / p95 (ms) |
| --- | --- | --- |
| Cold | 8.78 / 10.76 | 212.87 / 230.33 |
| Disk cache | 2.20 / 3.20 | 166.82 / 203.49 |
| Memory cache | 2.16 / 2.40 | 92.56 / 111.11 |
| Rapid navigation | 9.96 / 10.25 | 191.77 / 212.92 |

All untraced samples completed under the existing three-second deadline. Memory
hits still inspect the source; isolation deliberately adds startup/verification
cost. These small synthetic samples are not a large-image performance guarantee.
The harness, archived baseline, raw distributions and traces are under ignored
`build/preview-benchmark` in the umbrella. Ptrace instrumentation can perturb the
three-second deadline and is excluded from the latency distributions.

No application deployment or publication was performed. Installed QML checks
exercise the shared Files/dialog rendering path; they are not a claim of an
interactive deployed Files or portal session.

An untraced repeat sampled **220 distinct helper scopes** for the complete benchmark
(including warmups), with **15,044,608 bytes** maximum observed cgroup memory peak.
The expected count is 2 helpers per cold/disk request and 1 per memory hit;
rapidly superseded jobs may cancel before launch. The observed total matches the
matrix including warmups. Scope memory excludes parent allocations and is sampled,
not a complete process RSS measurement. The focused exec trace shows the worker
launching systemd scopes with the verified helper/bubblewrap boundary; tracing
itself eventually triggered the existing timeout, so that run supplies launch
attribution only. Source review finds no direct raster inspect/decode/metadata or
legacy PNG-cache lookup calls in migrated production paths. SVG source rendering
and legacy consumers remain explicit exclusions.

Final focused results: native Images **8/8**, native Thumbnails **4/4** (legacy
and isolated installed packages), native File Browser **7/7**, and container
Core-only **2/2**. Pinned formatting and changed-source static analysis pass after
one test-expression parentheses correction. Thumbnails full container integration
passes. Umbrella dependency checks pass **14/14**; runtime staging-order and real
ownership/collision/conservative-uninstall checks pass **2/2**. These focused
results do not turn the unrelated File Browser full-gate failures into a pass.

## Application acceptance follow-up (2026-11-08)

Automated acceptance passes. The user reports that the executable manual native
checks passed; close-during-load could not be exercised because fixtures loaded too
quickly. That native interaction remains unverified; automated pending-work shutdown
coverage passes. Optional unavailable-runtime launches were not separately confirmed.
No publication, deployment, revision-pin update, Viewer migration, timeout tuning,
worker-count change or codec-limit change is included.

### Appearance gate root causes

The pinned container reproduced missing `image://icon/missing/folder` pixels before
color comparison. Plugin diagnostics showed no staged platform-theme search directory;
fixture diagnostics showed empty primary/fallback theme names. The provider prefix
was supplied through the `CMAKE_PREFIX_PATH` environment, while the consumer launcher
only built `QT_PLUGIN_PATH` from the CMake variable. It now includes both sources.
The platform plugin exists in the staged installation; neither candidate-chain
resolution nor semantic rendering required a production behavior change.

After plugin discovery was fixed, live updates exposed a fixture startup race:
the first atomic replacement could precede the platform reader's queued watcher
initialization. Draining queued startup work before replacing the fixture fixes it.
Image readiness and final accent pixels now use bounded checks instead of the fixed
150 ms delay. Accent, shading, text color, multiple sizes, live icon-theme updates,
application theme overrides and selection preservation remain asserted. Small helper
extractions and braces clear the existing appearance-consumer diagnostics; the icon
provider change is braces only. Both installed appearance consumers passed before
repeating the full gate.

`task check:integration -- --siblings ..` passes in the pinned image: main CTest
**12/12**, Core-only **2/2**, formatting, static analysis, QML checks and REUSE.
Evidence: `build/acceptance/integration-ok6fq_5x/acceptance.log` in this repository.

### Runtime composition and native harnesses

Files' DirectoryController and engine setup and the portal Backend → Request →
Controller path accept runtime options by value. Existing entry points delegate
to installation defaults. Only non-installed lab/test executables select an
explicit runtime. Files' existing native-preview lab now also supports
`--verify-isolated-preview` offscreen, loading the actual `Main` QML and its pane,
Quick Look and grid. Portal presentation tests use the actual chooser dock/drawer;
no portal grid thumbnails were added.

The portal test/probe wrapper starts a non-activating private D-Bus daemon and passes
its address to explicit backend/client connections. It never replaces
`DBUS_SESSION_BUS_ADDRESS`, so Images' systemd helpers retain the real user manager.
The native chooser wrapper forwards arguments with normal shell quoting and accepts
an explicit fixture folder and runtime.

The umbrella runner fails on missing runtime/executables/provider fixtures, user
manager or namespace support. Its synthetic fixtures have independently specified
red, blue and green pixels. Tests cover cold raster source decoding, disk and memory
reuse, SVG-derived cached PNG decoding, pane/Quick Look/grid pixels, chooser dock and
drawer pixels, resizing and selection preservation. Source/cache stage barriers test
cancellation, shutdown, late replies and unchanged cache bytes; timeout and unavailable
runtime errors are checked through Files and chooser presentation. These barriers
establish cancellation at consumer source/cache checkpoints, not inside a codec
instruction; unchanged Images codec/protocol/cleanup-fault evidence is reused.

### Commands and results

Providers were consumed as installed host-native packages, with the existing Images
and Thumbnails artifacts reused and File Browser's required components built/staged.
The development prefix was moved from `build/isolated-prefix` to
`build/isolated-prefix-relocated` in the umbrella; the old path is absent.
`HolonightImages_RUNTIME_DIRECTORY` exports the relocated prefix's
`libexec/holonight-images/runtime`. Installed Preview and PreviewQuick consumers were
configured against that relocated prefix and both passed with this explicit runtime.
The applications were rebuilt against the relocated installed packages.

From the umbrella root, the final application matrix is:

```sh
python3 scripts/check-isolated-previews.py \
  --prefix build/isolated-prefix-relocated \
  --runtime build/isolated-prefix-relocated/libexec/holonight-images/runtime \
  --files holonight-files/build/isolated/tests/files-native-preview-lab \
  --portal xdg-desktop-portal-holonight/build/isolated/tests/portal-tests \
  --fixtures holonight-images/build/isolated-release/tests/fixtures \
  --output build/isolated-acceptance-new
```

Use a fresh output directory. The runner renders offscreen and requires access to
the real user systemd manager and bubblewrap namespaces. It does not drive native
pointer/focus interaction. Successful evidence is under umbrella
`build/isolated-acceptance-04`, `build/isolated-acceptance-relocated` and
`build/isolated-acceptance-final`; earlier failed
attempts remain failed. The relocated matrix additionally asserts an isolated cache
read specifically for the SVG-derived PNG. Files' focused DirectoryController,
PreviewConsumers and QuickLookPresentationModel regressions pass **107/107** after
providing the staged runtime to the existing worker-thumbnail fixture. Portal focused
lifecycle/presentation regressions pass, and staged activation, Settings and demo CTest
pass **3/3**. The passive Files runner tests pass **17/17**, and its runner/observer CTest passes **2/2**. Umbrella packaging tests
pass **15**, with the opt-in real ownership test skipped in this invocation; its
previous passing evidence is reused because its tested inputs were not changed.

Changed-source static analysis uses a temporary compilation database without GCC's
`-mno-direct-extern-access`, which the installed clang-tidy cannot parse; production
compile flags are unchanged. Existing unrelated host-tool diagnostics are excluded
with changed-line filters. New Files acceptance source is checked in full. Pinned changed-source formatting, whitespace,
Python compilation and shell syntax checks also pass. Raw follow-up logs live in
umbrella `build/isolated-acceptance-evidence`.

### Manual native observations

Prepared commands, from the umbrella root:

```sh
bash build/isolated-preview-manual.sh files
bash build/isolated-preview-manual.sh portal
# Optional native infrastructure-error presentation checks:
bash build/isolated-preview-manual.sh files unavailable
bash build/isolated-preview-manual.sh portal unavailable
```

The launcher uses the relocated runtime, isolated application config/state/cache,
the real Wayland connection and a private chooser bus. Files records passive events
in a distinct session directory under `build/isolated-preview-manual/files`.
Select red/blue/SVG entries; inspect the Files pane, grid and Quick Look; resize and
change selection rapidly; close during pending work; reopen for disk-cache rendering.
In the chooser inspect the wide dock and narrow drawer, then close/cancel. Record
missing/stale pixels, hangs or incorrect errors.

The user subsequently reported that the checks they could execute passed. They could
not close during loading because the fixtures loaded too quickly. Rapid selection
changes briefly showed the loading indicator, noticeably more often than with the
previous implementation even for these small fixtures. This is consistent with the
recorded baseline/isolated timing comparison above (cold medians 8.78/212.87 ms,
disk 2.20/166.82 ms, memory 2.16/92.56 ms, rapid navigation 9.96/191.77 ms), but those
measurements used 1600×1200 synthetic PNGs and are not timings of this manual session.
The isolation startup/verification cost remains present for small files, and memory
hits still inspect the source. This latency regression was not optimized in this
milestone. Optional unavailable-runtime manual checks were not separately confirmed;
their automated presentation coverage passes.
These synthetic checks do not establish new latency, photographic quality, animation
or broader codec guarantees. Full Files/portal component container gates were not
repeated: application changes have focused host-native, packaging and source checks,
while the shared File Browser current-sibling integration gate was run in full.

## Fast in-memory raster revisits (2026-11-08)

A live PreviewService now retains optional successful raster facts (oriented source
size and formatted EXIF) with its decoded pixels. The service-local cache still has
at most two entries, a 64 MiB pixel budget and LRU eviction. Oversized images may be
displayed but are not retained. No cross-service or cross-session inspection cache
is introduced.

Every request still opens the native filename, verifies a regular descriptor and
sniffs MIME. Before isolated inspection, a raster candidate must match device,
inode, size, and nanosecond modification/change times. Pixel adequacy is computed
from the cached oriented dimensions using the existing sizing rules. The descriptor
identity is checked again before accepting the hit. A changed candidate is removed;
the miss path receives the refreshed identity so an old disk thumbnail cannot be
accepted after a same-size edit with restored modification time. New reusable facts
are committed only after successful inspection and pixel acquisition, metadata
formatting, an unchanged descriptor identity and cancellation checks.

This is observed-change detection, **not an atomic filesystem snapshot**. Concurrent
writes after the checks remain possible; an open descriptor pins an inode, not the
pathname or its contents. Failed, cancelled and timed-out work does not populate
reusable facts. Existing generation and timeout suppression protects queued results.
Hits retain progressive pixels followed by final metadata delivery.

Previously validated, adequate in-memory pixels and metadata remain usable if the
runtime subsequently disappears or becomes invalid. A fresh service, evicted entry,
changed source or insufficient resolution still requires the verified runtime and
fails closed when it is unavailable. SVG resource validation, disk PNG isolation,
grid caching, worker counts, decode limits, deadlines and loading presentation are
unchanged. Cold and disk-only latency optimization is outside this change.

### Verification and performance

Native provider coverage includes oriented dimensions and nonempty camera EXIF,
progressive delivery, adequate pane/Quick Look revisits, insufficient resolution,
LRU eviction, combined and single-image budget limits, replacement, restored-mtime
same-size edits, permissions/removal, non-UTF-8 native names and deterministic
mutation during lookup. Barriers cover cancellation, superseding selections,
timeout, shutdown and late replies; cancelled acquisition cannot populate memory.
Disposable runtime bundles qualify warm-hit success and miss/upgrade failure after
runtime removal. Files and chooser offscreen harnesses assert warm final pixels,
source dimensions, MIME and absence of unrelated EXIF while retaining their existing
cold/disk/SVG/error/lifecycle checks.

The installed-consumer benchmark uses the preserved pre-change isolated packages
and optimized host-native packages, identical consumer build settings, 256-pixel
bounds, private empty disk caches, one cold warmup per fixture, then 100 measured
unchanged warm revisits for each of 160×80 (manual fixture size) and 1600×1200 solid
PNGs. Timing includes worker completion and event delivery, with 1 ms event polling.
Both acceptance limits are median ≤20 ms and p95 ≤50 ms. Raw distributions, preserved
packages, benchmark source and separate launch traces are in umbrella
`build/warm-preview`; traced timings are excluded. These synthetic measurements do
not establish photographic, cold, disk-only or cross-host performance guarantees.

Final paired untraced results (median / nearest-rank p95, milliseconds):

| PNG source | Pre-change isolated | Optimized memory hit |
| --- | --- | --- |
| 160×80 | 87.84 / 105.75 | 2.15 / 2.28 |
| 1600×1200 | 83.30 / 99.10 | 2.15 / 2.19 |

Both latency targets pass. A separate exec-only trace completes all 200 warm revisits
with exactly four helper launches, accounted for by inspection and decoding of the
two cold warmups: **zero additional helper launches for adequate memory hits**.
Some traced cold warmups failed and remain failed evidence, excluded from timing
and launch acceptance. The final successful trace is `helpers-confirmed.trace`
with all 200 results in `traced-confirmed.json`; no production deadline was changed.

Native preview tests pass **32/32**; all **12** native CTest entries pass across the
ordinary checks and outside-sandbox runtime-dependent checks. Files' focused
controller/consumer/presentation regressions pass **107/107**. Files and chooser
rendered acceptance passes. Current-sibling container integration passes **12/12**
main tests, **2/2** Core-only tests, formatting, static analysis, QML and REUSE
(`build/acceptance/integration-8ct9kkn2/acceptance.log`). The final refreshed-identity
edge-case fix followed that snapshot; its provider/installed-consumer tests and
changed-source static analysis/format checks pass separately. Unchanged dependency
builds were not repeated. The host analyzer reports two unchanged trailing-comma
style findings outside the edited lines; the changed-line check excludes those.
The provider test source and new Files harness are analyzed in full, with chooser
analysis scoped to the added assertions. Earlier sandbox runtime failures remain
recorded; qualifying native runs have user-manager/namespace access.

### Manual native warm revisits

From the umbrella, use the existing prepared launchers:

```sh
bash build/isolated-preview-manual.sh files
bash build/isolated-preview-manual.sh portal
```

In each live window, select red, then blue, then alternate those two repeatedly.
Check final pixels and dimensions, then repeat after Quick Look/pane transitions in
Files and dock/drawer resizing in the chooser. Selecting SVG as a third entry may
evict one raster; reopening a window tests disk reuse rather than this memory path.
Record stale pixels/metadata, visible delays or hangs. Native pointer/focus interaction
remains user-driven. The user subsequently confirmed that the manual warm-revisit
checks passed and authorized committing, publishing and pinning the milestone.
Deployment remains outside this milestone.

## Experiment 0 scheduler control (2026-11-09)

The umbrella [installed-consumer harness](../../../../tooling/preview-benchmark/README.md#experiment-0-attribution-and-scheduler-evidence-2026-11-09)
now exercises ThumbnailProvider itself, including queueing, response completion,
cancellation, cache hits and draining. Fresh A/B installed PreviewQuick packages
use the frozen revisions with two explicitly configured threads and 256-pixel
boxes. Production defaults remain unchanged. Each batch uses 16 distinct copies;
cold, same-provider warm repeat and first-12 cancellation workloads rotate four
frozen fixtures. `grid-decode` remains the separate synchronous decoder case.

Issued requests, decoder entries, cancelled requests that never entered decoding,
cancelled requests that did enter, finished responses and validated delivered
images are independent counters. Cancellation is not evidence of interrupted
codec execution. Responses survive through completion; pending work drains before
teardown. Controlled decoder barriers verify queueing, cancellation before/after
entry, warm hits with no decoding, exact synthetic pixels, response destruction
while decoding, and complete draining against A/B/D. These are response delivery
observations, not rendered QML frame times.

The scheduler run stopped at the first unexplained cleanup failure, preserving
418 exploratory batches. Those batches repeated one fixture each and are
excluded from the requested mixed-fixture control. The final harness cycles all
four fixtures within every 16-request batch and passes in-process smoke checks;
no corrected B measurement followed the cleanup failure. Frozen B's oriented-JPEG cancellation batch delivered three of
four surviving requests; all responses finished and pending work reached zero.
No retry or extra manager cleanup followed. Warm batches in both baselines entered
no decoder. Concurrent preparation work and incomplete counts limit latency
interpretation. The
[umbrella report](../../../../docs/initiatives/image-isolation.md#experiment-0-missing-evidence-2026-11-09)
records exact counts, paths and the separate D attribution/resource passes.

Diagnostic consumer probes exist only in archived source overlays: job dispatch,
worker entry, cache lookup, publication and actual delivery correlate provider
calls without changing public APIs, messages, cache policy or the three-second
deadline. The instrumented packages pass all five focused isolation regressions
and existing Files/chooser offscreen acceptance. Historical cleanup causes remain
unresolved; no optimization is selected or implemented while Experiment 0 remains
incomplete.

## Corrected Experiment 0 control (2026-11-09)

The unfinished scheduler requirement is superseded by A/B2: A remains frozen;
B2 keeps File Browser `2f86c9b` and Thumbnails `9462bac` and consumes corrected
Images cleanup. D2 applies diagnostic overlays to that same implementation.
Original A/B latency, both historical failures, both subsequent recurrences, and
the excluded homogeneous scheduler run remain unchanged historical evidence.

The harness validates four copies of each frozen fixture, two configured threads
and 256-pixel boxes before measurement and again when reporting batches. A shared
private gate prevents decoder admission after a reported cleanup failure; already
admitted work drains. Barrier tests cover that stop, cancellation before/after
entry, warm cache hits without decoding, response lifetime and draining. No
consumer production behavior, defaults, UI, or revision pins change. See the
[benchmark correction note](../../../../tooling/preview-benchmark/README.md#corrected-cleanup-iteration-ab2-and-d2)
and umbrella correction note for actual denominators and verification results.

Iteration result: **corrected measurements remain incomplete**. An initial packaging
error silently skipped the cleanup patch while reporting success. The resulting
mislabeled lifecycle (1,000 cycles), attribution (280 samples), resources (84
samples) and scheduler (50 attempted batches, one failed warm setup) are preserved
and excluded from corrected evidence. The scheduler cleanup recurrence stopped
measurement without retry or extra manager cleanup. The packaging regression is
fixed and tested; new verified A/B2 and D2 packages were built, but were not measured
again in this iteration. See the [umbrella audit](../../../../docs/initiatives/image-isolation.md#cleanup-observation-correction-and-excluded-iteration-2026-11-09) for artifact paths,
denominators, the preserved failure, verification and remaining gaps.

## Fresh corrected Experiment 0 preparation gate (2026-11-09)

The fresh iteration `build/preview-iteration-qyruwtid` stopped before measurement:
B2's `build/holonight-images/install_manifest.txt` differs from its frozen package
inventory (expected `42f04300fb75b3bf77a6fe41a21460811491cdd63095bc98ce367c9606e0f325`,
observed `5c93d2e527707c3879ff5c140ae8216de920c24b5a1d42a97451e88c833f79c0`).
Scheduler `preview-scheduler-f2c1ty8f` and diagnostic `preview-diagnostic-ymcm3gs7`
remain unchanged. No measured pass or rebuild was launched. Lifecycle 1,000 cycles,
D2 attribution 280 samples, resources 84 samples and A/B2 600 batches are entirely
unattempted; attempted/completed/failed/excluded counts are all zero. All 3,000
lifecycle operation timings and 964 later observations remain missing.

The runner now preserves preparation failures and checks lifecycle records even
on exit zero; missing/malformed output and runtime/provider failures gate later
passes. All 25 focused tooling tests, Python compilation and whitespace checks
pass. Previously recorded cleanup/provider, installed/relocated,
scheduler-barrier and Files/chooser acceptance is reused. The artifact mismatch
must be resolved before native measurement; no performance or cleanup conclusion
is available, and runtime-generation reuse remains deferred. Historical evidence,
failures and exclusions are preserved. See the umbrella image-isolation note's
"Fresh corrected Experiment 0 preparation" section for hashes and denominators.
