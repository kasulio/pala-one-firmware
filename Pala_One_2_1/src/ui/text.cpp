#include "src/ui/text.h"

#include "src/hal/display.h"            // u8g2
#include "src/pure/bookmarks_codec.h"   // kOffsetUnset
#include "src/pure/md_style.h"          // MdStyleState, mdAdvanceStyle
#include "src/pure/paths.h"             // isMarkdownBookPath
#include "src/storage/page_cache.h"     // on-disk page-offset cache
#include "src/ui/font.h"                // Font::useBody / bodyLayout / measureBionicLine / layoutForCache

// Markdown style carry across soft-wrapped visual lines and page starts.
// measure() starts from this state (copy, no writeback); onSoftWrapCommit
// advances it; hard `\n` / paragraph gap resets it. Each paginate seeds
// from the last source newline before startPos so random-access page
// draws keep an open * / ** face.
static MdStyleState s_mdCarry;

// Measure-width adapter for the paginator. Routes through Font::measureBionicLine
// so the bionic 1-px-per-split-word adjustment is folded into the same width
// budget the paginator wraps against — without bionic this collapses to a
// plain getUTF8Width under the Body face. Seeds from s_mdCarry so a soft-
// wrapped continuation token is measured in the open italic/bold face.
static int bodyMeasure(const char* s) {
  MdStyleState st = s_mdCarry;
  return Font::measureBionicLine(s, &st);
}

static void mdSoftWrapCommit(const char* buf, size_t /*len*/) {
  mdAdvanceStyle(buf, &s_mdCarry);
}

static void mdStyleReset() {
  s_mdCarry = MdStyleState{};
}

// Restore incoming face at `startPos`. Hard `\n` already resets style, so
// only the current source line (last `\n`..startPos) can hold an open span.
// Chunked read; a trailing `*` is deferred so `**` is never split.
static void seedMdCarry(File& f, uint32_t startPos) {
  s_mdCarry = MdStyleState{};
  if (startPos == 0 || !Font::markdownEnabled()) return;

  constexpr size_t kChunk = 256;
  uint8_t buf[kChunk];
  uint32_t lineStart = 0;
  uint32_t pos = startPos;
  while (pos > 0) {
    uint32_t chunkStart = (pos > kChunk) ? pos - kChunk : 0;
    size_t n = (size_t)(pos - chunkStart);
    if (!f.seek(chunkStart)) break;
    int got = f.read(buf, n);
    if (got <= 0) break;
    bool found = false;
    for (int i = got; i-- > 0; ) {
      if (buf[i] == '\n') {
        lineStart = chunkStart + (uint32_t)i + 1;
        found = true;
        break;
      }
    }
    if (found) break;
    if (chunkStart == 0) {
      lineStart = 0;
      break;
    }
    pos = chunkStart;
  }

  uint32_t p = lineStart;
  while (p < startPos) {
    size_t n = (size_t)(startPos - p);
    if (n > kChunk) n = kChunk;
    if (!f.seek(p)) break;
    int got = f.read(buf, n);
    if (got <= 0) break;
    size_t use = (size_t)got;
    if (p + (uint32_t)got < startPos && got > 1 && buf[got - 1] == '*') {
      use = got - 1;
    }
    if (use > 0) {
      mdAdvanceStyle(reinterpret_cast<const char*>(buf), use, &s_mdCarry);
    }
    if (use == 0) break;
    p += (uint32_t)use;
  }
}

// ============================================================================
//  Page primitives — one page at `startPos`, three flavors.
//
//  All set the body font before calling the paginator so the measure
//  function and the metrics describe the same face. Each returns the byte
//  offset where the next page begins.
// ============================================================================
static uint32_t paginateBody(File& f, uint32_t startPos, const LineCallback& onLine) {
  seedMdCarry(f, startPos);
  BufferedFileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();
  if (Font::markdownEnabled()) {
    return paginatePage(stream, startPos, m, bodyMeasure, onLine,
                        mdSoftWrapCommit, mdStyleReset);
  }
  return paginatePage(stream, startPos, m, bodyMeasure, onLine);
}

uint32_t drawPageAt(File& f, uint32_t startPos) {
  const LayoutMetrics& m = Font::bodyLayout();

  int cursorY = TOP_PAD + m.ascent;
  auto onLine = [&](const char* buf, size_t len) {
    // len 0 indicates a paragraph gap. Advance by gap height.
    if (len == 0) { cursorY += m.paragraphGapH; return; }
    // Draw from carried style; soft-wrap commit (below) advances s_mdCarry
    // after this returns so the next line keeps an open * / ** face.
    MdStyleState st = s_mdCarry;
    Font::drawBionicLine(MARGIN_X, cursorY, buf, &st);
    cursorY += m.lineH;
  };

  return paginateBody(f, startPos, onLine);
}

uint32_t extractPageText(File& f, uint32_t startPos, String& out) {
  auto onLine = [&](const char* buf, size_t len) {
    // len 0 indicates paragraph break.
    if (len == 0) { out.concat('\n'); return; }
    // Trim leading whitespace (paginator already trims trailing).
    const char* start = buf;
    size_t remaining = len;
    while (remaining > 0 && (*start == ' ' || *start == '\t')) { start++; remaining--; }
    out.concat(start, remaining);
    out.concat('\n');
  };

  return paginateBody(f, startPos, onLine);
}

uint32_t nextPageOffset(File& f, uint32_t startPos) {
  return paginateBody(f, startPos, nullptr);
}

// ============================================================================
//  Cross-book offset lookup
// ============================================================================
uint32_t pageOffsetForPage(File& f, const String& path, int page) {
  if (page < 0) page = 0;

  // Match measure/draw to this book's format (.md vs .txt). Restore after —
  // web lookups can run while another book is open on-device.
  const bool prevMd = Font::markdownEnabled();
  Font::setMarkdownEnabled(isMarkdownBookPath(path));

  // On-disk fast path: O(1) seek to the highest cached page <= target.
  // The active reader keeps this file fresh for books it visits; cross-book
  // lookups (web bookmark resolve, page-text export) ride along.
  uint32_t off = 0;
  int startPage = loadOffsetForPageFromDisk(path, f.size(),
                                            Font::layoutForCache(),
                                            page, &off);
  if (startPage < 0) startPage = 0;

  for (int p = startPage; p < page; p++) {
    uint32_t next = nextPageOffset(f, off);
    if (next == off) break;
    off = next;
  }

  Font::setMarkdownEnabled(prevMd);
  return off;
}

uint32_t resolveBookmarkOffset(const String& path, uint16_t page, uint32_t storedOffset) {
  File f = FS.open(path, "r");
  if (!f) return 0;

  size_t size = f.size();
  if (storedOffset != kOffsetUnset && storedOffset < size) {
    f.close();
    return storedOffset;
  }

  uint32_t off = pageOffsetForPage(f, path, page);
  f.close();
  return off;
}
