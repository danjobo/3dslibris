#include "sync/sync_manifest.h"
#include "sync/sync_merge.h"

#include "book/annotation_store_utils.h"
#include "test_assert.h"

#include <stdint.h>
#include <string>

namespace {

const uint32_t kA = 0xAAAA0001u;
const uint32_t kB = 0xBBBB0002u;

Annotation Highlight(uint32_t console, uint32_t n, uint32_t modified,
                     const char *quote, const char *note = "") {
  Annotation a;
  a.id = ((uint64_t)console << 32) | n;
  a.kind = Annotation::kHighlight;
  a.created = 1000;
  a.modified = modified;
  a.quote = quote;
  a.note = note;
  return a;
}

Annotation *FindRecord(BookState *s, uint64_t id) {
  for (size_t i = 0; i < s->records.size(); i++)
    if (s->records[i].id == id)
      return &s->records[i];
  return NULL;
}

std::string Text(const BookState &s) {
  return annotation_store_utils::Serialize(s);
}

void ExpectConverges(const char *label, const BookState &a,
                     const BookState &b) {
  const BookState ab = sync_merge::Merge(a, b, NULL);
  const BookState ba = sync_merge::Merge(b, a, NULL);
  test::ExpectStrEq(label, Text(ab).c_str(), Text(ba).c_str());
  // Merging again changes nothing.
  sync_merge::MergeStats stats;
  const BookState again = sync_merge::Merge(ab, b, &stats);
  test::ExpectStrEq(label, Text(again).c_str(), Text(ab).c_str());
  test::ExpectFalse(label, stats.Changed());
}

void TestDivergedConsolesConverge() {
  BookState base;
  base.records.push_back(Highlight(kA, 1, 10, "shared quote"));

  BookState a = base;
  FindRecord(&a, ((uint64_t)kA << 32) | 1)->note = "edited on A";
  FindRecord(&a, ((uint64_t)kA << 32) | 1)->modified = 20;
  a.records.push_back(Highlight(kA, 2, 21, "only on A"));

  BookState b = base;
  Annotation *deleted_on_b = FindRecord(&b, ((uint64_t)kA << 32) | 1);
  deleted_on_b->deleted = true; // older than A's edit
  deleted_on_b->modified = 15;
  b.records.push_back(Highlight(kB, 1, 22, "only on B"));

  sync_merge::MergeStats stats;
  BookState merged = sync_merge::Merge(a, b, &stats);
  test::ExpectEq("three records", (int)merged.records.size(), 3);
  Annotation *shared = FindRecord(&merged, ((uint64_t)kA << 32) | 1);
  test::ExpectTrue("newer edit beats older delete",
                   shared && !shared->deleted &&
                       shared->note == "edited on A");
  test::ExpectEq("one added from B", stats.records_added, 1);
  test::ExpectEq("nothing of A's updated", stats.records_updated, 0);
  ExpectConverges("diverged converge", a, b);
}

void TestNewerDeletionWins() {
  BookState a;
  a.records.push_back(Highlight(kA, 1, 10, "q", "old note"));
  BookState b = a;
  b.records[0].deleted = true;
  b.records[0].modified = 30;
  sync_merge::MergeStats stats;
  BookState merged = sync_merge::Merge(a, b, &stats);
  test::ExpectTrue("deleted", merged.records[0].deleted);
  test::ExpectEq("counted as update", stats.records_updated, 1);
  ExpectConverges("delete converge", a, b);
}

void TestDeletionWinsTie() {
  BookState a;
  a.records.push_back(Highlight(kA, 1, 50, "q", "note"));
  BookState b = a;
  b.records[0].deleted = true; // same modified time
  test::ExpectTrue("tie a,b", sync_merge::Merge(a, b, NULL).records[0].deleted);
  test::ExpectTrue("tie b,a", sync_merge::Merge(b, a, NULL).records[0].deleted);
}

void TestProgressMostRecentWins() {
  BookState a, b;
  a.has_progress = true;
  a.progress.last_read = 100;
  a.progress.page_hint = 10;
  a.progress.quote = "page ten";
  b.has_progress = true;
  b.progress.last_read = 200;
  b.progress.page_hint = 5; // went back to re-read, more recently
  b.progress.quote = "page five";
  sync_merge::MergeStats stats;
  BookState merged = sync_merge::Merge(a, b, &stats);
  test::ExpectStrEq("most recent position", merged.progress.quote.c_str(),
                    "page five");
  test::ExpectTrue("progress change reported", stats.progress_changed);
  merged = sync_merge::Merge(b, a, &stats);
  test::ExpectStrEq("other direction", merged.progress.quote.c_str(),
                    "page five");
  test::ExpectFalse("no change for the newer side", stats.progress_changed);

  BookState none;
  merged = sync_merge::Merge(none, a, &stats);
  test::ExpectTrue("adopted when missing", merged.has_progress);
  test::ExpectTrue("adoption reported", stats.progress_changed);
}

// Tiny deterministic PRNG for the convergence fuzz test.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t Next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t Below(uint32_t n) { return Next() % n; }
};

void RandomEdits(Rng *rng, BookState *s, uint32_t console, uint32_t *clock,
                 int edits) {
  static const char *kWords[] = {"alpha", "beta", "gamma", "delta"};
  for (int i = 0; i < edits; i++) {
    (*clock)++;
    const uint32_t op = rng->Below(3);
    if (op == 0 || s->records.empty()) {
      Annotation a = Highlight(console, (uint32_t)s->records.size() + 100 + i,
                               *clock, kWords[rng->Below(4)]);
      if (rng->Below(2))
        a.kind = Annotation::kBookmark;
      s->records.push_back(a);
    } else {
      Annotation &a = s->records[rng->Below((uint32_t)s->records.size())];
      a.modified = *clock;
      if (op == 1)
        a.note = kWords[rng->Below(4)];
      else
        a.deleted = true;
    }
    if (rng->Below(4) == 0) {
      s->has_progress = true;
      s->progress.last_read = *clock;
      s->progress.page_hint = (uint16_t)rng->Below(300);
      s->progress.quote = kWords[rng->Below(4)];
    }
  }
}

void TestRandomEditsConverge() {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    Rng rng(seed);
    BookState base;
    uint32_t clock = 100;
    RandomEdits(&rng, &base, kA, &clock, 5);
    BookState a = base, b = base;
    uint32_t clock_a = clock, clock_b = clock + (seed % 3);
    RandomEdits(&rng, &a, kA, &clock_a, 8);
    RandomEdits(&rng, &b, kB, &clock_b, 8);
    ExpectConverges("random converge", a, b);
  }
}

void TestManifestRoundTrip() {
  sync_manifest::Manifest m;
  sync_manifest::BookEntry one;
  one.file_name = "The\tForever War.epub";
  one.file_size = 1234567;
  one.state.records.push_back(Highlight(kA, 1, 10, "quote", "note\nline"));
  one.state.has_progress = true;
  one.state.progress.last_read = 99;
  one.state.progress.quote = "start";
  m.books.push_back(one);
  sync_manifest::BookEntry two;
  two.file_name = "manga.cbz";
  two.file_size = 50000000000ull;
  m.books.push_back(two);

  sync_manifest::Manifest out;
  test::ExpectTrue("parsed", sync_manifest::Parse(sync_manifest::Serialize(m),
                                                  &out));
  test::ExpectEq("two books", (int)out.books.size(), 2);
  test::ExpectStrEq("name with tab", out.books[0].file_name.c_str(),
                    "The\tForever War.epub");
  test::ExpectTrue("large size", out.books[1].file_size == 50000000000ull);
  test::ExpectStrEq("state survives", Text(out.books[0].state).c_str(),
                    Text(one.state).c_str());
  test::ExpectTrue("find by sync id",
                   out.Find(sync_merge::MakeSyncBookId("manga.cbz",
                                                       50000000000ull)) !=
                       NULL);
  test::ExpectTrue("size is part of the id",
                   out.Find(sync_merge::MakeSyncBookId("manga.cbz", 1)) ==
                       NULL);

  test::ExpectFalse("bad header", sync_manifest::Parse("nope\n", &out));
  test::ExpectTrue("malformed book skipped",
                   sync_manifest::Parse("3DSLIBRIS-SYNC 1\n"
                                        "BOOK\tbroken\n"
                                        "ENDBOOK\n",
                                        &out));
  test::ExpectEq("nothing parsed", (int)out.books.size(), 0);
}

} // namespace

void TestUploadStateIsShared() {
  // A uploaded the highlight; B has the same version but doesn't know.
  Annotation a = Highlight(0xA, 1, 100, "quote", "note");
  a.readwise_uploaded = 100;
  a.readwise_id = 555;
  Annotation b = a;
  b.readwise_uploaded = 0;
  b.readwise_id = 0;
  BookState sa, sb;
  sa.records.push_back(a);
  sb.records.push_back(b);
  sync_merge::MergeStats stats_b;
  const BookState on_b = sync_merge::Merge(sb, sa, &stats_b);
  test::ExpectEq("b learns the upload", (int)on_b.records[0].readwise_uploaded,
                 100);
  test::ExpectTrue("b learns the readwise id", on_b.records[0].readwise_id == 555);
  test::ExpectTrue("counts as a change", stats_b.Changed());
  test::ExpectEq("not shown as an edit", stats_b.records_updated, 0);
  sync_merge::MergeStats stats_a;
  const BookState on_a = sync_merge::Merge(sa, sb, &stats_a);
  test::ExpectFalse("a has nothing new", stats_a.Changed());
  test::ExpectEq("a keeps its upload", (int)on_a.records[0].readwise_uploaded, 100);

  // B then edits the note: the edit wins, the upload state stays known
  // (so B's next upload updates the Readwise highlight with id 555).
  Annotation edited = b;
  edited.modified = 200;
  edited.note = "new note";
  BookState sb2;
  sb2.records.push_back(edited);
  const BookState merged = sync_merge::Merge(sa, sb2, NULL);
  test::ExpectStrEq("edit wins", merged.records[0].note.c_str(), "new note");
  test::ExpectEq("upload version kept", (int)merged.records[0].readwise_uploaded,
                 100);
  test::ExpectTrue("id kept", merged.records[0].readwise_id == 555);
}

void TestColorChangeIsAnEdit() {
  Annotation a = Highlight(0xA, 1, 100, "quote", "");
  Annotation b = a;
  b.color = 3;
  b.modified = 150;
  BookState sa, sb;
  sa.records.push_back(a);
  sb.records.push_back(b);
  sync_merge::MergeStats stats;
  const BookState merged = sync_merge::Merge(sa, sb, &stats);
  test::ExpectEq("newer color wins", (int)merged.records[0].color, 3);
  test::ExpectEq("counted", stats.records_updated, 1);
}

int main() {
  TestDivergedConsolesConverge();
  TestNewerDeletionWins();
  TestDeletionWinsTie();
  TestProgressMostRecentWins();
  TestRandomEditsConverge();
  TestManifestRoundTrip();
  TestUploadStateIsShared();
  TestColorChangeIsAnEdit();
  return 0;
}
