#ifndef PALA_PURE_PAGINATOR_H
#define PALA_PURE_PAGINATOR_H

#include <functional>

#include "arduino_compat.h"
#include "stream.h"

// Layout dimensions for the paginator. All values are in pixels.
struct LayoutMetrics {
  int ascent = 0;
  int descent = 0;
  int lineH = 0;
  int maxWidth = 0;
  int maxLines = 1;
  // Height to be used for a paragraph gap. Either lineH or lineH/2.
  int paragraphGapH = 0;
};

// Measure the rendered width (in pixels) of a UTF-8 string under the layout's
// current font. The paginator never sets fonts itself — callers must ensure
// the measurement function is consistent with the metrics they pass in.
using MeasureFn = std::function<int(const char*)>;

// Called once per emitted line, in order. `buf` is NUL-terminated and
// trailing-spaces-trimmed; `len` is its byte length (excluding NUL). The
// buffer is owned by the paginator and only valid for the duration of the
// call — copy if you need to keep it.
using LineCallback = std::function<void(const char* buf, size_t len)>;

// Soft-wrap style carry: after a width/hard-break split (not a source `\n`),
// walk the flushed line to advance markdown bold/italic so the next visual
// line + subsequent measure() calls keep the open face. Null = unused.
using SoftWrapCommitFn = std::function<void(const char* buf, size_t len)>;

// Reset markdown style after a source newline or paragraph gap. Not called
// at page entry — caller seeds incoming style (see mdRecoverStyle). Null =
// unused.
using StyleResetFn = std::function<void()>;

// Pure pagination engine. Reads bytes from `in` starting at `startPos` and
// emits at most `metrics.maxLines` lines via `onLine`. Returns the absolute
// byte offset where the next page begins.
//
// `onLine` may be null (just compute the next offset). `measure` MUST be set.
// `onSoftWrapCommit` / `onStyleReset` let the caller thread markdown style
// across soft wraps (including into the next page) while resetting on hard
// newlines. Incoming style at `startPos` is the caller's (see text.cpp).
// Non-null `onSoftWrapCommit` also switches measure to the full candidate
// line — a prefix `**` can change later-token widths, so the tail is not
// independent. Null keeps the tail-only measure used for plain text.
uint32_t paginatePage(IReadStream& in,
                      uint32_t startPos,
                      const LayoutMetrics& metrics,
                      const MeasureFn& measure,
                      const LineCallback& onLine = nullptr,
                      const SoftWrapCommitFn& onSoftWrapCommit = nullptr,
                      const StyleResetFn& onStyleReset = nullptr);

#endif  // PALA_PURE_PAGINATOR_H
