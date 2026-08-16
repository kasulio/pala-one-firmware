#ifndef PALA_PURE_MD_STYLE_H
#define PALA_PURE_MD_STYLE_H

// Minimal markdown style walker for body text:
//   `**` toggles bold, `*` toggles italic. `**` is matched greedily first
//   so `***x***` becomes bold+italic "x". Exception: when italic is already
//   open, bold is closed, and `**` is both left- and right-flanking
//   (`*this is**a test*`), split into italic-close + italic-open instead
//   of opening bold. Markers are consumed (never emitted). Unpaired open
//   leaves the rest of the line styled. No underscores, no escapes.
//
// Soft-wrapped continuation lines pass the same MdStyleState so an open
// `*` / `**` on the previous visual line keeps styling the next. Hard
// newlines and paragraph gaps reset (see paginator onSoftWrapCommit /
// onStyleReset). Page starts do not reset — recover with mdRecoverStyle
// from the last source `\n` (or byte 0) up to the page offset.
//
// `onRun(ptr, len, bold, italic)` for each non-empty run. Pure — no
// Arduino / display deps — so host tests cover the lexer.

#include "arduino_compat.h"

struct MdStyleState {
  bool bold = false;
  bool italic = false;
};

inline bool mdIsStyleWs(char c) {
  return c == ' ' || c == '\t';
}

// ATX heading at column 0: `#{1..6}` then a required space. Returns level
// 1..6 and sets *textOut to the first content byte (after hashes + space,
// with extra spaces skipped). Returns 0 and *textOut=line when not a
// heading (`#foo`, bare `##`, leading spaces, plain text).
inline int mdAtxHeading(const char* line, const char** textOut) {
  if (!textOut) return 0;
  *textOut = line;
  if (!line) return 0;
  int level = 0;
  while (line[level] == '#' && level < 6) level++;
  if (level == 0) return 0;
  if (line[level] != ' ') return 0;
  const char* p = line + level + 1;
  while (*p == ' ') p++;
  *textOut = p;
  return level;
}

// Walk `len` bytes of `line`, emitting styled runs. If `io` is non-null,
// start from its bold/italic and write the ending state back.
template <typename Fn>
inline void forEachMdStyleRun(const char* line, size_t len, Fn onRun,
                              MdStyleState* io = nullptr) {
  if (!line) return;
  MdStyleState local;
  MdStyleState* st = io ? io : &local;
  size_t i = 0;
  size_t runStart = 0;
  while (i < len) {
    if (line[i] == '*' && i + 1 < len && line[i + 1] == '*') {
      const bool rightFlank = (i > 0) && !mdIsStyleWs(line[i - 1]);
      const bool leftFlank = (i + 2 < len) && !mdIsStyleWs(line[i + 2]);
      // *foo**bar* — both-flanking ** with italic open is close+open, not bold.
      if (st->italic && !st->bold && rightFlank && leftFlank) {
        if (i > runStart) {
          onRun(line + runStart, (int)(i - runStart), st->bold, st->italic);
        }
        st->italic = !st->italic;
        i += 1;
        runStart = i;
        continue;
      }
      if (i > runStart) {
        onRun(line + runStart, (int)(i - runStart), st->bold, st->italic);
      }
      st->bold = !st->bold;
      i += 2;
      runStart = i;
      continue;
    }
    if (line[i] == '*') {
      if (i > runStart) {
        onRun(line + runStart, (int)(i - runStart), st->bold, st->italic);
      }
      st->italic = !st->italic;
      i += 1;
      runStart = i;
      continue;
    }
    i++;
  }
  if (i > runStart) onRun(line + runStart, (int)(i - runStart), st->bold, st->italic);
}

// NUL-terminated overload.
template <typename Fn>
inline void forEachMdStyleRun(const char* line, Fn onRun, MdStyleState* io = nullptr) {
  if (!line) return;
  size_t len = 0;
  while (line[len]) len++;
  forEachMdStyleRun(line, len, onRun, io);
}

// Advance style by consuming markers in `line` without emitting runs.
inline void mdAdvanceStyle(const char* line, MdStyleState* io) {
  if (!io) return;
  forEachMdStyleRun(line, [](const char*, int, bool, bool) {}, io);
}

inline void mdAdvanceStyle(const char* line, size_t len, MdStyleState* io) {
  if (!io || !line) return;
  forEachMdStyleRun(line, len, [](const char*, int, bool, bool) {}, io);
}

// Incoming face at a page-start offset: replay markers after the last `\n`
// in `buf[0..len)` (or from 0 if none). Hard newlines already reset style,
// so this is enough to restore an open `*` / `**` when a page split on a
// soft wrap.
inline void mdRecoverStyle(const char* buf, size_t len, MdStyleState* io) {
  if (!io) return;
  *io = MdStyleState{};
  if (!buf || len == 0) return;
  size_t start = 0;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') start = i + 1;
  }
  mdAdvanceStyle(buf + start, len - start, io);
}

#endif  // PALA_PURE_MD_STYLE_H
