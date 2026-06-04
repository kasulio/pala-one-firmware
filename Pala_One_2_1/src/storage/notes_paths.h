#ifndef PALA_STORAGE_NOTES_PATHS_H
#define PALA_STORAGE_NOTES_PATHS_H

#include <Arduino.h>

namespace NotesPaths
{

constexpr const char *kNotesFolderRel = "notes";
constexpr const char *kNotesDirAbs = "/books/notes";
constexpr size_t kMaxNotePath = 96;
constexpr size_t kMaxNotes = 32;

void ensureDirAndMigrate();

// Catalog book indices with folder == "notes". Returns count written to out[].
int listNoteBookIndices(int *out, int cap);

// Creates /books/notes/Note N.txt (first free N). pathOut must hold kMaxNotePath.
bool createNoteFile(char *pathOut, size_t pathCap);

const char *currentPath();
void setCurrentPath(const char *path);

size_t currentFileSize();

bool isNotesFolder(const char *folderRel);

} // namespace NotesPaths

#endif
