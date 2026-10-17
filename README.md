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
