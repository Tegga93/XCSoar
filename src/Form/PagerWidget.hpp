/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#ifndef XCSOAR_PAGER_WIDGET_HPP
#define XCSOAR_PAGER_WIDGET_HPP

#include "Widget.hpp"
#include "Util/StaticArray.hpp"

/**
 * A #Widget that host multiple other widgets, displaying one at a
 * time.
 */
class PagerWidget : public Widget {
  struct Child {
    Widget *widget;

    /**
     * Has Widget::Prepare() been called?
     */
    bool prepared;

    Child() = default;

    gcc_constexpr_ctor
    Child(Widget *_widget):widget(_widget), prepared(false) {}
  };

  bool initialised, prepared, visible;

  ContainerWindow *parent;
  PixelRect position;

  unsigned current;
  StaticArray<Child, 32u> children;

public:
  PagerWidget():initialised(false) {}
  virtual ~PagerWidget();

  /**
   * Append a child #Widget to the end.  The program will abort when
   * the list of pages is already full.
   *
   * @param w a #Widget that is "uninitialised"; it will be deleted by
   * this class
   */
  void Add(Widget *w);

  /**
   * Delete all widgets.  This may only be called after Unprepare().
   */
  void Clear();

  unsigned GetSize() const {
    return children.size();
  }

  unsigned GetCurrentIndex() const {
    assert(!children.empty());

    return current;
  }

  const Widget *GetCurrentWidget() const {
    assert(!children.empty());

    return children[current].widget;
  }

  /**
   * Attempts to display page.  Follows Widget API rules
   * @param i Tab that is requested to be shown.
   * @param click true if Widget's Click() or ReClick() is to be called.
   * @return true if specified page is now visible
   */
  bool SetCurrent(unsigned i, bool click=false);

  bool Next();
  bool Previous();

  /**
   * Calls SetCurrentPage() with click=true parameter.
   * Call this to indicate that the user has clicked on the "handle
   * area" of a page (e.g. a tab).  It will invoke Widget::ReClick()
   * if the page was already visible, or Widget::Leave() then Widget::Click()
   * and switch to that page.
   *
   * @return true if the specified page is now visible
   */
  bool ClickPage(unsigned i) {
    return SetCurrent(i, true);
  }

  /* methods from Widget */
  virtual void Initialise(ContainerWindow &parent, const PixelRect &rc);
  virtual void Prepare(ContainerWindow &parent, const PixelRect &rc);
  virtual void Unprepare();
  virtual bool Save(bool &changed, bool &require_restart);
  virtual bool Click();
  virtual void ReClick();
  virtual void Show(const PixelRect &rc);
  virtual void Hide();
  virtual bool Leave();
  virtual void Move(const PixelRect &rc);
  virtual bool SetFocus();
};

#endif
