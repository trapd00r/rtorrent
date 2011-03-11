// rTorrent - BitTorrent client
// Copyright (C) 2005-2007, Jari Sundell
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// In addition, as a special exception, the copyright holders give
// permission to link the code of portions of this program with the
// OpenSSL library under certain conditions as described in each
// individual source file, and distribute linked combinations
// including the two.
//
// You must obey the GNU General Public License in all respects for
// all of the code used other than OpenSSL.  If you modify file(s)
// with this exception, you may extend this exception to your version
// of the file(s), but you are not obligated to do so.  If you do not
// wish to do so, delete this exception statement from your version.
// If you delete this exception statement from all source files in the
// program, then also delete it here.
//
// Contact:  Jari Sundell <jaris@ifi.uio.no>
//
//           Skomakerveien 33
//           3185 Skoppum, NORWAY

// interface modifications by karabaja4
// <karabaja4@archlinux.us>

#include "config.h"

#include <rak/algorithm.h>
#include <torrent/rate.h>

#include "core/download.h"
#include "core/view.h"

#include "canvas.h"
#include "globals.h"
#include "utils.h"
#include "window_download_list.h"

namespace display {

WindowDownloadList::~WindowDownloadList() {
  m_connChanged.disconnect();
}

void
WindowDownloadList::set_view(core::View* l) {
  m_view = l;

  m_connChanged.disconnect();

  if (m_view != NULL)
    m_connChanged = m_view->signal_changed().connect(sigc::mem_fun(*this, &Window::mark_dirty));
}

void
WindowDownloadList::redraw() {
  m_slotSchedule(this, (cachedTime + rak::timer::from_seconds(1)).round_seconds());

  m_canvas->erase();

  if (m_view == NULL)
    return;

  m_canvas->print(0, 0, "%s", ("[View: " + m_view->name() + "]").c_str());

  if (m_view->empty_visible() || m_canvas->width() < 5 || m_canvas->height() < 2)
    return;

  typedef std::pair<core::View::iterator, core::View::iterator> Range;

  Range range = rak::advance_bidirectional(m_view->begin_visible(),
                                           m_view->focus() != m_view->end_visible() ? m_view->focus() : m_view->begin_visible(),
                                           m_view->end_visible(),
                                           (m_canvas->height() - 1) / 3);

  // Make sure we properly fill out the last lines so it looks like
  // there are more torrents, yet don't hide it if we got the last one
  // in focus.
  if (range.second != m_view->end_visible())
    ++range.second;

  int pos = 2;

  while (range.first != range.second) {
    char buffer[m_canvas->width() + 1];
    char* position;
    char* last = buffer + m_canvas->width() - 2 + 1;
    int title_length;
    
    //1 = red
    //2 = yellow
    //3 = green
	
	//do not print on last lines if cannot show whole torrent
	if (pos >= (m_canvas->height() - 1))
		break;
	
    //print title
    position = print_download_title(buffer, last, *range.first);
    //m_canvas->print(0, pos, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);
    title_length = strlen(buffer);
    m_canvas->print(0, pos, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);
    
    //title color
    if( (*range.first)->is_done() ) {
      //finished
      if( (*range.first)->download()->up_rate()->rate() != 0 ) {
		m_canvas->set_attr(3, pos, (title_length - 1), A_BOLD, 3);
      } else {
		m_canvas->set_attr(3, pos, (title_length - 1), A_NORMAL, 3);
      }
    } else {
      //not finished
      if( (*range.first)->download()->down_rate()->rate() != 0 ) {
		m_canvas->set_attr(3, pos, (title_length - 1), A_BOLD, 2);
      } else {
		m_canvas->set_attr(3, pos, (title_length - 1), A_NORMAL, 2);
      }
    }

    //print title extra
    position = print_download_title_extra(buffer, last, *range.first);
    
    //do not let title extra get off screen
    buffer[m_canvas->width() - title_length - 2] = '\0';
    
	m_canvas->print((title_length + 2), pos++, "%s", buffer);
    
    //print info
    position = print_download_info(buffer, last, *range.first);
    m_canvas->print(0, pos, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);
    
    //info color
    if (!(*range.first)->download()->is_open()) {
		//closed
		m_canvas->set_attr(3, pos, 6, A_NORMAL, 1);
	}
	else if (!(*range.first)->download()->is_active()) {
		//paused
		m_canvas->set_attr(3, pos, 6, A_NORMAL, 2);
	}
	else {
		//active
		m_canvas->set_attr(3, pos, 6, A_NORMAL, 3);
	}
	
	if ((*range.first)->is_done()) {
		//finished
		m_canvas->set_attr(12, pos, 8, A_NORMAL, 3);
	}
    
    //do not print info extra if it collides with info
    if ((strlen(buffer) + 2) <= (m_canvas->width() - 16)) {
		
		//print info extra
		position = print_download_info_extra(buffer, last, *range.first);
		m_canvas->print((m_canvas->width() - 16), pos++, "%s", buffer);
		
	}
	else {
		pos++;
	}
		
    //skip one line
    pos++;
    ++range.first;
  }    
}

void
WindowDownloadList::set_done_fg_color(int64_t color) {
  short fg, bg;
  pair_content(3, &fg, &bg);
  if( color < 0 ) color = -1;
  color = color % 8;
  init_pair(3, (short)color, bg);
}

void
WindowDownloadList::set_done_bg_color(int64_t color) {
  short fg, bg;
  pair_content(3, &fg, &bg);
  if( color < 0 ) color = -1;
  color = color % 8;
  init_pair(3, fg, (short)color);
}

void
WindowDownloadList::set_active_fg_color(int64_t color) {
  short fg, bg;
  pair_content(2, &fg, &bg);
  if( color < 0 ) color = -1;
  color = color % 8;
  init_pair(2, (short)color, bg);
}

void
WindowDownloadList::set_active_bg_color(int64_t color) {
  short fg, bg;
  pair_content(2, &fg, &bg);
  if( color < 0 ) color = -1;
  color = color % 8;
  init_pair(2, fg, (short)color);
}

}
