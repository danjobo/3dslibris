# Library and settings responsiveness capture

Use a debug build. These changes keep background workers disabled; a single
cover/metadata extraction still runs synchronously and can exceed 3 ms. The main
loop now handles input and presents the result before idle work, does not start a
job on a frame with held/new keys, and returns to input after each browser job.

## Capture

1. Browse several library pages quickly, then leave each still long enough for
   covers to load. Repeat those pages after the covers are cached. Compare list
   and gallery views. Note which book was selected when a stall occurred.
2. Open settings and change font size/spacing several times rapidly. Stop for a
   second. Repeat and leave settings immediately. Reopen settings and restart
   normally to verify persistence. Repeat for global and per-book settings.
3. Open a large EPUB and MOBI, return to library and reopen each. Change font size
   and repeat. Keep book names and whether caches already existed in your notes.
4. Open PDF/CBZ and verify the prior pad/stylus smoothing fix still works. Test
   HOME and restore when testing on console.
5. Exit normally and copy `3dslibris.log` into the matching build folder.

## Logs

* `TIMING: job=index|cover|toc ... ms=... book=...`: one record per executed
  browser job (debug only). Find long individual jobs before trying to split
  their implementation or enable workers. Indexing a book outside the visible
  page no longer requests a full library redraw. Cover warmup redraws only the
  lower gallery; transitions back to the browser refresh the upper splash.
* `TIMING: ui_frame mode=... ms=... keys=... held=...`: frames whose dispatch and
  presentation took at least 32 ms. Excludes the idle work after presentation and
  excludes time waiting for input/VBlank. This is not physical input-to-display
  latency. Repeated pan frames may appear; idle frames below the threshold do not.
* `TIMING: prefs_write ms=... ok=... books=...`: actual preference file writes.
  Rapid settings changes request a save after 2 s; pending saves wait for
  released input or are forced on leaving settings. Existing explicit writes for
  book transitions and normal exit remain immediate. Errors retry after 5 s.
* Existing `EPUB: timing`, `MOBI: timing` and `FlushPendingCacheSaves` records
  locate opening/cache work. They do not yet isolate every CSS/font/layout phase.

Compare the same device, books, library size, cache state and build type. Debug
logging adds SD work, so these results do not directly predict release timings.

## Preference recovery

Writes go to `3dslibris.xml.tmp`. Stream errors, flush and close are checked before
replacement. The previous primary is moved to `3dslibris.xml.bak`, then the
completed temporary becomes primary. If installation fails, rollback is attempted.
When primary is missing on startup, the backup is read; a partial temporary is
never loaded. The backup is retained after success. Corrupt existing primary XML
is not silently replaced with the backup.

This reduces interruption-related loss but is not a claim of power-loss atomicity
or durable media flushing on every SD backend. Changes not yet saved in the
2 s coalescing window can be lost on abrupt termination. No new filesystem
work was added to APT suspend hooks. Confirm normal persistence and HOME/restore
on actual hardware before release.
