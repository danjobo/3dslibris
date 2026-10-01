/*
    3dslibris - sync_merge.h

    Merging per-book state from two consoles. Pure and deterministic: both
    consoles merge the other's state into their own and end up identical.

    - Reading position: the newer last_read wins ("most recently read").
    - Highlights and bookmarks: combined by id. For the same id the newer
      modified wins; on a tie a deletion wins, then a fixed field order
      decides, so the result never depends on which side merges.
*/

#pragma once

#include <stdint.h>
#include <string>

#include "book/annotation.h"

namespace sync_merge {

// Identifies "the same book" across consoles: file name and size (folders
// can differ between consoles).
std::string MakeSyncBookId(const std::string &file_name, uint64_t file_size);

struct MergeStats {
  int records_added;    // new from the other console
  int records_updated;  // changed (edits, deletions) by the other console
  bool progress_changed;

  MergeStats() : records_added(0), records_updated(0), progress_changed(false) {}
  bool Changed() const {
    return records_added || records_updated || progress_changed;
  }
};

// Returns local merged with remote; stats describe what changed locally.
BookState Merge(const BookState &local, const BookState &remote,
                MergeStats *stats);

} // namespace sync_merge
