/*
    3dslibris - hardcover_controller.h

    Hardcover progress tracking (AppMode::Hardcover).
      - Book info > X opens it for the current book: find the book on
        Hardcover (ISBN from the file name, then title and author) and pick
        the right one from a list; send progress now; unlink; token.
      - Leaving a linked book whose page changed since the last update
        shows "Updating Hardcover..." and sends it (book marked Reading, or
        Read on the last page). Without Wi-Fi it is skipped quietly; a
        failure is shown once, not retried until the page changes again.
    Links live in hardcover-links.txt and the token in hardcover-token.txt
    (paste it there from hardcover.app > Settings > API), both in the
    3dslibris folder. Requests block for a few seconds; a message is drawn
    first and the work runs on the next frame.
*/

#pragma once

#include <map>
#include <string>
#include <vector>

#include "app/hardcover_client.h"
#include "book/hardcover_utils.h"

class App;
class Book;
struct FrameInput;

class HardcoverController {
public:
  explicit HardcoverController(App &app);

  // From book info: the screen for this book.
  void Show(Book *book);
  // Leaving the reader: true if this book's progress should be sent now.
  bool WantsSendOnClose(Book *book);
  // Shows "Updating Hardcover..." and sends, then opens the library.
  void ShowSendOnClose(Book *book);
  void RunFrame(const FrameInput &input);

private:
  enum Screen { kMenu, kResults, kMessage };
  enum Work { kWorkNone, kWorkFind, kWorkSend, kWorkSendOnClose };
  enum Option { kFind = 0, kSendNow, kUnlink, kToken, kOptionCount };

  bool LoadBookInfo(Book *book);
  void Start(Work work, const std::string &message);
  void DoFind();
  void DoSend(bool on_close);
  void ChooseResult(int index);
  void EnterToken();
  void Finish(); // leaves the screen (book info, or the library on close)
  void Draw();
  void SetLines(const std::string &a, const std::string &b = std::string(),
                const std::string &c = std::string());
  std::string LoadToken() const;
  std::vector<hardcover_utils::Link> LoadLinks() const;
  bool SaveLinks(const std::vector<hardcover_utils::Link> &links) const;
  int CurrentProgressPage(const hardcover_utils::Link &link,
                          bool *finished) const;

  App &app_;
  Book *book_;
  std::string sync_id_;
  std::string title_;
  std::string author_;
  std::string file_name_;
  Screen screen_;
  Work work_;
  bool on_close_;
  int index_;
  int results_top_;
  bool dirty_;
  bool left_; // Finish() ran: another view owns the screen now
  std::vector<std::string> lines_;
  std::vector<hardcover_client::Candidate> results_;
  // Page last attempted per book this session, so a failure isn't retried
  // every time the library opens.
  std::map<std::string, int> attempted_;
};
