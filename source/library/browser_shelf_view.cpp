#include "library/browser_shelf_view.h"

#include <string>

#include "book/book.h"
#include "library/browser_presentation_hit_utils.h"
#include "library/browser_presentation_utils.h"
#include "library/library_draw.h"
#include "library/library_paint_utils.h"
#include "library/library_theme_utils.h"
#include "ui/text.h"

namespace browser_shelf_view {

int HitTestBookIndex(int x, int y, int page_start, int book_count) {
  return browser_presentation_hit_utils::HitTestGridBookIndex(
      x, y, page_start, book_count, kX0, kY0, kCellW, kCellH, kCols, kRows,
      kPageCapacity);
}

void DrawPage(const BrowserDrawContext &ctx, int page_start) {
  Text *ts = ctx.ts;
  const library_theme_utils::LibraryPalette pal =
      library_theme_utils::ForColorMode(ts->GetColorMode());
  const library_paint_utils::Surface surface =
      library_draw::SurfaceFor(ts, ts->screenright);
  const int book_count = (int)ctx.books->size();

  // The planks run the full width, whether or not a row has books.
  for (int row = 0; row < kRows; row++) {
    const int plank_y = kY0 + row * kCellH + kPlankOffsetY;
    library_paint_utils::FillRect(surface, 2, plank_y, surface.width - 2,
                                  plank_y + kPlankH, pal.shelf);
    library_paint_utils::FillRect(surface, 2, plank_y + kPlankH,
                                  surface.width - 2, plank_y + kPlankH + 2,
                                  pal.shelf_edge);
    ts->MarkScreenDirtyRect(ts->screenright, 2, plank_y, surface.width - 2,
                            plank_y + kPlankH + 2);
  }

  for (int i = page_start; i < book_count && i < page_start + kPageCapacity;
       i++) {
    Book *book = (*ctx.books)[i];
    const int slot = i - page_start;
    const int cell_x = kX0 + (slot % kCols) * kCellW;
    const int cell_y = kY0 + (slot / kCols) * kCellH;
    const int box_x = cell_x + kCoverOffsetX;
    const bool has_cover = book->coverPixels != NULL &&
                           book->coverWidth > 0 && book->coverHeight > 0;

    int img_w = kCoverW;
    int img_h = kCoverH;
    if (has_cover)
      library_paint_utils::FitSize(book->coverWidth, book->coverHeight,
                                   kCoverW, kCoverH, true, &img_w, &img_h);
    // Standing on the plank, centered in the cell.
    const int img_x = box_x + (kCoverW - img_w) / 2;
    const int img_y = cell_y + kCoverH - img_h;

    library_paint_utils::FillRect(surface, img_x + 2, img_y + 2,
                                  img_x + img_w + 2, img_y + img_h,
                                  pal.shadow);
    if (has_cover) {
      library_paint_utils::BlitScaled(surface, book->coverPixels,
                                      book->coverWidth, book->coverHeight,
                                      img_x, img_y, img_w, img_h);
    } else {
      library_paint_utils::FillRect(surface, img_x, img_y, img_x + img_w,
                                    img_y + img_h, pal.placeholder);
      // A spine line, so it reads as a book.
      library_paint_utils::FillRect(surface, img_x + 4, img_y, img_x + 5,
                                    img_y + img_h, pal.track);
    }
    if (book == ctx.selected_book)
      library_paint_utils::FrameRect(surface, img_x - 3, img_y - 3,
                                     img_x + img_w + 3, img_y + img_h + 2, 2,
                                     pal.accent);
    ts->MarkScreenDirtyRect(ts->screenright, img_x - 3, img_y - 3,
                            img_x + img_w + 3, img_y + img_h + 2);

    if (!has_cover) {
      ts->SetPixelSize(9);
      const std::string name =
          browser_presentation_utils::BuildBrowserDisplayName(book);
      browser_presentation_utils::DrawWrappedTitleInsideCover(
          ts, name, img_x + 2, img_y, img_w - 2, img_h, TEXT_STYLE_BROWSER);
    }

    if (!book->IsBrowserFolder()) {
      const library_progress_utils::BookProgress progress =
          library_draw::ProgressFor(book);
      // Progress runs along the plank under the book.
      library_draw::DrawProgressBar(ts, ts->screenright, img_x,
                                    cell_y + kPlankOffsetY + 1, img_w, 3,
                                    progress.percent, pal);
      library_draw::DrawStatusBadge(ts, ts->screenright, img_x, img_y,
                                    img_x + img_w, progress, pal);
    }
  }
}

} // namespace browser_shelf_view
