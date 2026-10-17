# Shared file UI controls

Shared file presentation uses Qt's passive controls. `DetailedColumnLayout` is a
single width contract for `DetailedListHeader` and `DetailedListRow`: Modified
hides below 640 logical pixels, Size below 420. Consumers format values and own
sortRequested, selection and activation. Rows expose gutterContent, selectionContent,
filenameContent and editorContent slots; editing chooses the editor slot.
DirectoryListing's existing delegate override remains intact.

BreadcrumbAdapter owns separate ordered labels and native byte ancestors. Invalid
indices return an empty identity. It lives in Core; only QML registration lives in
Quick. FilePreview lives in PreviewQuick's existing import and consumes a caller-owned
PreviewService and formatted metadataRows. The complete preview/metadata composition scrolls
inside its assigned geometry; image requests follow the frame size and window DPR. Decoding, cancellation and resource limits
remain in the existing service. Core-only configurations require no Quick dependencies.

Acceptance includes undecodable path identities with identical labels, installed
C++/QML consumers, optional components, compact columns, and existing stale-result tests.

Verification results and compatible provider revisions are recorded in the umbrella
[initiative ledger](../../../../docs/initiatives/shared-file-ui-controls/TASKS.md).
