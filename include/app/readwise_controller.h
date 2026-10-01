/*
    3dslibris - readwise_controller.h

    "Readwise" screen (AppMode::Readwise), from GENERAL settings:
      - upload highlights: sends the library's new and edited highlights
        to Readwise over HTTPS (see app/https_client.h); what was sent is
        kept with each highlight (and shared by sync)
      - upload on close (on by default): leaving a book with new or edited
        highlights sends that book's. Without Wi-Fi it is skipped quietly;
        a failure is shown once, not retried until the highlights change.
        "off" in readwise-on-close.txt turns it off.
      - export CSV file: every highlight, for readwise.io/import_bulk
      - Readwise token: read from readwise-token.txt in the 3dslibris
        folder (easiest to paste there from a PC), or typed here and saved
        there; checked with Readwise when entered. Y tests the connection.
    Network requests block for a few seconds; a "working" message is drawn
    first and the request runs on the next frame.
*/

#pragma once

#include <map>
#include <string>
#include <vector>

#include "book/readwise_api_utils.h"

class App;
class Book;
struct FrameInput;

class ReadwiseController {
public:
  explicit ReadwiseController(App &app);

  void Show();
  // Leaving the reader: true if this book has highlights to send now.
  bool WantsUploadOnClose(Book *book);
  // Shows "Uploading highlights..." and sends, then opens the library.
  void ShowUploadOnClose(Book *book);
  void RunFrame(const FrameInput &input);

private:
  enum Option { kUpload = 0, kOnClose, kExportCsv, kToken, kOptionCount };
  enum Pending { kNone, kDoUpload, kDoUploadOnClose, kDoCheckToken, kDoTest };

  struct Sent {
    int created;
    int updated;
    std::string failure;
    Sent() : created(0), updated(0) {}
  };

  void Draw();
  void Leave();
  void Start(Pending work, const char *message);
  // Highlights still to send (none if nothing is new or edited).
  readwise_api_utils::Work PendingWork(
      const std::vector<readwise_api_utils::Highlight> &highlights) const;
  Sent Send(const std::string &token, const readwise_api_utils::Work &work);
  void Upload();
  void UploadOnClose();
  bool UploadOnCloseEnabled() const;
  void SetUploadOnClose(bool on);
  void ExportCsv();
  void EnterToken();
  void CheckToken();
  void TestConnection();
  std::string LoadToken() const;
  bool SaveToken(const std::string &token) const;
  void SetLines(const std::string &a, const std::string &b = std::string(),
                const std::string &c = std::string());

  App &app_;
  int index_;
  bool dirty_;
  Pending pending_;
  std::string pending_token_;
  std::vector<std::string> lines_;
  bool on_close_;
  bool left_; // Leave() ran: another view owns the screen now
  Book *close_book_;
  // What was last tried per book on close this session, so a failure
  // isn't retried every time the library opens.
  std::map<std::string, std::string> attempted_;
};
