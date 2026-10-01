#include "sync/sync_merge.h"

#include <algorithm>
#include <map>
#include <utility>
#include <stdio.h>

namespace sync_merge {

namespace {

// Total order on two versions of the same record: true if b should replace
// a. Newer edits win, deletions win ties, then plain field comparison keeps
// the choice independent of merge direction.
bool Prefer(const Annotation &a, const Annotation &b) {
  if (a.modified != b.modified)
    return b.modified > a.modified;
  if (a.deleted != b.deleted)
    return b.deleted;
  if (a.note != b.note)
    return b.note > a.note;
  if (a.quote != b.quote)
    return b.quote > a.quote;
  if (a.prefix != b.prefix)
    return b.prefix > a.prefix;
  if (a.page_hint != b.page_hint)
    return b.page_hint > a.page_hint;
  if (a.color != b.color)
    return b.color > a.color;
  return false;
}

bool SameContent(const Annotation &a, const Annotation &b) {
  return a.kind == b.kind && a.modified == b.modified &&
         a.deleted == b.deleted && a.note == b.note && a.quote == b.quote &&
         a.prefix == b.prefix && a.color == b.color;
}

// Upload state from either version: the later upload, and any known id
// (the one that goes with the later upload if both know one).
void CombineUploadState(const Annotation &a, const Annotation &b,
                        Annotation *out) {
  const Annotation &later =
      b.readwise_uploaded > a.readwise_uploaded ||
              (b.readwise_uploaded == a.readwise_uploaded &&
               b.readwise_id > a.readwise_id)
          ? b
          : a;
  const Annotation &other = &later == &a ? b : a;
  out->readwise_uploaded = later.readwise_uploaded;
  out->readwise_id = later.readwise_id ? later.readwise_id : other.readwise_id;
}

bool PreferProgress(const ReadingProgress &a, const ReadingProgress &b) {
  if (a.last_read != b.last_read)
    return b.last_read > a.last_read;
  if (a.quote != b.quote)
    return b.quote > a.quote;
  return b.page_hint > a.page_hint;
}

} // namespace

std::string MakeSyncBookId(const std::string &file_name, uint64_t file_size) {
  char size[32];
  snprintf(size, sizeof(size), "%llu", (unsigned long long)file_size);
  return file_name + "#" + size;
}

BookState Merge(const BookState &local, const BookState &remote,
                MergeStats *stats) {
  MergeStats local_stats;
  BookState merged;

  // Records by id.
  std::map<uint64_t, Annotation> by_id;
  for (size_t i = 0; i < local.records.size(); i++) {
    const Annotation &a = local.records[i];
    std::map<uint64_t, Annotation>::iterator it = by_id.find(a.id);
    if (it == by_id.end())
      by_id.insert(std::make_pair(a.id, a));
    else if (Prefer(it->second, a))
      it->second = a;
  }
  for (size_t i = 0; i < remote.records.size(); i++) {
    const Annotation &r = remote.records[i];
    std::map<uint64_t, Annotation>::iterator it = by_id.find(r.id);
    if (it == by_id.end()) {
      by_id.insert(std::make_pair(r.id, r));
      local_stats.records_added++;
      continue;
    }
    const Annotation mine = it->second;
    if (Prefer(mine, r)) {
      if (!SameContent(mine, r))
        local_stats.records_updated++;
      it->second = r;
    }
    CombineUploadState(mine, r, &it->second);
    if (it->second.readwise_uploaded != mine.readwise_uploaded ||
        it->second.readwise_id != mine.readwise_id)
      local_stats.uploads_learned++;
  }
  for (std::map<uint64_t, Annotation>::const_iterator it = by_id.begin();
       it != by_id.end(); ++it)
    merged.records.push_back(it->second);

  // Reading position.
  if (local.has_progress && remote.has_progress) {
    const bool take_remote = PreferProgress(local.progress, remote.progress);
    merged.progress = take_remote ? remote.progress : local.progress;
    merged.has_progress = true;
    local_stats.progress_changed =
        take_remote && (remote.progress.quote != local.progress.quote ||
                        remote.progress.page_hint != local.progress.page_hint);
  } else if (remote.has_progress) {
    merged.progress = remote.progress;
    merged.has_progress = true;
    local_stats.progress_changed = true;
  } else if (local.has_progress) {
    merged.progress = local.progress;
    merged.has_progress = true;
  }

  if (stats)
    *stats = local_stats;
  return merged;
}

} // namespace sync_merge
