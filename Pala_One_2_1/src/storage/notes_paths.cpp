#include "src/storage/notes_paths.h"

#include <stdio.h>
#include <string.h>

#include "src/state.h"
#include "src/storage/fs_util.h"
#include "src/storage/library.h"

namespace NotesPaths
{

namespace
{

static char s_currentPath[kMaxNotePath] = "";

static bool noteNameExists(int n)
{
  char path[kMaxNotePath];
  snprintf(path, sizeof(path), "%s/Note %d.txt", kNotesDirAbs, n);
  return FS.exists(path);
}

static void ensureDirOnly()
{
  ensureBooksDir();
  ensureDirRecursive(kNotesDirAbs);
}

static void migrateLegacyPoc()
{
  constexpr const char *kLegacy = "/notes/poc.txt";
  if (!FS.exists(kLegacy))
    return;

  ensureDirOnly();
  if (!createNoteFile(s_currentPath, sizeof(s_currentPath)))
    return;

  File src = FS.open(kLegacy, "r");
  File dst = FS.open(s_currentPath, "w");
  if (src && dst)
  {
    while (src.available())
      dst.write((uint8_t)src.read());
  }
  if (src)
    src.close();
  if (dst)
    dst.close();
  FS.remove(kLegacy);
  if (isDirEmpty("/notes"))
    FS.rmdir("/notes");
}

} // namespace

bool isNotesFolder(const char *folderRel)
{
  return folderRel && strcmp(folderRel, kNotesFolderRel) == 0;
}

void ensureDirAndMigrate()
{
  ensureDirOnly();
  migrateLegacyPoc();
}

int listNoteBookIndices(int *out, int cap)
{
  int n = 0;
  for (int i = 0; i < g_library.bookCount && n < cap; i++)
  {
    if (isNotesFolder(g_library.books[i].folder))
      out[n++] = i;
  }
  return n;
}

bool createNoteFile(char *pathOut, size_t pathCap)
{
  ensureDirOnly();
  for (int n = 1; n < 1000; n++)
  {
    char path[kMaxNotePath];
    snprintf(path, sizeof(path), "%s/Note %d.txt", kNotesDirAbs, n);
    if (!FS.exists(path))
    {
      File f = FS.open(path, "w");
      if (!f)
        return false;
      f.close();
      if (pathCap > 0)
      {
        strncpy(pathOut, path, pathCap - 1);
        pathOut[pathCap - 1] = '\0';
      }
      setCurrentPath(path);
      return true;
    }
  }
  return false;
}

const char *currentPath() { return s_currentPath; }

void setCurrentPath(const char *path)
{
  if (!path)
  {
    s_currentPath[0] = '\0';
    return;
  }
  strncpy(s_currentPath, path, sizeof(s_currentPath) - 1);
  s_currentPath[sizeof(s_currentPath) - 1] = '\0';
}

size_t currentFileSize()
{
  if (!s_currentPath[0] || !FS.exists(s_currentPath))
    return 0;
  File f = FS.open(s_currentPath, "r");
  if (!f)
    return 0;
  size_t sz = f.size();
  f.close();
  return sz;
}

} // namespace NotesPaths
