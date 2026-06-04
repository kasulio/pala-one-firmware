#include "src/ui/screens/notes_read_screen.h"

#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/storage/notes_paths.h"
#include "src/ui/lock.h"
#include "src/ui/notes_read_menu.h"
#include "src/ui/reader.h"
#include "src/ui/reader_actions.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/notes_edit_screen.h"
#include "src/ui/screens/notes_picker_screen.h"
#include "src/ui/sleep.h"
#include "src/ui/toast.h"

namespace
{

static bool openCurrentNoteForRead()
{
  const char *path = NotesPaths::currentPath();
  if (!path || !path[0])
    return false;

  resetBookView();
  if (!g_bookview.book.open(path))
    return false;

  g_bookview.pages.count = 1;
  g_bookview.pages.offsets[0] = 0;
  g_bookview.pages.eofReached = false;
  g_bookview.cursor.pageIndex = 0;
  g_bookview.cursor.pageTurnsSinceFull = 0;
  return true;
}

static void performReadHoldAction(ButtonAction action)
{
  switch (action)
  {
  case ACTION_BOOKMARK:
    break;
  case ACTION_LOCK:
    Lock::engage();
    Sleep::enter();
    break;
  case ACTION_MENU:
    NotesReadMenu::open();
    break;
  default:
    break;
  }
}

} // namespace

void NotesReadScreen::onEnter()
{
  if (!openCurrentNoteForRead())
  {
    nextScreen = &g_notesPickerScreen;
    return;
  }
  NotesReadMenu::close();
  draw();
}

void NotesReadScreen::draw()
{
  if (NotesReadMenu::isActive())
    NotesReadMenu::draw();
  else
    renderCurrentPage();
}

void NotesReadScreen::onButton(const ButtonEvent &e)
{
  if (!e.any())
    return;

  if (NotesReadMenu::isActive())
  {
    NotesReadMenu::onButton(e);
    if (NotesReadMenu::consumeEditRequest())
    {
      nextScreen = &g_notesEditScreen;
      return;
    }
    if (!NotesReadMenu::isActive())
    {
      g_bookview.cursor.pageTurnsSinceFull = FULL_REFRESH_EVERY_N_PAGES;
      renderCurrentPage();
    }
    return;
  }

  if (e.kind == ButtonEvent::Triple)
  {
    resetBookView();
    navigateToLibraryRoot();
    return;
  }

  if (e.kind == ButtonEvent::Long || e.kind == ButtonEvent::VeryLong ||
      e.kind == ButtonEvent::ClickHold)
  {
    performReadHoldAction(Gestures::actionFor(e.kind));
    return;
  }

  if (e.kind == ButtonEvent::Double)
  {
    if (retreatPage())
      draw();
    return;
  }

  if (e.kind == ButtonEvent::Short)
  {
    if (advancePage())
      draw();
    return;
  }
}

void NotesReadScreen::onSleep()
{
  resetBookView();
}

NotesReadScreen g_notesReadScreen;
