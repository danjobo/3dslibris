#pragma once

namespace cover_layout {

// Grid thumbnail (and its cache file).
static const int kBrowserCoverThumbWidth = 85;
static const int kBrowserCoverThumbHeight = 115;

// Extractors decode covers this large: the library's top-screen cover. The
// cache keeps it as a separate file and shrinks a copy to the thumbnail.
static const int kCoverExtractWidth = 168;
static const int kCoverExtractHeight = 232;

} // namespace cover_layout
