#include "src/ui/screens/notes_picker_screen.h"

#include "src/hal/display.h"
#include "src/storage/library.h"
#include "src/storage/notes_paths.h"
#include "src/ui/font.h"
#include "src/ui/notes_document.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/notes_edit_screen.h"
#include "src/ui/widgets.h"

namespace
{

static int s_cursor = 0;
static int s_noteIndices[NotesPaths::kMaxNotes];
static int s_noteCount = 0;

static int rowCount() { return 1 + s_noteCount; }

static String rowLabel(int row)
{
  if (row == 0)
    return D_NOTES_PICKER_NEW;
  return String(g_library.books[s_noteIndices[row - 1]].name);
}

static void openEditForRow(int row)
{
  if (row <= 0 || row > s_noteCount)
    return;
  const char *path = g_library.books[s_noteIndices[row - 1]].path;
  NotesPaths::setCurrentPath(path);
  NotesDocument::open(path);
  g_notesPickerScreen.nextScreen = &g_notesEditScreen;
}

static void createAndEdit()
{
  char path[NotesPaths::kMaxNotePath];
  if (!NotesPaths::createNoteFile(path, sizeof(path)))
    return;
  loadBooks();
  NotesDocument::open(path);
  g_notesPickerScreen.nextScreen = &g_notesEditScreen;
}

} // namespace

void NotesPickerScreen::onEnter()
{
  loadBooks();
  s_noteCount = NotesPaths::listNoteBookIndices(s_noteIndices, NotesPaths::kMaxNotes);
  if (s_cursor >= rowCount())
    s_cursor = max(0, rowCount() - 1);
  draw();
}

void NotesPickerScreen::draw()
{
  prepareMenuFrame();
  Font::useBody();
  int y = drawSectionHeader(D_NOTES_PICKER_HEADER);

  const int rows = rowCount();
  if (rows == 0)
  {
    drawMenuRow(y, D_NOTES_PICKER_NEW, true);
    display.update();
    return;
  }

  drawScrollableList(y, rows, s_cursor,
                     [&](int idx, int rowY, bool selected, int /*budget*/) {
                       drawMenuRow(rowY, rowLabel(idx), selected);
                       return 1;
                     });

  display.update();
}

void NotesPickerScreen::onButton(const ButtonEvent &e)
{
  if (!e.any())
    return;

  if (e.kind == ButtonEvent::Triple)
  {
    nextScreen = &g_libraryScreen;
    return;
  }

  const int rows = rowCount();
  if (rows == 0)
  {
    if (e.kind == ButtonEvent::Double)
      createAndEdit();
    else if (e.any())
      nextScreen = &g_libraryScreen;
    return;
  }

  if (e.kind == ButtonEvent::Short)
  {
    s_cursor = (s_cursor + 1) % rows;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Long)
  {
    s_cursor--;
    if (s_cursor < 0)
      s_cursor = rows - 1;
    draw();
    return;
  }

  if (e.kind == ButtonEvent::Double)
  {
    if (s_cursor == 0)
      createAndEdit();
    else
      openEditForRow(s_cursor);
    return;
  }
}

NotesPickerScreen g_notesPickerScreen;
