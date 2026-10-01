/*
    3dslibris - readwise_controller.h

    "Readwise" screen (AppMode::Readwise), from GENERAL settings:
      - upload highlights: sends the library's new and edited highlights
        to Readwise over HTTPS (see app/https_client.h), remembering what
        was sent in readwise-uploaded.txt
      - export CSV file: every highlight, for readwise.io/import_bulk
      - Readwise token: read from readwise-token.txt in the 3dslibris
        folder (easiest to paste there from a PC), or typed here and saved
        there; checked with Readwise when entered
      - test connection
    Network requests block for a few seconds; a "working" message is drawn
    first and the request runs on the next frame.
*/

#pragma once

#include <string>
#include <vector>

class App;
struct FrameInput;

class ReadwiseController {
public:
  explicit ReadwiseController(App &app);

  void Show();
  void RunFrame(const FrameInput &input);

private:
  enum Option { kUpload = 0, kExportCsv, kToken, kTest, kOptionCount };
  enum Pending { kNone, kDoUpload, kDoCheckToken, kDoTest };

  void Draw();
  void Leave();
  void Start(Pending work, const char *message);
  void Upload();
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
};
