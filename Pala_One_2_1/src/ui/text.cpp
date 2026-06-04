#include "src/ui/text.h"

#include <string.h>

#include "src/hal/display.h"            // u8g2
#include "src/pure/bookmarks_codec.h"   // kOffsetUnset
#include "src/pure/stream.h"
#include "src/storage/page_cache.h"     // on-disk page-offset cache
#include "src/ui/font.h"                // Font::useBody / bodyLayout / measureBionicLine / layoutForCache

// Measure-width adapter for the paginator. Routes through Font::measureBionicLine
// so the bionic 1-px-per-split-word adjustment is folded into the same width
// budget the paginator wraps against — without bionic this collapses to a
// plain getUTF8Width under the Body face.
static int bodyMeasure(const char* s) {
  return Font::measureBionicLine(s);
}

// ============================================================================
//  Page primitives — one page at `startPos`, three flavors.
//
//  All set the body font before calling the paginator so the measure
//  function and the metrics describe the same face. Each returns the byte
//  offset where the next page begins.
// ============================================================================
uint32_t drawPageAt(File& f, uint32_t startPos) {
  FileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();

  int cursorY = TOP_PAD + m.ascent;
  auto onLine = [&](const char* buf, size_t /*len*/, uint32_t /*srcStart*/) {
    // drawBionicLine sets the active u8g2 font back to Body before returning,
    // so the next line measurement (via bodyMeasure) is consistent.
    Font::drawBionicLine(MARGIN_X, cursorY, buf);
    cursorY += m.lineH;
  };

  return paginatePage(stream, startPos, m, bodyMeasure, onLine);
}

uint32_t extractPageText(File& f, uint32_t startPos, String& out) {
  FileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();

  auto onLine = [&](const char* buf, size_t len, uint32_t /*srcStart*/) {
    // Trim leading whitespace (paginator already trims trailing).
    const char* start = buf;
    size_t remaining = len;
    while (remaining > 0 && (*start == ' ' || *start == '\t')) { start++; remaining--; }
    out.concat(start, remaining);
    out.concat('\n');
  };

  return paginatePage(stream, startPos, m, bodyMeasure, onLine);
}

uint32_t nextPageOffset(File& f, uint32_t startPos) {
  FileReadStream stream(f);
  const LayoutMetrics& m = Font::bodyLayout();
  Font::useBody();
  return paginatePage(stream, startPos, m, bodyMeasure, nullptr);
}

// ============================================================================
//  Cross-book offset lookup
// ============================================================================
uint32_t pageOffsetForPage(File& f, const String& path, int page) {
  if (page < 0) page = 0;

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

int collectWrappedLines(const String& text, int maxWidthPx, String* out, int cap) {
  if (!out || cap <= 0)
    return 0;
  StringReadStream stream(text);
  LayoutMetrics m = Font::bodyLayout();
  m.maxWidth = maxWidthPx;
  m.maxLines = cap;
  Font::useBody();
  int count = 0;
  paginatePage(stream, 0, m, bodyMeasure, [&](const char* buf, size_t len, uint32_t /*srcStart*/) {
    if (count < cap)
      out[count++] = String(buf, (unsigned)len);
  });
  return count;
}

namespace
{

LayoutMetrics notesEditLayout(int maxWidthPx, int maxLines)
{
  LayoutMetrics m = Font::bodyLayout();
  m.maxWidth = maxWidthPx;
  m.maxLines = maxLines;
  m.trimTrailingSpaces = false;
  m.trimLeadingSpaces = false;
  return m;
}

int measureTextPrefix(const String& text, size_t start, size_t end)
{
  if (start >= end || start >= text.length())
    return 0;
  if (end > text.length())
    end = text.length();
  const size_t n = end - start;
  if (n == 0)
    return 0;
  if (n < 192)
  {
    char buf[192];
    memcpy(buf, text.c_str() + start, n);
    buf[n] = '\0';
    return bodyMeasure(buf);
  }
  return bodyMeasure(text.substring(start, end).c_str());
}

int measureCharAt(const String& text, size_t pos)
{
  const int clen = utf8SafeCharLenAt(text, (int)pos);
  if (clen <= 0)
    return bodyMeasure("?");
  return measureTextPrefix(text, pos, pos + (size_t)clen);
}

size_t byteOffsetAtColPx(const String& text, size_t lineStart, size_t lineEnd, int colPx)
{
  if (lineStart >= lineEnd || lineStart >= text.length())
    return lineStart;
  if (colPx <= 0)
    return lineStart;

  size_t pos = lineStart;
  int width = 0;
  while (pos < lineEnd && pos < text.length())
  {
    int clen = utf8SafeCharLenAt(text, (int)pos);
    if (clen <= 0)
      clen = 1;
    const size_t next = pos + (size_t)clen;
    if (next > lineEnd)
      break;
    const int cw = measureCharAt(text, pos);
    if (width + cw > colPx && width > 0)
      break;
    width += cw;
    pos = next;
    if (width >= colPx)
      break;
  }
  return pos;
}

} // namespace

int notesBuildWrapped(const String& text, int maxWidthPx, String* lineOut,
                      uint32_t* lineStartOut, int cap)
{
  if (cap <= 0)
    return 0;
  StringReadStream stream(text);
  LayoutMetrics m = notesEditLayout(maxWidthPx, cap);
  Font::useBody();
  int count = 0;
  paginatePage(stream, 0, m, bodyMeasure, [&](const char* buf, size_t len, uint32_t srcStart) {
    if (count >= cap)
      return;
    if (lineStartOut)
      lineStartOut[count] = srcStart;
    if (lineOut)
      lineOut[count] = String(buf, (unsigned)len);
    count++;
  });
  return count;
}

int notesBuildLineStarts(const String& text, int maxWidthPx, uint32_t* starts, int cap)
{
  return notesBuildWrapped(text, maxWidthPx, nullptr, starts, cap);
}

bool notesMapCaret(const String& text, int maxWidthPx, size_t caret,
                   const uint32_t* starts, int lineCount, int* outLine, int* outColPx)
{
  if (!outLine || !outColPx || lineCount <= 0 || !starts)
    return false;

  const size_t len = text.length();
  if (caret > len)
    caret = len;

  int line = 0;
  for (int i = 0; i < lineCount; i++)
  {
    const size_t start = starts[i];
    const size_t end = (i + 1 < lineCount) ? starts[i + 1] : len;
    if (caret < end || (caret == len && i == lineCount - 1))
    {
      line = i;
      break;
    }
    if (i == lineCount - 1)
      line = i;
  }

  const size_t lineStart = starts[line];
  const size_t lineEnd = (line + 1 < lineCount) ? starts[line + 1] : len;
  const size_t prefixEnd = (caret > lineStart && caret <= lineEnd) ? caret : lineEnd;
  *outLine = line;
  *outColPx = measureTextPrefix(text, lineStart, prefixEnd);
  return true;
}

size_t notesMoveCaretVertical(const String& text, int maxWidthPx, size_t caret, int deltaLine)
{
  if (deltaLine == 0)
    return caret;

  static constexpr int kMaxLines = 256;
  static uint32_t s_starts[kMaxLines];
  const int lineCount = notesBuildWrapped(text, maxWidthPx, nullptr, s_starts, kMaxLines);
  if (lineCount <= 0)
    return caret;

  int line = 0;
  int colPx = 0;
  if (!notesMapCaret(text, maxWidthPx, caret, s_starts, lineCount, &line, &colPx))
    return caret;

  line += deltaLine;
  if (line < 0)
    line = 0;
  if (line >= lineCount)
    line = lineCount - 1;

  const size_t lineStart = s_starts[line];
  const size_t lineEnd = (line + 1 < lineCount) ? s_starts[line + 1] : text.length();
  return byteOffsetAtColPx(text, lineStart, lineEnd, colPx);
}

int collectWrappedLinesForEdit(const String& text, int maxWidthPx, String* out, int cap)
{
  return notesBuildWrapped(text, maxWidthPx, out, nullptr, cap);
}
