#include "library/browser_grid_view.h"

#include <cstring>

#include "book/book.h"
#include "shared/debug_log.h"
#include "library/browser_presentation_hit_utils.h"
#include "library/browser_presentation_utils.h"
#include "library/browser_view_utils.h"
#include "library/library_draw.h"
#include "library/library_paint_utils.h"
#include "library/library_theme_utils.h"
#include "ui/text.h"

BrowserGridMarqueeState::BrowserGridMarqueeState()
    : book(NULL), strip(NULL), bg_strip(NULL), strip_w(0), strip_h(0),
      strip_x(0), strip_y(0), blit_w(0), scroll_offset(0), scroll_timer(0),
      end_timer(0), color_mode(-1), bg_color(0xFFFF), active(false) {}

void BrowserGridMarqueeState::Reset() {
  delete[] strip;
  delete[] bg_strip;
  strip = NULL;
  bg_strip = NULL;
  book = NULL;
  display_name.clear();
  strip_w = strip_h = strip_x = strip_y = blit_w = 0;
  scroll_offset = scroll_timer = end_timer = 0;
  color_mode = -1;
  bg_color = 0xFFFF;
  active = false;
}

namespace browser_grid_view {

int HitTestBookIndex(int x, int y, int page_start, int book_count) {
  return browser_presentation_hit_utils::HitTestGridBookIndex(
      x, y, page_start, book_count, kGridX0, kGridY0, kCellW, kCellH, kGridCols,
      kGridRows, kPageCapacity);
}

void DrawPage(const BrowserDrawContext &ctx, BrowserGridMarqueeState &marquee,
              int page_start) {
  const int book_count = (int)ctx.books->size();
  const library_theme_utils::LibraryPalette pal =
      library_theme_utils::ForColorMode(ctx.ts->GetColorMode());
  for (int i = page_start;
       i < book_count && i < page_start + kPageCapacity; i++) {
    int page_idx = i % kPageCapacity;
    int col = page_idx % kGridCols;
    int row = page_idx / kGridCols;
    int btnX = kGridX0 + col * kCellW;
    int btnY = kGridY0 + row * kCellH;

    Book *book = (*ctx.books)[i];
    const bool is_selected = book == ctx.selected_book;
    const library_paint_utils::Surface surface =
        library_draw::SurfaceFor(ctx.ts, ctx.ts->screenright);
    const int box_x = btnX + kCoverOffsetX;
    const int box_y = btnY + kCoverOffsetY;
    const bool has_cover = book->coverPixels != NULL &&
                           book->coverWidth > 0 && book->coverHeight > 0;

    // The cover sits on the bottom of its box, centered, like a book on a
    // shelf. Books without one get a plain box with the title inside.
    int img_w = kCoverW;
    int img_h = kCoverH;
    if (has_cover)
      library_paint_utils::FitSize(book->coverWidth, book->coverHeight,
                                   kCoverW, kCoverH, true, &img_w, &img_h);
    const int img_x = box_x + (kCoverW - img_w) / 2;
    const int img_y = box_y + (kCoverH - img_h);

    library_paint_utils::FillRect(surface, img_x + 2, img_y + 2,
                                  img_x + img_w + 2, img_y + img_h + 2,
                                  pal.shadow);
    if (has_cover) {
      library_paint_utils::BlitScaled(surface, book->coverPixels,
                                      book->coverWidth, book->coverHeight,
                                      img_x, img_y, img_w, img_h);
    } else {
      library_paint_utils::FillRect(surface, img_x, img_y, img_x + img_w,
                                    img_y + img_h, pal.placeholder);
    }
    if (is_selected) {
      library_paint_utils::FrameRect(surface, img_x - 4, img_y - 4,
                                     img_x + img_w + 4, img_y + img_h + 4, 2,
                                     pal.accent);
    }
    ctx.ts->MarkScreenDirtyRect(ctx.ts->screenright, img_x - 4, img_y - 4,
                                img_x + img_w + 4, img_y + img_h + 4);

    if (!book->IsBrowserFolder()) {
      const library_progress_utils::BookProgress progress =
          library_draw::ProgressFor(book);
      library_draw::DrawProgressBar(ctx.ts, ctx.ts->screenright, img_x,
                                    btnY + kBarOffsetY, img_w, kBarH,
                                    progress.percent, pal);
      library_draw::DrawStatusBadge(ctx.ts, ctx.ts->screenright, img_x, img_y,
                                    img_x + img_w, progress, pal);
    }

    ctx.ts->SetStyle(TEXT_STYLE_BROWSER);

    ctx.ts->SetPixelSize(10);
    std::string display_name =
        browser_presentation_utils::BuildBrowserDisplayName((*ctx.books)[i]);
    if (has_cover) {
      if (!display_name.empty()) {
        const char *dname = display_name.c_str();
        unsigned char cur_style = (unsigned char)ctx.ts->GetStyle();
        int full_w = (int)ctx.ts->GetStringWidth(dname, cur_style);
        Book *book_i = book;
        bool overflows = full_w > kCellW;

        int saved_margin_right = ctx.ts->margin.right;
        bool saved_clip = ctx.ts->IsClipToContentEnabled();
        bool saved_wrap = ctx.ts->IsAutoWrapEnabled();

        ctx.ts->margin.right = ctx.ts->display.width - (btnX + kCellW);
        ctx.ts->SetClipToContentEnabled(true);
        ctx.ts->SetAutoWrapEnabled(false);

        if (is_selected && overflows) {
          const int glyph_top = btnY + kTitleOffsetY - ctx.ts->GetHeight();
          const int title_y = (glyph_top >= 0) ? glyph_top : 0;
          const int sh = ctx.ts->GetHeight() + 2;
          const int sw = full_w;
          const int fb_stride = ctx.ts->display.height;

          int current_color_mode = ctx.ts->GetColorMode();
          if (marquee.book != book_i || marquee.display_name != display_name ||
              marquee.color_mode != current_color_mode) {
            const int vis_w = (btnX + kCellW <= ctx.ts->display.width)
                                  ? kCellW
                                  : ctx.ts->display.width - btnX;
            const int capture_w =
                (sw < ctx.ts->display.width) ? sw : ctx.ts->display.width;

            marquee.Reset();
            marquee.book = book_i;
            marquee.display_name = display_name;
            marquee.strip_w = capture_w;
            marquee.strip_h = sh;
            marquee.strip_x = btnX;
            marquee.strip_y = title_y;
            marquee.blit_w = vis_w;
            marquee.strip = new unsigned short[capture_w * sh];
            marquee.bg_strip = new unsigned short[vis_w * sh];
#ifdef DSLIBRIS_DEBUG
            if (book_i->GetStatusReporter()) {
              DBG_LOGF(book_i->GetStatusReporter(),
                       "MARQUEE build: dname='%.20s' full_w=%d sw=%d "
                       "capture_w=%d vis_w=%d btnX=%d title_y=%d sh=%d",
                       dname, full_w, sw, capture_w, vis_w, btnX, title_y,
                       sh);
            }
#endif

            for (int r = 0; r < sh; r++) {
              const unsigned short *src =
                  ctx.ts->screenright + (title_y + r) * fb_stride + btnX;
              unsigned short *dst = marquee.bg_strip + r * vis_w;
              memcpy(dst, src, vis_w * sizeof(unsigned short));
            }

            unsigned short *saved_screen = ctx.ts->GetScreen();
            int saved_mr = ctx.ts->margin.right;
            int saved_ml = ctx.ts->margin.left;
            const int off_fb_stride = ctx.ts->display.height;
            int bg_pixels = vis_w * sh;
            if (bg_pixels > 0) {
              u32 r_acc = 0, g_acc = 0, b_acc = 0;
              for (int i = 0; i < bg_pixels; i++) {
                u16 c = marquee.bg_strip[i];
                r_acc += ((c >> 11) & 0x1F) << 3 | ((c >> 11) & 0x1F) >> 2;
                g_acc += ((c >> 5) & 0x3F) << 2 | ((c >> 5) & 0x3F) >> 4;
                b_acc += (c & 0x1F) << 3 | (c & 0x1F) >> 2;
              }
              marquee.bg_color = (u16)(((r_acc / bg_pixels) >> 3 << 11) |
                                       ((g_acc / bg_pixels) >> 2 << 5) |
                                       ((b_acc / bg_pixels) >> 3));
            }

            for (int r = 0; r < sh; r++) {
              for (int c = 0; c < capture_w; c++) {
                ctx.ts->offscreen[(title_y + r) * off_fb_stride + c] =
                    marquee.bg_color;
              }
            }

            ctx.ts->SetScreen(ctx.ts->offscreen);
            ctx.ts->margin.left = 0;
            ctx.ts->margin.right = 0;
            ctx.ts->SetPen(0, btnY + kTitleOffsetY);
            ctx.ts->PrintString(dname, cur_style);
            ctx.ts->margin.left = saved_ml;
            ctx.ts->margin.right = saved_mr;
            ctx.ts->SetScreen(saved_screen);

            for (int row_i = 0; row_i < sh; row_i++) {
              const unsigned short *src =
                  ctx.ts->offscreen + (title_y + row_i) * fb_stride;
              unsigned short *dst = marquee.strip + row_i * capture_w;
              memcpy(dst, src, capture_w * sizeof(unsigned short));
            }
            marquee.scroll_offset = 0;
            marquee.scroll_timer = 0;
            marquee.color_mode = current_color_mode;
            marquee.active = true;
          }
        } else if (is_selected && !overflows) {
          marquee.Reset();
        }

        if (!(is_selected && overflows)) {
          const int title_x = overflows ? btnX : btnX + (kCellW - full_w) / 2;
          ctx.ts->SetPen(title_x, btnY + kTitleOffsetY);
          ctx.ts->PrintString(dname, cur_style);
        }

        ctx.ts->margin.right = saved_margin_right;
        ctx.ts->SetClipToContentEnabled(saved_clip);
        ctx.ts->SetAutoWrapEnabled(saved_wrap);
      }
    } else {
      browser_presentation_utils::DrawWrappedTitleInsideCover(
          ctx.ts, display_name, img_x, img_y, img_w, img_h,
          TEXT_STYLE_BROWSER);
      // The title is on the box already; the author goes underneath.
      if (!book->IsBrowserFolder() && !book->GetAuthor().empty()) {
        const std::vector<std::string> author = library_draw::WrapLines(
            ctx.ts, book->GetAuthor(), TEXT_STYLE_BROWSER, kCellW, 1);
        if (!author.empty()) {
          ctx.ts->SetTextColorOverride(pal.muted);
          library_draw::PrintCentered(ctx.ts, author[0], TEXT_STYLE_BROWSER,
                                      btnX, btnX + kCellW,
                                      btnY + kTitleOffsetY);
          ctx.ts->ClearTextColorOverride();
        }
      }
    }
  }
}

void TickMarquee(Text *ts, BrowserGridMarqueeState &marquee) {
  if (!marquee.active || !marquee.strip || !marquee.bg_strip)
    return;

  const int kPauseFrames = 60;
  const int scroll_max = marquee.strip_w - marquee.blit_w;

  if (marquee.scroll_timer < kPauseFrames) {
    marquee.scroll_timer++;
  } else if (marquee.scroll_offset >= scroll_max) {
    if (marquee.end_timer < kPauseFrames) {
      marquee.end_timer++;
    } else {
      marquee.scroll_offset = 0;
      marquee.scroll_timer = 0;
      marquee.end_timer = 0;
    }
  } else {
    marquee.scroll_offset++;
  }

  const int fb_stride = ts->display.height;
  const int tx = marquee.strip_x;
  const int ty = marquee.strip_y;
  const int sh = marquee.strip_h;
  const int sw = marquee.strip_w;
  const int off = marquee.scroll_offset;
  const int bw = marquee.blit_w;

  int blit_w = bw;
  if (blit_w + off > sw)
    blit_w = sw - off;
  if (blit_w <= 0)
    return;

  for (int row = 0; row < sh; row++) {
    if (ty + row >= ts->display.height)
      break;
    unsigned short *dst = ts->screenright + (ty + row) * fb_stride + tx;
    memcpy(dst, marquee.bg_strip + row * bw, bw * sizeof(unsigned short));
    const unsigned short *src = marquee.strip + row * sw + off;
    for (int col = 0; col < blit_w; col++) {
      if (src[col] != marquee.bg_color)
        dst[col] = src[col];
    }
  }

  ts->MarkScreenDirtyRect(ts->screenright, tx, ty, tx + bw, ty + sh);
}

} // namespace browser_grid_view
