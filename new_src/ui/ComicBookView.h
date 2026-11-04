#ifndef NEW_UI_COMICBOOKVIEW_H
#define NEW_UI_COMICBOOKVIEW_H

#include "ui/ComicBookModel.h"
#include "ui/UiTypes.h"

namespace newcore {

class Ui;

// Stateless view (ADR 0012 point 1): ComicBook::Draw + DrawExitButton, landscape
// mode (src/ComicBook.cpp:137-214, :930-970). Returns {}: the session reads the input.
UiResult drawComicBook(Ui& ui, const ComicBookModel& m);

} // namespace newcore

#endif // NEW_UI_COMICBOOKVIEW_H
