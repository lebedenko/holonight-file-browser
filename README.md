# HoloNight File Browser

Independent, application-neutral browsing and inspection provider for Files, Viewer
and the FileChooser portal. This repository owns the installed `HolonightFileBrowser`
package and `Holonight.FileBrowser` import previously shipped by Files.

Consumers use `find_package(HolonightFileBrowser CONFIG REQUIRED COMPONENTS ...)`
and installed `HolonightFileBrowser::Core`, `Quick`, `Search`, `Storage`, `Preview`,
`PreviewQuick` and `Markdown` targets. No consumer adds provider sources or private
include directories. Core requires only Qt Core; Markdown alone introduces QMarkdown.

Each component has a `BUILD_FILE_BROWSER_*` CMake option. For a Core-only build, turn
QUICK, SEARCH, STORAGE, PREVIEW, PREVIEW_QUICK and MARKDOWN off. PreviewQuick requires
Quick and Preview; Markdown requires PreviewQuick. Other components are independent.

Use `task build` / `task test` with installed host dependencies and an explicit
`CMAKE_PREFIX_PATH` for development providers. `task check -- --siblings /path/to/umbrella`
accepts current sibling checkouts before publication. All installation tests stage into
build directories; system installation belongs to the umbrella.

See [design and API ownership](docs/sdd/shared-file-selection/DESIGN.md).

`Holonight.FileBrowser` also exports `DetailedColumnLayout`, `DetailedListHeader`
and `DetailedListRow`. Give the header and rows the same column-layout object;
consumers retain sorting, selection, navigation and editing. `BreadcrumbAdapter`
keeps display labels separate from native ancestor identities. The optional
`Holonight.FileBrowser.Preview` import exports `FilePreview`, a scrollable image/icon
and metadata composition driven by a caller-owned `PreviewService`.
See the [presentation interfaces and migration design](docs/sdd/shared-file-ui-controls/README.md).

Directory listings contain actual children only, including hidden entries before filtering.
Empty directories have zero rows. The scanner skips `.` and `..` and never reads parent
metadata or synthesizes navigation rows. Consumers own parent navigation through their
commands and toolbar controls. `entryIdentity()` preserves the `entry:` prefix and native
path bytes. The former parent field, role and synthetic-entry helper are removed;
remaining role numbers are unchanged. Rebuild Files, Viewer and the portal together
against the updated installed provider; this API removal has no compatibility shim.
