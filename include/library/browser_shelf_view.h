/*
    3dslibris - browser_shelf_view.h

    The library's bookshelf view: three shelves of three covers on the
    bottom screen, each book standing on a plank with its reading progress
    under it. Titles are left to the top screen's details panel.
*/

#pragma once

#include "library/browser_draw_context.h"

namespace browser_shelf_view {

static const int kCols = 3;
static const int kRows = 3;
static const int kPageCapacity = kCols * kRows;
static const int kCellW = 78;
static const int kCellH = 96;
static const int kX0 = (240 - kCols * kCellW) / 2;
static const int kY0 = 4;
// Cover box inside a cell, standing on the plank below it.
static const int kCoverW = 64;
static const int kCoverH = 86;
static const int kCoverOffsetX = (kCellW - kCoverW) / 2;
static const int kPlankOffsetY = kCoverH + 2;
static const int kPlankH = 5;

int HitTestBookIndex(int x, int y, int page_start, int book_count);
void DrawPage(const BrowserDrawContext &ctx, int page_start);

} // namespace browser_shelf_view
