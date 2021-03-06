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

#include "config.h"

#include <rak/error_number.h>
#include <torrent/tracker.h>

#include "core/manager.h"

#include "globals.h"
#include "control.h"
#include "command_helpers.h"

void
initialize_command_tracker() {
  CMD2_TRACKER        ("t.is_open",           std::tr1::bind(&torrent::Tracker::is_busy, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.is_enabled",        std::tr1::bind(&torrent::Tracker::is_enabled, std::tr1::placeholders::_1));
  CMD2_TRACKER_VALUE_V("t.is_enabled.set",    std::tr1::bind(&torrent::Tracker::set_enabled, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_TRACKER_V      ("t.enable",            std::tr1::bind(&torrent::Tracker::enable, std::tr1::placeholders::_1));
  CMD2_TRACKER_V      ("t.disable",           std::tr1::bind(&torrent::Tracker::disable, std::tr1::placeholders::_1));

  CMD2_TRACKER        ("t.url",               std::tr1::bind(&torrent::Tracker::url, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.group",             std::tr1::bind(&torrent::Tracker::group, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.type",              std::tr1::bind(&torrent::Tracker::type, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.id",                std::tr1::bind(&torrent::Tracker::tracker_id, std::tr1::placeholders::_1));

  CMD2_TRACKER        ("t.normal_interval",   std::tr1::bind(&torrent::Tracker::normal_interval, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.min_interval",      std::tr1::bind(&torrent::Tracker::min_interval, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.scrape_time_last",  std::tr1::bind(&torrent::Tracker::scrape_time_last, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.scrape_complete",   std::tr1::bind(&torrent::Tracker::scrape_complete, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.scrape_incomplete", std::tr1::bind(&torrent::Tracker::scrape_incomplete, std::tr1::placeholders::_1));
  CMD2_TRACKER        ("t.scrape_downloaded", std::tr1::bind(&torrent::Tracker::scrape_downloaded, std::tr1::placeholders::_1));
}
