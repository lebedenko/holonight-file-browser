# Shared file selection toolkit

## Intent and dependency direction

Files, the portal chooser and Viewer construct separate browsing sessions. Worker
services feed models, sorting/filtering, identity-based selection and passive views.
Application controllers own navigation, activation, commands, focus and persistence.

| Component | Responsibility | Installed dependencies |
|---|---|---|
| Core | Native paths, batched scanning, reader, ordering, selection, places | Qt Core |
| Quick | Passive list, grid, places surface and icon provider | Core, Qt Quick, HoloNight Qt |
| Search | Folder matching, recursive traversal, index lifecycle and snapshots | Core, HoloNight Search |
| Storage | Device model, explicit actions and capacity work | Core, System Services Storage |
| Preview | Image/text inspection, decode and cache coordination | Core, Qt Gui/Svg, Images, Thumbnails |
| PreviewQuick | Bounded thumbnail provider, image item, passive text presentation | Preview, Quick |
| Markdown | Restricted Markdown document presentation | PreviewQuick, QMarkdown |

## Contracts

Native Linux bytes are authoritative. `native_path.h` converts local URLs without a
Unicode round trip and rejects nonlocal URLs and embedded NULs. `entryIdentity`
distinguishes real entries from synthetic parent rows. Selection is a value object:
current, anchor, single, toggle, range and explicit reconciliation. Consumers choose
when navigation or filtering clears selection and what replaces a missing current item.

`walkDirectory` is the supported worker interface for the reader and specialized
application models. It delivers batches of at most 250 entries, with a 25 ms flush
threshold. Callbacks execute on the worker; callers provide bounded queued delivery
(two slots in Files and DirectoryReader), generation rejection and asynchronous
shutdown. Cancellation suppresses further callbacks; blocked filesystem calls cannot
be interrupted. Keep an asynchronous owner alive until its shutdown signal.

DirectoryReader loads incrementally and offers refresh reconciliation by native
identity without resetting surviving rows. Sorting defaults to Files' natural locale
ordering; deterministic numeric filename ordering and grid movement are separately
exported for Viewer. Files owns its editing/placeholder proxy, watcher coordination,
restore behavior and mutation suspension.
Its proxy derives from the shared proxy and overrides only editing locks and
placeholder comparisons, keeping the ordinary sorting and filtering implementation shared.

Recursive search receives roots, hidden policy, exclusions, result limits and an
optional cache path. Files' adapter reloads its settings on start/warmup, preserves the
last valid policy after parse errors and retains its freshness/invalidation behavior.
Native-byte identities survive index ranking and version-2 snapshots; version-1
snapshots are disposable and rebuilt at the existing application cache path. Symlinked
directories are not traversed. Folder matching never starts recursive traversal.

PreviewService has a dedicated selected-preview worker, separate from the bounded
thumbnail provider. Existing Files/Viewer decode limits, orientation, DPR sizing,
SVG local-resource policies, source-replacement checks and typed preview failures
are retained. Images owns decoding, Thumbnails owns disk-cache format, Search owns
ranking and System Services owns UDisks. Cache failures do not prevent rendering.
Text is capped and preserves complete UTF-8 characters. Markdown accepts only the
application-authorized local roots; HTTPS and qrc resources remain disabled.
Selected previews accept the native path separately from their display path.
`ThumbnailRequest::native_path` and `ThumbnailProvider::idForNative` carry the same
identity through descriptor-based decoding, URI disk-cache keys and memory-cache keys.
Qt's filename-based SVG resource fallback and Markdown local roots are disabled when
the native filename cannot round-trip through Qt's display string representation.

Places accepts caller bookmarks; applications own their files. Storage actions are
explicit requests. Removal/failure signals return control to applications for recovery
navigation and feedback. No provider owns an application settings registry.

## Consumer boundaries

Files retains editing, operations, modal commands, history, settings, preview selection
and window layout. Provider classes can be registered under the existing Files QML
contract. Viewer retains its image document, full-resolution rendering, animation,
explicit-file inclusion, missing-current-file policy and portal Open; its folder adapter
uses the public scanner and explicit ordering policy. The portal retains protocol,
request lifecycle, parenting, filter/MIME interpretation, destination validation,
choices and overwrite confirmation. New portal previews, devices and grid mode are
outside this migration.

## Verification

Provider tests cover native filenames, cancellation, incremental scans, refresh,
selection reconciliation, native search/cache round trips and external C++/QML consumers
for every component. Core-only is configured separately. Existing application suites
remain the regression oracle for editing, navigation, preview limits, device recovery,
grid selection, playback boundaries and portal protocol. The umbrella tests ownership
transfer, duplicate staging ownership, collisions and conservative uninstall in disposable
roots. Native desktop checks are user-operated. Publication is a separate authorized step.
