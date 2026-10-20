# Preview enum registration

PreviewService exposes scoped-only QML enums. Use
`PreviewService.PreviewErrorKind.None`, `PreviewService.PreviewConsumer.Pane`,
`PreviewService.PreviewConsumer.QuickLook`, and
`PreviewService.QuickLookEligibility.Unsupported`.

Flattened aliases such as `PreviewService.None`, `PreviewService.Pane`, and
`PreviewService.Unsupported` are unavailable. Migrate consumers to explicit enum
scopes before using this provider. Both PreviewErrorKind and QuickLookEligibility
contain Unsupported with different values; disabling unscoped registration prevents
those names from colliding. C++ enum names, values, properties and behavior are unchanged.

Files retains its QML_FOREIGN registration. Its isolated
`files-preview-scoped-enums` CTest exercises that real registration, checks scoped
values and absent aliases, and rejects enum-clash diagnostics. Build and install
this provider into a temporary prefix before building the migrated Files consumer.
