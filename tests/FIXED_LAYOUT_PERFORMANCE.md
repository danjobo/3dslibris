# PDF / CBZ performance capture

Use `3dslibris-debug.3dsx` or `3dslibris-debug.cia`. Measurements are enabled only in debug builds and written as `PERF` lines to `sd:/3ds/3dslibris/3dslibris.log`.

## Repeatable capture

1. With the application closed, save the previous log under another name.
2. Record the device (Old/New 3DS or Azahar version), CIA/3DSX, SD card, document name, orientation and zoom. The log records New 3DS/homebrew flags but cannot identify an emulator reliably.
3. Open a PDF or CBZ, then visit the same ten pages on each device. Wait 10–15 seconds on each page, or longer if a PDF is still refining.
4. Return through those pages to compare cached and uncached work. Repeat at another zoom, keeping settings identical across devices.
5. Exit normally and copy the log before the next run. Send the complete log together with the device/settings notes and page numbers that felt slow.

## Reading the measurements

Durations `us` are microseconds (divide by 1000 for milliseconds). `at_us` is the capture timestamp, not the time the line was written. Pages are one-based; page 0 is document opening. Zoom -1 means unspecified.

| Stages | Work measured |
| --- | --- |
| `pdf.open_file`, `pdf.index_metadata`, `pdf.open_total` | MuPDF opening, page count/metadata and total opening |
| `pdf.load_page`, `pdf.display_list` | Page loading/bounds and display-list construction |
| `pdf.raster`, `pdf.rgb565` | Rasterization including pixmap setup; output allocation/conversion to RGB565 |
| `pdf.preview_total`, `pdf.interactive_total` | Whole render calls, including their subphases and cleanup |
| `pdf.strip_worker`, `pdf.strip_sync` | Each final-quality strip, including rasterization and conversion |
| `cbz.index_zip`, `cbz.open_total` | Archive indexing and total document opening |
| `cbz.read_zip`, `cbz.decode` | Entry lookup/read/inflate; image decoding including fallback attempts |
| `cbz.scale_preview`, `cbz.scale_interactive` | Scaling for each cache |
| `cbz.prefetch_*` | Read, decode and combined scaling during prefetch |
| `first_present`, `preview_present`, `interactive_present`, `final_present` | Elapsed time from the first draw of that view until a buffer swap containing that quality |

A new `view` starts when the document, page, zoom or target dimensions change, or after leaving the reader. Presentation timings start in the draw path, **not** at button press/book selection; opening is measured separately. They include scheduled quality delays and intervening logging. A buffer swap is not a physical display-latency measurement. Each quality is recorded only once per view. CBZ uses interactive quality as its best cached level and does not emit `final_present`.

Use `doc` and the filename mapping to associate records. CBZ opening uses a separate `CBZ_OPEN` identity; match it to the rendering identity by filename. Filenames are truncated to 127 bytes. Cached pages can omit read/decode/render stages. Missing completion records after a failure or cancellation do not mean zero duration. Totals include their subphases: do not add both when calculating work. PDF file/image/font reads can occur inside MuPDF parsing and rendering, so these timings do not isolate SD I/O.

`bytes` and `size` describe the relevant payload/output; strip bytes describe the shared destination buffer, not additional allocation per strip. Prefetch scaling size describes the interactive output. Memory rows sample heap usage/free space, free linear memory and free memory regions after presentation, in bytes. Region free space is different from allocator free space. These are snapshots, **not peak-memory measurements**.

Workers enqueue records in a fixed 64-event RAM queue. The main thread writes them after presentation; workers do not write these logs to SD. `PERF dropped=N` means the capture is incomplete. Debug logging itself adds overhead, especially on slow SD cards. Compare equivalent debug captures; emulator timings do not predict console performance. Existing lifecycle/error logs remain available for crash diagnosis.

## Zoom reuse and ZIP breakdown

When zooming a PDF on the same page, the display list is retained. Expect new raster/conversion work but no new `pdf.display_list` construction until the page changes or the document/view state is reset.

`cbz.reuse_source` records a zoom increase served from an existing decoded bitmap with sufficient width and height. Its `us=0` is a marker, not a measured duration. `size` is the reused source size. Such a change should not need `cbz.read_zip` or `cbz.decode`. Other zoom increases can still require a larger decode; the optimization must not reduce sharpness.
The direct-source synchronous draw can also reuse a source without emitting this marker; absence of ZIP/decode stages for that view confirms reuse.

In the synchronous CBZ reader, a decoded source with enough resolution is drawn directly. Its view has no `cbz.scale_interactive` record or second full-page bitmap. That scale stage remains for a lower-resolution decode fallback and for the deferred reader path.

At low zoom, synchronous CBZ decoding requests at least zoom index 4 as its source-quality floor. `cbz.decode_target` can therefore report a larger target than the current view's zoom suggests. A failed high-resolution decode still falls back through lower zooms. This favors text legibility and reuse when zooming in, at the cost of a slower first decode and a larger source bitmap.

ZIP reads additionally emit `cbz.zip_open`, `cbz.zip_locate_offset` (or `cbz.zip_locate_name`) and `cbz.zip_read_inflate`. The latter includes entry metadata, opening the compressed entry, reading/inflating it and closing the entry. Closing the archive itself remains included only in the outer `cbz.read_zip` timer. These nested stages must not be added to that outer total.

ZIP subphases have a separate entry identity (`doc`, page 0, zoom -1). Two mapping records identify the archive (`CBZ_ZIP`) and entry (`CBZ_ENTRY`) for that identity. They also cover reads outside the viewer, such as cover loading. The offset path can fail and then succeed by name; an individual `ok=0` does not necessarily mean the entire page failed. The extra debug lines add logging overhead after presentation and can affect subsequent timing.

## Drawing and presentation breakdown

The following detail is emitted only before the first presentation of a view (document/page/zoom/target size). Repeated idle frames and later quality refinements do not emit it. This avoids per-frame logging; existing quality milestones remain unchanged.

| Stage | Scope |
| --- | --- |
| `pdf.prepare` | Cache invalidation and page metrics before ensuring the preview |
| `pdf.ensure_preview` | Preview lookup/build, including nested `pdf.preview_total` if rendering is needed |
| `pdf.ensure_interactive` | Synchronous main-image lookup/build, including nested `pdf.interactive_total` |
| `pdf.view_setup` | Viewport, preview geometry and text-style setup |
| `pdf.blit_main` | Clear and draw the main reading screen, including scaling/filtering |
| `pdf.preview_background` | Clear the overview screen, draw its gradient and paper background |
| `pdf.blit_preview` | Scale/copy the overview image |
| `pdf.overlay` | Preview border, viewport indicator and restoration of text state |
| `pdf.draw_total` | Enclosing duration of the PDF drawing phases above |
| `draw_to_present` | From `Drawn` to the buffer swap, including remaining reader/UI work and presentation |
| `present.framebuffer_copy` | Dirty-screen check and copy of software screens into the framebuffer |
| `present.flush` | CPU time in `gfxFlushBuffers` |
| `present.swap` | CPU time in `gfxSwapBuffers` (not physical display latency) |

The `present.*` and `draw_to_present` stages apply to CBZ too. A framebuffer-copy record with `ok=0` means no buffer was written on that attempt, not necessarily a failure. The PDF phase records describe elapsed work; renderer failures retain their existing nested error records.

Compare `pdf.draw_total + draw_to_present` with `first_present` (small differences come from instrumentation and setup boundaries). Do not add enclosed phases or render totals again. To isolate the old unexplained gap, inspect `pdf.blit_main`, the three overview/overlay phases, then `draw_to_present`; within the latter, subtract the `present.*` durations to estimate the remaining reader/UI work. No measured interval should automatically be interpreted as an intentional delay.

## Retained CBZ archive and decode phases

The main CBZ view retains a private ZIP handle across page changes. `cbz.zip_reuse`
(`us=0`) replaces `cbz.zip_open` on subsequent reads. Covers and background work
still use independent handles. The reader closes on destruction, unrecovered read
failure, path changes and view resets that restart workers (including resume).
The next read opens lazily. Archive teardown is no longer part of every page read.

On 3DS, `cbz.decode_*` splits the existing `cbz.decode` total into context setup,
compressed-buffer copy, image header loading, pixmap decoding, residual subsampling,
RGB565 allocation/conversion and resource release. `cbz.decode_target` is a zero-time
marker containing the requested dimensions. Header, pixmap and subsample stages
report their respective image dimensions. These nested records use the input byte
vector as `doc`, page 0 and the attempted zoom: correlate them chronologically with
the enclosing decode, not by matching its viewer pointer. Failed fallback attempts
can omit the unfinished phase; the outer decode result remains authoritative.
Host decode tests use stb_image; they do not execute this MuPDF path.
