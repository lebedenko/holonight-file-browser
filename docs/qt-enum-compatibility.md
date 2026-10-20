# Qt-recognized enum base types

DevicesModel::Verb uses qint8 and PreviewService::PreviewErrorKind,
PreviewConsumer and QuickLookEligibility use quint8, with explicit QtTypes includes.
These aliases preserve enum names, values, width, signedness and ABI. Scoped-only
registration and consumer QML_FOREIGN wrappers remain unchanged. Unrelated enums
are unchanged; Qt 6.11 remains supported.

Build and stage this provider into an isolated prefix before configuring Files.
Use matching CMake, QML and runtime library paths. Run preview/storage regressions,
installed Storage/Preview/PreviewQuick consumers and Core-only acceptance, then
component task check. Validate separate Qt 6.11 and 6.12 builds where available.
See the [Files compatibility note](../../holonight-files/docs/qt-enum-compatibility.md)
and [umbrella contract](../../docs/qt-enum-compatibility.md).
