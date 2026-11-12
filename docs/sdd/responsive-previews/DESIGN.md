# Responsive raster previews

Status: implementation present; qualification is incomplete. No responsiveness
claim is established until the frozen latency comparison and all correctness,
resource and installed-consumer gates pass.

The [Images contract](../../../../holonight-images/docs/sdd/bounded-sandbox-reuse/DESIGN.md)
owns protected leases, synchronous transactions, session negotiation and cleanup.
File Browser owns the scheduling and cache policy described here. Files and the
chooser consume the existing Preview/PreviewQuick package surfaces. This change
adds no grid policy, UI wording, default size or format changes.

## Threads and request flow

The existing control worker opens native paths with nonblocking/close-on-exec
flags, checks regular-file identity, determines MIME and performs an adequate
memory lookup. It never waits for a raster session to decode or retire. Each of
two slots owns its own thread, helper and Qt devices. Only native descriptors and
immutable request values cross to a slot; its `QFile` is constructed there.
Source identity is checked again after descriptor transfer.

Control requests coalesce to the latest pending selection. Raster scheduling
allows one active job and one latest pending job. Superseded pending closures
release their descriptors. Adequate memory hits return through the control worker
while another slot decodes or retires. Text and SVG keep their existing control
worker policy; grid decoding and cache capacity remain unchanged.

The two-entry/64-MiB memory cache uses one short-held mutex for lookup, promotion,
insertion and removal. No I/O, callback, protocol exchange or helper cleanup runs
under that mutex. Raster facts travel with cached pixels, so hits retain oriented
source dimensions and EXIF. Native bytes identify sources; display strings do not.

Disk lookup and source decode continue to use the borrowed `SourceTransaction`
coordination interface. A helper transaction still inspects the source on disk
hits. Successful results persist eligible thumbnails before pixel delivery, then
publish final source metadata after pixels. Existing source mutation checks,
resize adequacy and selection-generation rejection remain in force. The UI's
three-second timeout remains independent of the transport deadlines.

## Slot policy

Slots have `Empty`, `Preparing`, `Ready`, `Active` and `Retiring` states.
Preparing and retiring helpers consume the two-helper cap. The first protected,
session-capable worker is prepared asynchronously at service startup. A standby
is prepared only at the actual transaction-start boundary. An adequate memory
hit, local rejection or cancellation before dispatch does not trigger standby
preparation or increment the started-transaction count.

The pool recycles after 32 started transactions or 60 seconds since readiness.
An in-flight job finishes before age retirement. Ready workers close after
30 seconds idle, without replacement solely because they expired. Local
pre-dispatch work does not reset their idle age.

Scope `memory.current` is sampled on preparation and job completion before Ready
admission, and periodically while idle. Unknown measurements retire the worker.
Idle workers are retired, largest allocation first, until aggregate retained idle
memory is at most 64 MiB. A preparation/readiness failure is recorded for its
demand and does not schedule its own retry. A later request or explicit refresh
can authorize a new attempt. Retirement of existing workers continues even when
the owner no longer authorizes acquisitions; revocation never suspends idle expiry.

A cancelled dispatched transaction is poisoned and its cleanup finishes before
another decode, including on an existing standby. The control worker remains
available throughout that cleanup. Cancellation is checked again when a completion
returns to control; a cancelled used worker cannot slip back into readiness.
Owner cleanup failure disables further launches/transactions. The Images layer
pins unresolved resources instead of retrying manager cleanup.

Mutable raw development bundles execute fully validated one-shot jobs in slots.
Protected legacy generations without a session declaration use owner-validated,
leased one-shot execution, preserving rollback compatibility. A failed managed
owner selection never falls back to an old lease or mutable-directory execution.

## Refresh and shutdown

`refreshRuntime()` requests a new owner epoch on control. Old idle sessions retire;
obsolete preparation completions cannot become Ready. Active jobs may finish with
their original leases. Failed refresh revokes new acquisitions without stale
fallback. Refresh does not change the current presentation or add a respawn loop.

Shutdown rejects new work, cancels queued/preparing/active requests, drains both
slot threads and their cleanup, releases the owner, then finishes the control
thread and emits `shutdownFinished`. Failed cleanup is reported and quarantined
by Images; completion of shutdown is not a claim that an unverified scope became
safe to remove. Queued control callbacks carry a lifetime guard.

## Verification and targets

Injected clocks and preparation/readiness/memory/retirement boundaries test
recycling, age, idle expiry, idle memory, failure without retries, refresh races,
latest pending selection and cancellation cleanup before standby use. Native
regressions retain format/orientation, precision, cache commit, mutation, native
filename, resize and stale-result checks. A blocked-decode regression proves that
an adequate memory hit can deliver before the old decode unblocks.

Core-only builds and installed C++/QML consumers remain package gates. Staged and
relocated Files/chooser acceptance must exercise mutable one-shot and protected
reusable installations. Native host-linked Release binaries are the measurement
artifacts; container binaries are acceptance only.

The frozen comparison contains the transaction baseline, owner-validated protected
one-shot variant, and this scheduler with reusable sessions. Ready lightweight
and every disk-hit fixture require median <=30 ms and p95 <=50 ms for both pixels
and final metadata. Memory median/p95 may regress at most 0.2 ms. Constructor
startup, depletion and navigation p95 may regress at most max(5 ms, 10%).
Failures and missing samples are never trimmed. Lifecycle/resource diagnostics
run separately from latency. The
[umbrella assessment](../../../../docs/initiatives/immutable-runtime-standby.md)
links results and outstanding gates; no low-end hardware claim is made.


The completed native comparison records 4,000 successful reusable observations
and passes all calculated numerical gates. Lightweight ready final-metadata
median/p95 are at most 6.661/14.441 ms; disk-hit median/p95 at most 3.850/4.619 ms.
Across pixels and metadata, memory-hit median/p95 regress by at most 0.141/0.003 ms;
genuine-startup p95 regresses at most 8.171%. The strict comparison remains
incomplete because the frozen baseline has eight cleanup-flagged navigation
entries and 17 missing placeholders. Its partial quantiles are not qualified
reference results. The umbrella assessment preserves the complete evidence.
