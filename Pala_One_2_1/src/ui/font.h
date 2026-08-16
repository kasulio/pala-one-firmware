#ifndef PALA_UI_FONT_H
#define PALA_UI_FONT_H

#include "src/pure/paginator.h"     // LayoutMetrics
#include "src/pure/md_style.h"      // MdStyleState
#include "src/storage/page_cache.h" // PageCacheLayout

// ============================================================================
//  Font module — owns the device-wide font role mapping (which u8g2 font
//  table is "body", "bold", etc.) and the layout-metrics cache derived from
//  the body font + line gap + screen dimensions.
//
//  Roles:
//    Body / Bold   Regular/bold body face at the user-chosen body size
//                  (8/10/12/14) AND family (Helvetica or OpenDyslexic).
//                  Reader text, menu rows, section headers.
//    Toast         Helvetica regular 8 — Latin Extended (accent-capable);
//                  used for toast text where translated strings may carry
//                  á é í ó ú ñ ¿ ¡ ü.
//    UiSmall       Fixed 6x10 (ASCII only). Battery percentage — digits/% only.
//    UiTiny        Fixed 5x8  (ASCII only). Page number in the status bar.
//
//  Roles, not tables: nothing outside font.cpp references u8g2 font
//  identifiers directly. Adding/changing a font is a one-file change.
//
//  Glyph coverage: UiSmall / UiTiny tables are _tf (ASCII printable only).
//  Do NOT route translated user-visible strings through them — they will
//  render missing-glyph boxes for any accent. Use Toast (or Body/Bold) for
//  anything that could contain a translation.
// ============================================================================
namespace Font {

// Body face family. OpenDyslexic is the alternative face for users with
// dyslexia; tables come from opendyslexic_u8g2_fonts.h (already vendored
// at the sketch root).
enum class Family : uint8_t { Helvetica = 0, OpenDyslexic = 1 };

// Switch the active u8g2 font to the named role. After one of these, raw
// u8g2 calls (setCursor/print/getUTF8Width) all use the role's table.
void useBody();
void useBold();
void useToast();
void useUiSmall();
void useUiTiny();

// Big-display bold (Helvetica B14). Exposed for the Pala apps API's
// `drawCenteredLarge` — firmware screens use Body/Bold under the
// user-chosen size and don't reach for this directly.
void useAppLarge();

// Layout metrics for the body font under the current line gap and screen
// dimensions. Cached; invalidated automatically by the mutators below.
// Calling this leaves the active u8g2 font set to Body.
const LayoutMetrics& bodyLayout();

// Drop the cached layout. External owners of layout-affecting state call
// this after they mutate (e.g. `Statusbar::setMode` changes the bottom
// reserve, which changes how many text lines fit on a page).
void invalidateLayoutCache();

// ----------------------------------------------------------------------------
//  Persistence — NVS keys live inside font.cpp; nothing else references them.
// ----------------------------------------------------------------------------

// Read body size + line gap from NVS into Font's internal state and apply
// them. Defaults: bodySize=10 (with input validation falling back to 10),
// lineGap=0 (clamped to [0, 4]). Call once from setup() after `prefs.begin`.
void loadSettings();

// Apply + persist. Out-of-set body sizes fall back to 10; line gap is
// clamped to [0, 4]. All invalidate the layout cache as needed.
void setBodySize(int sz);
void setLineGap(int gap);
void setFamily(Family fam);
void setBionic(bool on);
void setHalfParagraphGaps(bool on);

// Session flag: measure/draw markdown for the book currently being
// paginated. Not persisted — set from the book path (`.md`) on open and
// around cross-book page lookups. Defaults false.
void setMarkdownEnabled(bool on);
bool markdownEnabled();

// Current applied values. Page-cache stamping passes everything through
// `layoutForCache()`; these direct accessors exist for places that need
// the individual values (web settings form, debug output).
int    currentBodySize();
int    currentLineGap();
Family currentFamily();
bool   bionicEnabled();
bool   halfParagraphGapsEnabled();

// One-shot snapshot of every layout-affecting setting, packed for stamping
// the on-disk page cache. Centralizing this here means call sites in
// reader.cpp / text.cpp don't need to know which fields the cache stamps.
// Includes the statusbar reserve height so that toggling statusbar mode
// (which changes how many lines fit on a page) invalidates the cache too.
PageCacheLayout layoutForCache();

// Pixel gap inserted between the bold prefix and the regular tail of a
// bionic-rendered word. Also added to per-word measurements by the
// paginator so wrapping accounts for the extra width.
constexpr int kBionicRestGapPx = 1;

// Measure / draw one line of body text.
//
// Priority:
//   1. Bionic on  — whitespace-delimited words; qualifying words render a
//      Bold prefix + Body tail with kBionicRestGapPx between (see
//      bionicPrefixBytes). Markdown is ignored.
//   2. Helvetica + bionic off + markdown enabled for the active book
//      (`.md` path; see `setMarkdownEnabled`) — minimal markdown:
//      `**bold**`, `*italic*`, `***bold italic***`, and ATX headings
//      (`#`..`######` + space). Markers stripped; headings force Bold;
//      inline faces switch per run (italic = Adobe Helvetica oblique).
//      Measure matches draw so wrap stays correct. When `ioStyle` is
//      non-null, start from its bold/italic and write the ending state
//      back so soft-wrapped continuation lines keep an open face
//      (see text.cpp).
//   3. Else — plain Body getUTF8Width / print.
//
// `line` is NUL-terminated. drawBionicLine sets u8g2's cursor to (x, y)
// and leaves the active u8g2 font on Body afterwards.
int  measureBionicLine(const char* line, MdStyleState* ioStyle = nullptr);
void drawBionicLine(int x, int y, const char* line, MdStyleState* ioStyle = nullptr);

// Byte length of the leading "strong" (bold) prefix of `word` when rendered
// in bionic mode. Returns 0 if the word doesn't qualify (punctuation-only,
// or short enough that splitting would look wrong). UTF-8-aware.
int bionicPrefixBytes(const char* word, int byteLen);

}  // namespace Font

#endif  // PALA_UI_FONT_H
