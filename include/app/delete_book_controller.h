/*
    3dslibris - delete_book_controller.h

    "Delete book" screen (AppMode::DeleteBook), opened by holding X on a
    book in the library. Deletes the book file, its cover thumbnail and its
    saved page and bookmarks, and optionally its highlights and notes (kept
    by default, so copying the book back restores them). Page caches can't
    be told apart by book; they never match another file and go with
    "clear cache". Books built into the app (romfs) can't be deleted.
*/

#pragma once

#include <string>

class App;
class Book;
struct FrameInput;

class DeleteBookController {
public:
  explicit DeleteBookController(App &app);

  void Show(Book *book);
  void RunFrame(const FrameInput &input);

private:
  enum Option { kDeleteKeepNotes = 0, kDeleteAll, kCancel, kOptionCount };

  void Draw();
  void Delete(bool with_notes);
  void Leave(int select_index);

  App &app_;
  Book *book_;
  std::string folder_;
  std::string file_name_;
  std::string title_;
  unsigned long long size_;
  bool built_in_;
  int index_;
  std::string error_;
  bool dirty_;
};
