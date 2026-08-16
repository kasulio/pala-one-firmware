#include "test_framework.h"
#include "pure/paths.h"

TEST_CASE("stripTxtExt removes .txt and .md") {
  CHECK(stripTxtExt("book.txt") == "book");
  CHECK(stripTxtExt("book.md") == "book");
  CHECK(stripTxtExt("book") == "book");
  CHECK(stripTxtExt("book.TXT") == "book.TXT");  // case-sensitive
  CHECK(stripTxtExt("book.MD") == "book.MD");
  CHECK(stripTxtExt(".txt") == "");
  CHECK(stripTxtExt(".md") == "");
  CHECK(stripTxtExt("") == "");
}

TEST_CASE("isBookFilename accepts .txt and .md") {
  CHECK(isBookFilename("a.txt"));
  CHECK(isBookFilename("a.md"));
  CHECK(isBookFilename("/books/a.txt"));
  CHECK(isBookFilename("/books/a.md"));
  CHECK(!isBookFilename("a.bin"));
  CHECK(!isBookFilename("a.TXT"));
  CHECK(!isBookFilename("a"));
  CHECK(!isBookFilename(""));
}

TEST_CASE("isMarkdownBookPath is .md only") {
  CHECK(isMarkdownBookPath("notes.md"));
  CHECK(isMarkdownBookPath("/books/notes.md"));
  CHECK(!isMarkdownBookPath("notes.txt"));
  CHECK(!isMarkdownBookPath("/books/notes.txt"));
  CHECK(!isMarkdownBookPath("notes"));
  CHECK(!isMarkdownBookPath("notes.MD"));
}

TEST_CASE("bookPathWithExt swaps book extension") {
  CHECK(bookPathWithExt("/books/a.txt", ".md") == "/books/a.md");
  CHECK(bookPathWithExt("/books/a.md", ".txt") == "/books/a.txt");
  CHECK(bookPathWithExt("/books/fiction/a.txt", ".md") == "/books/fiction/a.md");
  CHECK(bookPathWithExt("/books/a.txt", ".txt") == "/books/a.txt");
  CHECK(bookPathWithExt("/books/a.bin", ".md") == "");
  CHECK(bookPathWithExt("/books/a.txt", ".bin") == "");
  CHECK(bookPathWithExt("/books/a.txt", nullptr) == "");
}

TEST_CASE("lastPathComponent") {
  CHECK(lastPathComponent("/books/a.txt") == "a.txt");
  CHECK(lastPathComponent("a.txt") == "a.txt");
  CHECK(lastPathComponent("/a/b/c") == "c");
  CHECK(lastPathComponent("") == "");
  CHECK(lastPathComponent("/") == "");
}

TEST_CASE("folderParent") {
  CHECK(folderParent("a/b/c") == "a/b");
  CHECK(folderParent("a") == "");
  CHECK(folderParent("") == "");
  CHECK(folderParent("a/b") == "a");
}

TEST_CASE("prettyRelativeLabel") {
  CHECK(prettyRelativeLabel("the_great_book.txt") == "the great book");
  CHECK(prettyRelativeLabel("classics/the_iliad.txt") == "classics / the iliad");
  CHECK(prettyRelativeLabel("notes.md") == "notes");
  CHECK(prettyRelativeLabel("classics/notes.md") == "classics / notes");
  CHECK(prettyRelativeLabel("plain") == "plain");
}

TEST_CASE("folderLeafLabel") {
  CHECK(folderLeafLabel("a/b/sci_fi") == "sci fi");
  CHECK(folderLeafLabel("sci_fi") == "sci fi");
}

TEST_CASE("isAllowedFolderByte covers ascii alnum, underscore, dash, space, high bytes") {
  CHECK(isAllowedFolderByte('a'));
  CHECK(isAllowedFolderByte('Z'));
  CHECK(isAllowedFolderByte('0'));
  CHECK(isAllowedFolderByte('_'));
  CHECK(isAllowedFolderByte('-'));
  CHECK(isAllowedFolderByte(' '));
  CHECK(isAllowedFolderByte(0xC3));  // UTF-8 lead byte
  CHECK(!isAllowedFolderByte('/'));
  CHECK(!isAllowedFolderByte('.'));
  CHECK(!isAllowedFolderByte(':'));
  CHECK(!isAllowedFolderByte('\\'));
}

TEST_CASE("sanitizeFolderSegment replaces disallowed bytes") {
  CHECK(sanitizeFolderSegment("hello/world") == "hello_world");
  CHECK(sanitizeFolderSegment("a.b") == "a_b");
  CHECK(sanitizeFolderSegment("   x   ") == "x");
  CHECK(sanitizeFolderSegment("") == "");
}

TEST_CASE("sanitizeFolderInput normalizes separators and drops dot segments") {
  CHECK(sanitizeFolderInput("a/b/c") == "a/b/c");
  CHECK(sanitizeFolderInput("a\\b\\c") == "a/b/c");
  CHECK(sanitizeFolderInput("a/./b") == "a/b");
  CHECK(sanitizeFolderInput("a/../b") == "a/b");
  CHECK(sanitizeFolderInput("///a///b///") == "a/b");
  CHECK(sanitizeFolderInput("") == "");
  CHECK(sanitizeFolderInput(".") == "");
  CHECK(sanitizeFolderInput("..") == "");
}

TEST_CASE("sanitizeUploadedFilename") {
  CHECK(sanitizeUploadedFilename("simple.txt") == "simple.txt");
  CHECK(sanitizeUploadedFilename("notes.md") == "notes.md");
  CHECK(sanitizeUploadedFilename("no_ext") == "no_ext.txt");
  CHECK(sanitizeUploadedFilename("path/to/file.txt") == "file.txt");
  CHECK(sanitizeUploadedFilename("path/to/notes.md") == "notes.md");
  CHECK(sanitizeUploadedFilename("..hidden.txt") == "hidden.txt");
  CHECK(sanitizeUploadedFilename("with..parent.txt") == "withparent.txt");
  CHECK(sanitizeUploadedFilename("weird!@#chars.txt") == "weird___chars.txt");
  // Numeric, punctuation in middle stays as filename
  CHECK(sanitizeUploadedFilename("a1-b2.txt") == "a1-b2.txt");
  CHECK(sanitizeUploadedFilename("a1-b2.md") == "a1-b2.md");
}

TEST_CASE("sanitizeUploadedAppFilename") {
  CHECK(sanitizeUploadedAppFilename("simple.bin") == "simple.bin");
  CHECK(sanitizeUploadedAppFilename("no_ext") == "no_ext.bin");
  CHECK(sanitizeUploadedAppFilename("path/to/click_counter.bin") == "click_counter.bin");
  CHECK(sanitizeUploadedAppFilename("C:\\windows\\path\\app.bin") == "app.bin");
  CHECK(sanitizeUploadedAppFilename("") == "app.bin");
  // Double extension: stripped down to the stem, then .bin re-added.
  CHECK(sanitizeUploadedAppFilename("foo.tar.bin") == "foo.bin");
  // Disallowed chars (including spaces and punctuation) collapse to '_'.
  CHECK(sanitizeUploadedAppFilename("my app!.bin") == "my_app_.bin");
  // Leading dot: the ext-strip loop only strips the *trailing* extension
  // ("dot > 0", not >= 0), so ".hidden.bin" becomes ".hidden" after
  // strip. The byte filter then replaces '.' with '_' → "_hidden".
  // Hidden-file-like names can't sneak through as actual dot-prefixed
  // files on disk.
  CHECK(sanitizeUploadedAppFilename(".hidden.bin") == "_hidden.bin");
  // Mixed legal punctuation survives.
  CHECK(sanitizeUploadedAppFilename("a1-b2.bin") == "a1-b2.bin");
}
