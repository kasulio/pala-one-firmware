#include "paginator.h"

#include <string.h>

#include "text_util.h"

// Fixed scratch buffers for the paginator's working state. Sized for
// "any line that could possibly fit on the display" + "any reasonable
// word/URL length" with margin. Stays on the call stack — no heap.
static constexpr size_t kLineMax   = 256;
static constexpr size_t kTokenMax  = 512;
static constexpr size_t kScratchMax = kLineMax + kTokenMax + 1;

uint32_t paginatePage(IReadStream& in,
                      uint32_t startPos,
                      const LayoutMetrics& m,
                      const MeasureFn& measure,
                      const LineCallback& onLine,
                      const SoftWrapCommitFn& onSoftWrapCommit,
                      const StyleResetFn& onStyleReset) {
  in.seek(startPos);

  // Vertical layout is tracked in PIXELS (usedH), not whole-line counts, so a
  // blank line between paragraphs can consume a fractional gap (e.g. a
  // half-height paragraph break) instead of a full empty line. budgetH is the
  // usable text height; a full text line costs m.lineH, a paragraph gap costs
  // gapH. The page-fill test (pageFull) is identical in the draw and measure
  // passes (both advance usedH the same way), so page offsets stay
  // deterministic for back-nav and the on-disk offset cache.
  const int budgetH = m.maxLines * m.lineH;
  const int gapH    = (m.paragraphGapH > 0) ? m.paragraphGapH : m.lineH;
  int usedH = 0;
  // This holds the content that has been incorporated into the current line.
  char line[kLineMax];
  // Indicates how many characters of line are part of the current line.
  size_t lineLen = 0;
  // Current calculated width of everything already added to the current line.
  // Unused when onSoftWrapCommit is set (full-line measure).
  int lineW = 0;
  // Tail = bytes after the last whitespace. Tail-only measure is valid when
  // a prefix cannot change later-token widths (plain text). Markdown passes
  // onSoftWrapCommit and measures the full candidate line instead.
  size_t tailStart = 0;
  int tailW = 0;
  int spaceW = -1;
  // This holds the accumulated token that should next be added to the current line.
  char token[kTokenMax] = {}; size_t tokLen  = 0;
  // Scratch for tail+token (plain) or full candidate line (markdown).
  char scratch[kScratchMax];

  uint32_t lineStartPos  = startPos;
  uint32_t tokenStartPos = startPos;

  auto trimTrailing = [](const char* buf, size_t& len) {
    while (len > 0 && buf[len - 1] == ' ') len--;
  };

  auto trimLeading = [](char* buf, size_t& len) {
    size_t i = 0;
    while (i < len && buf[i] == ' ') i++;
    if (i > 0) {
      memmove(buf, buf + i, len - i);
      len -= i;
    }
  };

  auto emit = [&](const char* buf, size_t len) {
    usedH += m.lineH;
    if (onLine) onLine(buf, len);
  };

  // True once there's no room left for another full text line calculated
  // according to pixel budgets.
  auto pageFull = [&]() -> bool { return usedH + m.lineH > budgetH; };

  // softWrap=true: width/token split — commit MD style for the next visual
  // line. softWrap=false: source newline / end flush — reset MD style.
  auto flushLine = [&](bool softWrap) {
    trimTrailing(line, lineLen);
    line[lineLen] = 0;
    emit(line, lineLen);
    if (softWrap) {
      if (onSoftWrapCommit) onSoftWrapCommit(line, lineLen);
    } else if (onStyleReset) {
      onStyleReset();
    }
    lineLen = 0;
    lineW = 0;
    tailStart = 0;
    tailW = 0;
  };

  auto safeReturn = [&](uint32_t off) -> uint32_t {
    if (off <= startPos) off = startPos + 1;
    size_t sz = in.size();
    if (sz > 0 && off > sz) off = (uint32_t)sz;
    return off;
  };

  auto lineEndsWithSpace = [&]() -> bool {
    return lineLen > 0 && line[lineLen - 1] == ' ';
  };

  // Hard-break the token buffer: emit prefix-by-prefix lines, each as wide
  // as fits at the current font, until the token is consumed. Updates
  // `tokenStartPos` as bytes leave so safeReturn can compute the correct
  // resume offset on early-out.
  auto hardBreakToken = [&]() -> uint32_t {
    while (tokLen > 0) {
      size_t fitLen = 0;
      while (fitLen < tokLen) {
        int clen = utf8SafeCharLenAt(token, tokLen, fitLen);
        if (clen <= 0) break;
        if (fitLen + (size_t)clen > tokLen) break;
        char saved = token[fitLen + clen];
        token[fitLen + clen] = 0;
        bool fits = measure(token) <= m.maxWidth;
        token[fitLen + clen] = saved;
        if (!fits) break;
        fitLen += (size_t)clen;
      }
      if (fitLen == 0) {
        int clen = utf8SafeCharLenAt(token, tokLen, 0);
        if (clen <= 0) clen = 1;
        if ((size_t)clen > tokLen) clen = (int)tokLen;
        fitLen = (size_t)clen;
      }

      char saved = token[fitLen];
      token[fitLen] = 0;
      emit(token, fitLen);
      // Mid-token hard break is a soft wrap for markdown style carry.
      if (onSoftWrapCommit) onSoftWrapCommit(token, fitLen);
      token[fitLen] = saved;

      if (pageFull())
        return safeReturn(tokenStartPos + (uint32_t)fitLen);

      memmove(token, token + fitLen, tokLen - fitLen);
      tokLen -= fitLen;
      tokenStartPos += (uint32_t)fitLen;
    }
    return 0;
  };

  // Start a fresh line with the token, hard-breaking it if it can't fit on
  // a line by itself. tokenW is the computed width of the token being used.
  auto startLineWithToken = [&](int tokenW) -> uint32_t {
    if (tokenW > m.maxWidth) {
      return hardBreakToken();
    }
    memcpy(line, token, tokLen);
    lineLen = tokLen;
    lineW = tailW = tokenW;
    tailStart = 0;
    lineStartPos = tokenStartPos;
    tokLen = 0;
    return 0;
  };

  // Try to append the current token to the line. Flushes the line first if
  // the combined width would overflow; falls into hardBreakToken if even a
  // standalone token won't fit. Clears `tokLen` on success.
  auto appendTokenToLine = [&]() -> uint32_t {
    if (tokLen == 0) return 0;

    trimLeading(token, tokLen);
    if (tokLen == 0) return 0;
    token[tokLen] = 0;

    if (lineLen == 0) {
      return startLineWithToken(measure(token));
    }

    int candidateW;
    int combinedW = 0;
    if (onSoftWrapCommit) {
      // Full line: a prefix can change the face of this token (`**bold more**`).
      memcpy(scratch, line, lineLen);
      memcpy(scratch + lineLen, token, tokLen);
      scratch[lineLen + tokLen] = 0;
      candidateW = measure(scratch);
    } else {
      const size_t tailLen = lineLen - tailStart;
      memcpy(scratch, line + tailStart, tailLen);
      memcpy(scratch + tailLen, token, tokLen);
      scratch[tailLen + tokLen] = 0;
      combinedW = measure(scratch);
      candidateW = lineW - tailW + combinedW;
    }

    if (candidateW > m.maxWidth) {
      flushLine(/*softWrap=*/true);
      if (pageFull()) return safeReturn(tokenStartPos);
      return startLineWithToken(measure(token));
    }

    memcpy(line + lineLen, token, tokLen);
    lineLen += tokLen;
    if (!onSoftWrapCommit) {
      lineW = candidateW;
      tailW = combinedW;
    }
    tokLen = 0;
    return 0;
  };

  while (in.available() && !pageFull()) {
    uint32_t charPos = in.position();
    int rb = in.read();
    if (rb < 0) break;
    char c = (char)rb;
    if (c == '\r') continue;

    if (c == '\n') {
      uint32_t forcedNext = appendTokenToLine();
      if (forcedNext != 0) return forcedNext;
      // Empty line = paragraph break. Height can be full or half line depending on settings.
      if (lineLen == 0) {
        // If we're at the top of the page, just skip it.
        if (usedH > 0) {
          usedH += gapH;
          if (onLine) onLine("", 0);
          if (onStyleReset) onStyleReset();
        }
      } else {
        flushLine(/*softWrap=*/false);
      }
      if (pageFull()) return safeReturn(in.position());
      lineStartPos = in.position();
      continue;
    }

    if (isBreakableWhitespaceByte(c)) {
      uint32_t forcedNext = appendTokenToLine();
      if (forcedNext != 0) return forcedNext;
      if (lineLen > 0 && !lineEndsWithSpace() && lineLen < kLineMax - 1) {
        if (!onSoftWrapCommit) {
          if (spaceW < 0) spaceW = measure(" ");
          const size_t tailLen = lineLen - tailStart;
          memcpy(scratch, line + tailStart, tailLen);
          scratch[tailLen] = ' ';
          scratch[tailLen + 1] = 0;
          lineW += measure(scratch) - tailW;
        }
        line[lineLen++] = ' ';
        if (!onSoftWrapCommit) {
          tailStart = lineLen - 1;
          tailW = spaceW;
        }
      }
      continue;
    }

    // Defensive: if the token buffer is full (degenerate input — a long
    // run of non-breakable bytes), force a flush before appending. Normal
    // text never reaches this branch.
    if (tokLen >= kTokenMax - 1) {
      uint32_t forcedNext = appendTokenToLine();
      if (forcedNext != 0) return forcedNext;
    }
    if (tokLen == 0) tokenStartPos = charPos;
    token[tokLen++] = c;

    if (isBreakablePunctuationByte(c)) {
      uint32_t forcedNext = appendTokenToLine();
      if (forcedNext != 0) return forcedNext;
    }
  }

  uint32_t forcedNext = appendTokenToLine();
  if (forcedNext != 0) return forcedNext;

  if (!pageFull() && lineLen > 0) {
    flushLine(/*softWrap=*/false);
  }

  return safeReturn(in.position());
}
