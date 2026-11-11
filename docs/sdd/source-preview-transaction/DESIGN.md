# PreviewService source transactions

PreviewService preserves its adequate memory-cache fast path. A raster miss uses
one Images source transaction: inspect source metadata, compute existing required
size/tier, traverse disk candidates once and decode retained source when needed.
The existing worker count, memory-cache capacity and presentation remain unchanged.
Standalone decoding, grid thumbnails and SVG keep their existing paths.

Source inspection supplies dimensions, orientation and EXIF even on disk hits.
No pixels, final metadata or cache writes escape before verified cleanup.
Successful source decoding may populate the disk cache after checking source
identity. Pixels are notified before final metadata; stale-generation and resize
adequacy protections remain in PreviewService. Quick Look uses this same service.

See the [Images](../../../../holonight-images/docs/sdd/source-preview-transaction/DESIGN.md)
and [Thumbnails](../../../../holonight-thumbnails/docs/sdd/source-preview-transaction/DESIGN.md)
contracts and [umbrella evidence](../../../../docs/initiatives/source-preview-transaction.md).
