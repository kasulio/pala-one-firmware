#ifndef PALA_PURE_PATHS_H
#define PALA_PURE_PATHS_H

#include "arduino_compat.h"

// Pure path / folder / filename utilities. No hardware deps.

// Strip a book extension. "book.txt" / "book.md" -> "book", "book" -> "book".
String stripTxtExt(const String& s);

// True if path/filename ends with a catalogued book extension (.txt or .md).
bool isBookFilename(const String& s);

// True if path should be measured/drawn with markdown styling (.md suffix).
bool isMarkdownBookPath(const String& s);

// Same directory + stem as `path`, with `ext` (".txt" or ".md"). Empty if
// `path` is not a book file or `ext` is not a book extension.
String bookPathWithExt(const String& path, const char* ext);

// Last "/" component. "/a/b/c" -> "c", "c" -> "c".
String lastPathComponent(const String& path);

// Parent folder of a relative path. "a/b/c" -> "a/b", "c" -> "".
String folderParent(const String& relPath);

// Replace '_' with ' ' and '/' with ' / ', strip book extension. For UI display.
String prettyRelativeLabel(const String& relPath);

// Last component with '_' -> ' '. For UI display.
String folderLeafLabel(const String& relPath);

// Display label for a book path: strip the leading folders, strip the
// book extension (.txt/.md), replace '_' with ' '.
String bookLeafLabel(const String& path);

// Byte-level character set used by sanitizeFolderSegment.
bool isAllowedFolderByte(uint8_t c);

// Replace disallowed bytes with '_', trim whitespace.
String sanitizeFolderSegment(const String& segment);

// Normalize separators, drop "." and "..", sanitize each segment, rejoin with '/'.
// Returns "" if every segment is empty or invalid.
String sanitizeFolderInput(const String& raw);

// Sanitize a user-uploaded filename. Removes path components, restricts to a
// safe byte set, kills repeated "..", keeps ".txt"/".md" (else appends
// ".txt"), falls back to "book.txt" if empty.
String sanitizeUploadedFilename(String fname);

// Sibling of `sanitizeUploadedFilename` for app .bin uploads. Removes path
// components, restricts to a safe byte set, kills repeated "..", strips
// every extension (so `my.app.bin` becomes `my_app.bin` after byte
// filtering — single extension), and ensures `.bin` suffix. Falls back to
// `"app.bin"` if empty.
String sanitizeUploadedAppFilename(String fname);

#endif  // PALA_PURE_PATHS_H
