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

#include <functional>
#include <unistd.h>
#include <cstdio>
#include <rak/file_stat.h>
#include <rak/error_number.h>
#include <rak/path.h>
#include <rak/socket_address.h>
#include <rak/string_manip.h>
#include <torrent/rate.h>
#include <torrent/throttle.h>
#include <torrent/tracker.h>
#include <torrent/connection_manager.h>
#include <torrent/data/file.h>
#include <torrent/data/file_list.h>
#include <torrent/peer/connection_list.h>
#include <torrent/peer/peer_list.h>

#include "core/download.h"
#include "core/download_store.h"
#include "core/manager.h"

#include "globals.h"
#include "control.h"
#include "command_helpers.h"

std::string
retrieve_d_base_path(core::Download* download) {
  if (download->file_list()->is_multi_file())
    return download->file_list()->frozen_root_dir();
  else
    return download->file_list()->empty() ? std::string() : download->file_list()->at(0)->frozen_path();
}

std::string
retrieve_d_base_filename(core::Download* download) {
  const std::string* base;

  if (download->file_list()->is_multi_file())
    base = &download->file_list()->frozen_root_dir();
  else
    base = &download->file_list()->at(0)->frozen_path();

  std::string::size_type split = base->rfind('/');

  if (split == std::string::npos)
    return *base;
  else
    return base->substr(split + 1);
}

torrent::Object
apply_d_change_link(core::Download* download, const torrent::Object::list_type& args, int changeType) {
  if (args.size() != 3)
    throw torrent::input_error("Wrong argument count.");

  torrent::Object::list_const_iterator itr = args.begin();

  const std::string& type    = (itr++)->as_string();
  const std::string& prefix  = (itr++)->as_string();
  const std::string& postfix = (itr++)->as_string();
  
  if (type.empty())
    throw torrent::input_error("Invalid arguments.");

  std::string target;
  std::string link;

  if (type == "base_path") {
    target = rpc::call_command_string("d.base_path", rpc::make_target(download));
    link = rak::path_expand(prefix + rpc::call_command_string("d.base_path", rpc::make_target(download)) + postfix);

  } else if (type == "base_filename") {
    target = rpc::call_command_string("d.base_path", rpc::make_target(download));
    link = rak::path_expand(prefix + rpc::call_command_string("d.base_filename", rpc::make_target(download)) + postfix);

//   } else if (type == "directory_path") {
//     target = rpc::call_command_string("d.directory", rpc::make_target(download));
//     link = rak::path_expand(prefix + rpc::call_command_string("d.base_path", rpc::make_target(download)) + postfix);

  } else if (type == "tied") {
    link = rak::path_expand(rpc::call_command_string("d.tied_to_file", rpc::make_target(download)));

    if (link.empty())
      return torrent::Object();

    link = rak::path_expand(prefix + link + postfix);
    target = rpc::call_command_string("d.base_path", rpc::make_target(download));

  } else {
    throw torrent::input_error("Unknown type argument.");
  }

  switch (changeType) {
  case 0:
    if (symlink(target.c_str(), link.c_str()) == -1)
      //     control->core()->push_log("create_link failed: " + std::string(rak::error_number::current().c_str()));
      //     control->core()->push_log("create_link failed: " + std::string(rak::error_number::current().c_str()) + " to " + target);
      ; // Disabled.
    break;

  case 1:
  {
    rak::file_stat fileStat;
    rak::error_number::clear_global();

    if (!fileStat.update_link(link) || !fileStat.is_link() ||
        unlink(link.c_str()) == -1)
      ; //     control->core()->push_log("delete_link failed: " + std::string(rak::error_number::current().c_str()));

    break;
  }
  default:
    break;
  }

  return torrent::Object();
}

torrent::Object
apply_d_delete_tied(core::Download* download) {
  const std::string& tie = rpc::call_command_string("d.tied_to_file", rpc::make_target(download));

  if (tie.empty())
    return torrent::Object();

  if (::unlink(rak::path_expand(tie).c_str()) == -1)
    control->core()->push_log_std("Could not unlink tied file: " + std::string(rak::error_number::current().c_str()));

  rpc::call_command("d.tied_to_file.set", std::string(), rpc::make_target(download));
  return torrent::Object();
}

torrent::Object
apply_d_connection_type(core::Download* download, const std::string& name) {
  torrent::Download::ConnectionType connType;

  if (name == "leech")
    connType = torrent::Download::CONNECTION_LEECH;
  else if (name == "seed")
    connType = torrent::Download::CONNECTION_SEED;
  else if (name == "initial_seed")
    connType = torrent::Download::CONNECTION_INITIAL_SEED;
  else
    throw torrent::input_error("Unknown peer connection type selected.");

  download->download()->set_connection_type(connType);
  return torrent::Object();
}

void
apply_d_directory(core::Download* download, const std::string& name) {
  if (!download->file_list()->is_multi_file())
    download->set_root_directory(name);
  else if (name.empty() || *name.rbegin() == '/')
    download->set_root_directory(name + download->info()->name());
  else
    download->set_root_directory(name + "/" + download->info()->name());
}

const char*
retrieve_d_connection_type(core::Download* download) {
  switch (download->download()->connection_type()) {
  case torrent::Download::CONNECTION_LEECH:
    return "leech";
  case torrent::Download::CONNECTION_SEED:
    return "seed";
  case torrent::Download::CONNECTION_INITIAL_SEED:
    return "initial_seed";
  default:
    return "unknown";
  }
}

const char*
retrieve_d_priority_str(core::Download* download) {
  switch (download->priority()) {
  case 0:
    return "off";
  case 1:
    return "low";
  case 2:
    return "normal";
  case 3:
    return "high";
  default:
    throw torrent::input_error("Priority out of range.");
  }
}

torrent::Object
retrieve_d_ratio(core::Download* download) {
  if (download->is_hash_checking())
    return int64_t();

  int64_t bytesDone = download->download()->bytes_done();
  int64_t upTotal   = download->info()->up_rate()->total();

  return bytesDone > 0 ? (1000 * upTotal) / bytesDone : 0;
}

torrent::Object
retrieve_d_hash(core::Download* download) {
  const torrent::HashString* hashString = &download->info()->hash();

  return torrent::Object(rak::transform_hex(hashString->begin(), hashString->end()));
}

torrent::Object
retrieve_d_local_id(core::Download* download) {
  const torrent::HashString* hashString = &download->info()->local_id();

  return torrent::Object(rak::transform_hex(hashString->begin(), hashString->end()));
}

torrent::Object
retrieve_d_local_id_html(core::Download* download) {
  const torrent::HashString* hashString = &download->info()->local_id();

  return torrent::Object(rak::copy_escape_html(hashString->begin(), hashString->end()));
}

torrent::Object
apply_d_custom(core::Download* download, const torrent::Object::list_type& args) {
  torrent::Object::list_const_iterator itr = args.begin();

  if (itr == args.end())
    throw torrent::bencode_error("Missing key argument.");

  const std::string& key = itr->as_string();

  if (++itr == args.end())
    throw torrent::bencode_error("Missing value argument.");

  download->bencode()->get_key("rtorrent").
                       insert_preserve_copy("custom", torrent::Object::create_map()).first->second.
                       insert_key(key, itr->as_string());
  return torrent::Object();
}

torrent::Object
retrieve_d_custom(core::Download* download, const std::string& key) {
  try {
    return download->bencode()->get_key("rtorrent").get_key("custom").get_key_string(key);

  } catch (torrent::bencode_error& e) {
    return std::string();
  }
}

torrent::Object
retrieve_d_custom_throw(core::Download* download, const std::string& key) {
  try {
    return download->bencode()->get_key("rtorrent").get_key("custom").get_key_string(key);

  } catch (torrent::bencode_error& e) {
    throw torrent::input_error("No such custom value.");
  }
}

torrent::Object
retrieve_d_bitfield(core::Download* download) {
  const torrent::Bitfield* bitField = download->download()->file_list()->bitfield();

  if (bitField->empty())
    return torrent::Object("");

  return torrent::Object(rak::transform_hex(bitField->begin(), bitField->end()));
}

// Just a helper function atm.
torrent::Object
cmd_d_initialize_logs(core::Download* download) {
  download->info()->signal_network_log().connect(sigc::mem_fun(control->core(), &core::Manager::push_log_complete));
  download->info()->signal_storage_error().connect(sigc::mem_fun(control->core(), &core::Manager::push_log_complete));

  if (!rpc::call_command_string("log.tracker").empty())
    download->info()->signal_tracker_dump().connect(sigc::ptr_fun(&core::receive_tracker_dump));

  return torrent::Object();
}

struct call_add_d_peer_t {
  call_add_d_peer_t(core::Download* d, int port) : m_download(d), m_port(port) { }

  void operator() (const sockaddr* sa, int err) {
    if (sa == NULL)
      control->core()->push_log("Could not resolve host.");
    else
      m_download->download()->add_peer(sa, m_port);
  }

  core::Download* m_download;
  int m_port;
};

void
apply_d_add_peer(core::Download* download, const std::string& arg) {
  int port, ret;
  char dummy;
  char host[1024];

  if (download->download()->info()->is_private())
    throw torrent::input_error("Download is private.");

  ret = std::sscanf(arg.c_str(), "%1023[^:]:%i%c", host, &port, &dummy);

  if (ret == 1)
    port = 6881;
  else if (ret != 2)
    throw torrent::input_error("Could not parse host.");

  if (port < 1 || port > 65535)
    throw torrent::input_error("Invalid port number.");

  torrent::connection_manager()->resolver()(host, (int)rak::socket_address::pf_inet, SOCK_STREAM, call_add_d_peer_t(download, port));
}

torrent::Object
f_multicall(core::Download* download, const torrent::Object::list_type& args) {
  if (args.empty())
    throw torrent::input_error("Too few arguments.");

  // We ignore the first arg for now, but it will be used for
  // selecting what files to include.

  // Add some pre-parsing of the commands, so we don't spend time
  // parsing and searching command map for every single call.
  torrent::Object             resultRaw = torrent::Object::create_list();
  torrent::Object::list_type& result = resultRaw.as_list();

  for (torrent::FileList::const_iterator itr = download->file_list()->begin(), last = download->file_list()->end(); itr != last; itr++) {
    torrent::Object::list_type& row = result.insert(result.end(), torrent::Object::create_list())->as_list();

    for (torrent::Object::list_const_iterator cItr = ++args.begin(), cLast = args.end(); cItr != args.end(); cItr++) {
      const std::string& cmd = cItr->as_string();
      row.push_back(rpc::parse_command(rpc::make_target(*itr), cmd.c_str(), cmd.c_str() + cmd.size()).first);
    }
  }

  return resultRaw;
}

torrent::Object
t_multicall(core::Download* download, const torrent::Object::list_type& args) {
  if (args.empty())
    throw torrent::input_error("Too few arguments.");

  // We ignore the first arg for now, but it will be used for
  // selecting what files to include.

  // Add some pre-parsing of the commands, so we don't spend time
  // parsing and searching command map for every single call.
  torrent::Object             resultRaw = torrent::Object::create_list();
  torrent::Object::list_type& result = resultRaw.as_list();

  for (int itr = 0, last = download->tracker_list()->size(); itr != last; itr++) {
    torrent::Object::list_type& row = result.insert(result.end(), torrent::Object::create_list())->as_list();

    for (torrent::Object::list_const_iterator cItr = ++args.begin(), cLast = args.end(); cItr != args.end(); cItr++) {
      const std::string& cmd = cItr->as_string();
      torrent::Tracker* t = download->tracker_list()->at(itr);

      row.push_back(rpc::parse_command(rpc::make_target(t), cmd.c_str(), cmd.c_str() + cmd.size()).first);
    }
  }

  return resultRaw;
}

torrent::Object
p_multicall(core::Download* download, const torrent::Object::list_type& args) {
  if (args.empty())
    throw torrent::input_error("Too few arguments.");

  // We ignore the first arg for now, but it will be used for
  // selecting what files to include.

  // Add some pre-parsing of the commands, so we don't spend time
  // parsing and searching command map for every single call.
  torrent::Object             resultRaw = torrent::Object::create_list();
  torrent::Object::list_type& result = resultRaw.as_list();

  for (torrent::ConnectionList::const_iterator itr = download->connection_list()->begin(), last = download->connection_list()->end();
       itr != last; itr++) {
    torrent::Object::list_type& row = result.insert(result.end(), torrent::Object::create_list())->as_list();

    for (torrent::Object::list_const_iterator cItr = ++args.begin(), cLast = args.end(); cItr != args.end(); cItr++) {
      const std::string& cmd = cItr->as_string();

      row.push_back(rpc::parse_command(rpc::make_target(*itr), cmd.c_str(), cmd.c_str() + cmd.size()).first);
    }
  }

  return resultRaw;
}

torrent::Object
p_call_target(const torrent::Object::list_type& args) {
  if (args.empty() || args.begin() + 1 == args.end() || args.begin() + 2 == args.end())
    throw torrent::input_error("Too few arguments.");

  // We ignore the first arg for now, but it will be used for
  // selecting what files to include.

  // Add some pre-parsing of the commands, so we don't spend time
  // parsing and searching command map for every single call.
  torrent::Object::list_const_iterator itr = args.begin();

  core::Download* download = control->core()->download_list()->find_hex_ptr(itr++->as_string().c_str());
  const std::string& peer_id = itr++->as_string();
  const std::string& command_key = itr++->as_string();

  torrent::HashString hash;

  if (peer_id.size() != 40 ||
      torrent::hash_string_from_hex_c_str(peer_id.c_str(), hash) == peer_id.c_str())
    throw torrent::input_error("Not a hash string.");

  torrent::ConnectionList::iterator peerItr = download->connection_list()->find(hash.c_str());

  if (peerItr == download->connection_list()->end())
    throw torrent::input_error("Could not find peer.");

  if (itr == args.end())
    return rpc::commands.call(command_key.c_str());

  if (itr + 1 == args.end())
    return rpc::commands.call(command_key.c_str(), *itr);

  return rpc::commands.call(command_key.c_str(), torrent::Object::create_list_range(itr, args.end()));
}

//
// New download commands and macros:
//

torrent::Object&
download_get_variable(core::Download* download, const char* first_key, const char* second_key = NULL) {
  if (second_key == NULL)
    return download->bencode()->get_key(first_key);

  return download->bencode()->get_key(first_key).get_key(second_key);
}

torrent::Object
download_set_variable(core::Download* download, const torrent::Object& rawArgs, const char* first_key, const char* second_key = NULL) {
  if (second_key == NULL)
    return download->bencode()->get_key(first_key) = torrent::object_create_normal(rawArgs);

  return download->bencode()->get_key(first_key).get_key(second_key) = torrent::object_create_normal(rawArgs);
}

torrent::Object
download_set_variable_value(core::Download* download, const torrent::Object::value_type& args,
                            const char* first_key, const char* second_key = NULL) {
  if (second_key == NULL)
    return download->bencode()->get_key(first_key) = args;

  return download->bencode()->get_key(first_key).get_key(second_key) = args;
}

torrent::Object
download_set_variable_string(core::Download* download, const torrent::Object::string_type& args,
                             const char* first_key, const char* second_key = NULL) {
  if (second_key == NULL)
    return download->bencode()->get_key(first_key) = args;

  return download->bencode()->get_key(first_key).get_key(second_key) = args;
}

//
//
//

torrent::Object
d_list_push_back(core::Download* download, const torrent::Object& rawArgs, const char* first_key, const char* second_key) {
  download_get_variable(download, first_key, second_key).as_list().push_back(rawArgs);

  return torrent::Object();
}

torrent::Object
d_list_push_back_unique(core::Download* download, const torrent::Object& rawArgs, const char* first_key, const char* second_key) {
  const torrent::Object& args = (rawArgs.is_list() && !rawArgs.as_list().empty()) ? rawArgs.as_list().front() : rawArgs;
  torrent::Object::list_type& list = download_get_variable(download, first_key, second_key).as_list();

  if (std::find_if(list.begin(), list.end(),
                   rak::bind1st(std::ptr_fun(&torrent::object_equal), args)) == list.end())
    list.push_back(rawArgs);

  return torrent::Object();
}

torrent::Object
d_list_has(core::Download* download, const torrent::Object& rawArgs, const char* first_key, const char* second_key) {
  const torrent::Object& args = (rawArgs.is_list() && !rawArgs.as_list().empty()) ? rawArgs.as_list().front() : rawArgs;
  torrent::Object::list_type& list = download_get_variable(download, first_key, second_key).as_list();

  return (int64_t)(std::find_if(list.begin(), list.end(),
                                rak::bind1st(std::ptr_fun(&torrent::object_equal), args)) != list.end());
}

torrent::Object
d_list_remove(core::Download* download, const torrent::Object& rawArgs, const char* first_key, const char* second_key) {
  const torrent::Object& args = (rawArgs.is_list() && !rawArgs.as_list().empty()) ? rawArgs.as_list().front() : rawArgs;
  torrent::Object::list_type& list = download_get_variable(download, first_key, second_key).as_list();

  list.erase(std::remove_if(list.begin(), list.end(), rak::bind1st(std::ptr_fun(&torrent::object_equal), args)), list.end());

  return torrent::Object();
}

#define CMD_ON_INFO(func) rak::on(std::mem_fun(&core::Download::info), std::mem_fun(&torrent::DownloadInfo::func))

#define CMD2_ON_INFO(func) std::tr1::bind(&torrent::DownloadInfo::func, std::tr1::bind(&core::Download::info, std::tr1::placeholders::_1))
#define CMD2_ON_DL(func) std::tr1::bind(&torrent::Download::func, std::tr1::bind(&core::Download::download, std::tr1::placeholders::_1))
#define CMD2_ON_FL(func) std::tr1::bind(&torrent::FileList::func, std::tr1::bind(&core::Download::file_list, std::tr1::placeholders::_1))

#define CMD2_BIND_DL std::tr1::bind(&core::Download::download, std::tr1::placeholders::_1)
#define CMD2_BIND_CL std::tr1::bind(&core::Download::connection_list, std::tr1::placeholders::_1)
#define CMD2_BIND_FL std::tr1::bind(&core::Download::file_list, std::tr1::placeholders::_1)
#define CMD2_BIND_PL std::tr1::bind(&core::Download::c_peer_list, std::tr1::placeholders::_1)
#define CMD2_BIND_TL std::tr1::bind(&core::Download::tracker_list, std::tr1::placeholders::_1)

#define CMD2_DL_VAR_VALUE(key, first_key, second_key)                   \
  CMD2_DL(key, std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, first_key, second_key)); \
  CMD2_DL_VALUE_P(key ".set", std::tr1::bind(&download_set_variable_value, \
                                             std::tr1::placeholders::_1, std::tr1::placeholders::_2, \
                                             first_key, second_key));

#define CMD2_DL_VAR_VALUE_PUBLIC(key, first_key, second_key)            \
  CMD2_DL(key, std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, first_key, second_key)); \
  CMD2_DL_VALUE(key ".set", std::tr1::bind(&download_set_variable_value, \
                                           std::tr1::placeholders::_1, std::tr1::placeholders::_2, \
                                           first_key, second_key));

#define CMD2_DL_VAR_STRING(key, first_key, second_key)                   \
  CMD2_DL(key, std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, first_key, second_key)); \
  CMD2_DL_STRING_P(key ".set", std::tr1::bind(&download_set_variable_string, \
                                              std::tr1::placeholders::_1, std::tr1::placeholders::_2, \
                                              first_key, second_key));

#define CMD2_DL_VAR_STRING_PUBLIC(key, first_key, second_key)                   \
  CMD2_DL(key, std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, first_key, second_key)); \
  CMD2_DL_STRING(key ".set", std::tr1::bind(&download_set_variable_string, \
                                            std::tr1::placeholders::_1, std::tr1::placeholders::_2, \
                                            first_key, second_key));

void
initialize_command_download() {
  CMD2_DL("d.hash",          std::tr1::bind(&retrieve_d_hash, std::tr1::placeholders::_1));
  CMD2_DL("d.local_id",      std::tr1::bind(&retrieve_d_local_id, std::tr1::placeholders::_1));
  CMD2_DL("d.local_id_html", std::tr1::bind(&retrieve_d_local_id_html, std::tr1::placeholders::_1));
  CMD2_DL("d.bitfield",      std::tr1::bind(&retrieve_d_bitfield, std::tr1::placeholders::_1));
  CMD2_DL("d.base_path",     std::tr1::bind(&retrieve_d_base_path, std::tr1::placeholders::_1));
  CMD2_DL("d.base_filename", std::tr1::bind(&retrieve_d_base_filename, std::tr1::placeholders::_1));

  CMD2_DL("d.name",          CMD2_ON_INFO(name));

  CMD2_DL("d.creation_date", CMD2_ON_INFO(creation_date));
  CMD2_DL("d.load_date",     CMD2_ON_INFO(load_date));

  //
  // Network related:
  //

  CMD2_DL         ("d.up.rate",       std::tr1::bind(&torrent::Rate::rate,  CMD2_ON_INFO(up_rate)));
  CMD2_DL         ("d.up.total",      std::tr1::bind(&torrent::Rate::total, CMD2_ON_INFO(up_rate)));
  CMD2_DL         ("d.down.rate",     std::tr1::bind(&torrent::Rate::rate,  CMD2_ON_INFO(down_rate)));
  CMD2_DL         ("d.down.total",    std::tr1::bind(&torrent::Rate::total, CMD2_ON_INFO(down_rate)));
  CMD2_DL         ("d.skip.rate",     std::tr1::bind(&torrent::Rate::rate,  CMD2_ON_INFO(skip_rate)));
  CMD2_DL         ("d.skip.total",    std::tr1::bind(&torrent::Rate::total, CMD2_ON_INFO(skip_rate)));

  CMD2_DL         ("d.peer_exchange",       CMD2_ON_INFO(is_pex_enabled));
  CMD2_DL_VALUE_V ("d.peer_exchange.set", std::tr1::bind(&torrent::Download::set_pex_enabled, CMD2_BIND_DL, std::tr1::placeholders::_2));

  CMD2_DL_LIST    ("d.create_link", std::tr1::bind(&apply_d_change_link, std::tr1::placeholders::_1, std::tr1::placeholders::_2, 0));
  CMD2_DL_LIST    ("d.delete_link", std::tr1::bind(&apply_d_change_link, std::tr1::placeholders::_1, std::tr1::placeholders::_2, 1));
  CMD2_DL         ("d.delete_tied", std::tr1::bind(&apply_d_delete_tied, std::tr1::placeholders::_1));

  CMD2_FUNC_SINGLE("d.start",     "d.hashing_failed.set=0 ;view.set_visible=started");
  CMD2_FUNC_SINGLE("d.stop",      "view.set_visible=stopped");
  CMD2_FUNC_SINGLE("d.try_start", "branch=\"or={d.hashing_failed=,d.ignore_commands=}\",{},{view.set_visible=started}");
  CMD2_FUNC_SINGLE("d.try_stop",  "branch=d.ignore_commands=, {}, {view.set_visible=stopped}");
  CMD2_FUNC_SINGLE("d.try_close", "branch=d.ignore_commands=, {}, {view.set_visible=stopped, d.close=}");

  //
  // Control functinos:
  //

  CMD2_DL         ("d.is_open",          CMD2_ON_INFO(is_open));
  CMD2_DL         ("d.is_active",        CMD2_ON_INFO(is_active));
  CMD2_DL         ("d.is_hash_checked",  std::tr1::bind(&torrent::Download::is_hash_checked, CMD2_BIND_DL));
  CMD2_DL         ("d.is_hash_checking", std::tr1::bind(&torrent::Download::is_hash_checking, CMD2_BIND_DL));
  CMD2_DL         ("d.is_multi_file",    std::tr1::bind(&torrent::FileList::is_multi_file, CMD2_BIND_FL));
  CMD2_DL         ("d.is_private",       CMD2_ON_INFO(is_private));
  CMD2_DL         ("d.is_pex_active",    CMD2_ON_INFO(is_pex_active));

  CMD2_DL_V("d.resume",     std::tr1::bind(&core::DownloadList::resume_default, control->core()->download_list(), std::tr1::placeholders::_1));
  CMD2_DL_V("d.pause",      std::tr1::bind(&core::DownloadList::pause_default, control->core()->download_list(), std::tr1::placeholders::_1));
  CMD2_DL_V("d.open",       std::tr1::bind(&core::DownloadList::open_throw, control->core()->download_list(), std::tr1::placeholders::_1));
  CMD2_DL_V("d.close",      std::tr1::bind(&core::DownloadList::close_throw, control->core()->download_list(), std::tr1::placeholders::_1));
  CMD2_DL_V("d.erase",      std::tr1::bind(&core::DownloadList::erase_ptr, control->core()->download_list(), std::tr1::placeholders::_1));
  CMD2_DL_V("d.check_hash", std::tr1::bind(&core::DownloadList::check_hash, control->core()->download_list(), std::tr1::placeholders::_1));

  CMD2_DL         ("d.save_resume",       std::tr1::bind(&core::DownloadStore::save_resume, control->core()->download_store(), std::tr1::placeholders::_1));
  CMD2_DL         ("d.save_full_session", std::tr1::bind(&core::DownloadStore::save_full, control->core()->download_store(), std::tr1::placeholders::_1));

  CMD2_DL_V("d.update_priorities", CMD2_ON_DL(update_priorities));

  CMD2_DL_STRING_V("add_peer",   std::tr1::bind(&apply_d_add_peer, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  //
  // Custom settings:
  //

  CMD2_DL_STRING("d.custom",       std::tr1::bind(&retrieve_d_custom, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_DL_STRING("d.custom_throw", std::tr1::bind(&retrieve_d_custom_throw, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_DL_LIST  ("d.custom.set",   std::tr1::bind(&apply_d_custom, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL_VAR_STRING_PUBLIC("d.custom1", "rtorrent", "custom1");
  CMD2_DL_VAR_STRING_PUBLIC("d.custom2", "rtorrent", "custom2");
  CMD2_DL_VAR_STRING_PUBLIC("d.custom3", "rtorrent", "custom3");
  CMD2_DL_VAR_STRING_PUBLIC("d.custom4", "rtorrent", "custom4");
  CMD2_DL_VAR_STRING_PUBLIC("d.custom5", "rtorrent", "custom5");

  // 0 - stopped
  // 1 - started
  CMD2_DL_VAR_VALUE("d.state", "rtorrent", "state");
  CMD2_DL_VAR_VALUE("d.complete", "rtorrent", "complete");

  // 0 off
  // 1 scheduled, being controlled by a download scheduler. Includes a priority.
  // 3 forced off
  // 2 forced on
  CMD2_DL_VAR_VALUE("d.mode", "rtorrent", "mode");

  // 0 - Not hashing
  // 1 - Normal hashing
  // 2 - Download finished, hashing
  // 3 - Rehashing
  CMD2_DL_VAR_VALUE("d.hashing", "rtorrent", "hashing");

  // 'tied_to_file' is the file the download is associated with, and
  // can be changed by the user.
  //
  // 'loaded_file' is the file this instance of the torrent was loaded
  // from, and should not be changed.
  CMD2_DL_VAR_STRING_PUBLIC("d.tied_to_file", "rtorrent", "tied_to_file");
  CMD2_DL_VAR_STRING("d.loaded_file",  "rtorrent", "loaded_file");

  // The "state_changed" variable is required to be a valid unix time
  // value, it indicates the last time the torrent changed its state,
  // resume/pause.
  CMD2_DL_VAR_VALUE("d.state_changed",          "rtorrent", "state_changed");
  CMD2_DL_VAR_VALUE("d.state_counter",          "rtorrent", "state_counter");
  CMD2_DL_VAR_VALUE_PUBLIC("d.ignore_commands", "rtorrent", "ignore_commands");

  CMD2_DL       ("d.connection_current",     std::tr1::bind(&retrieve_d_connection_type, std::tr1::placeholders::_1));
  CMD2_DL_STRING("d.connection_current.set", std::tr1::bind(&apply_d_connection_type, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL_VAR_STRING("d.connection_leech",      "rtorrent", "connection_leech");
  CMD2_DL_VAR_STRING("d.connection_seed",       "rtorrent", "connection_seed");

  CMD2_DL        ("d.hashing_failed",     std::tr1::bind(&core::Download::is_hash_failed, std::tr1::placeholders::_1));
  CMD2_DL_VALUE_V("d.hashing_failed.set", std::tr1::bind(&core::Download::set_hash_failed, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL         ("d.views",                  std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, "rtorrent", "views"));
  CMD2_DL         ("d.views.has",              std::tr1::bind(&d_list_has, std::tr1::placeholders::_1, std::tr1::placeholders::_2, "rtorrent", "views"));
  CMD2_DL         ("d.views.remove",           std::tr1::bind(&d_list_remove, std::tr1::placeholders::_1, std::tr1::placeholders::_2, "rtorrent", "views"));
  CMD2_DL         ("d.views.push_back",        std::tr1::bind(&d_list_push_back, std::tr1::placeholders::_1, std::tr1::placeholders::_2, "rtorrent", "views"));
  CMD2_DL         ("d.views.push_back_unique", std::tr1::bind(&d_list_push_back_unique, std::tr1::placeholders::_1, std::tr1::placeholders::_2, "rtorrent", "views"));

  // This command really needs to be improved, so we have proper
  // logging support.
  CMD2_DL         ("d.message",     std::tr1::bind(&core::Download::message, std::tr1::placeholders::_1));
  CMD2_DL_STRING_V("d.message.set", std::tr1::bind(&core::Download::set_message, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL         ("d.max_file_size",        CMD2_ON_FL(max_file_size));
  CMD2_DL_VALUE_V ("d.max_file_size.set",    std::tr1::bind(&torrent::FileList::set_max_file_size, CMD2_BIND_FL, std::tr1::placeholders::_2));

  CMD2_DL         ("d.peers_min",             std::tr1::bind(&torrent::ConnectionList::min_size, CMD2_BIND_CL));
  CMD2_DL_VALUE_V ("d.peers_min.set",         std::tr1::bind(&torrent::ConnectionList::set_min_size, CMD2_BIND_CL, std::tr1::placeholders::_2));
  CMD2_DL         ("d.peers_max",             std::tr1::bind(&torrent::ConnectionList::max_size, CMD2_BIND_CL));
  CMD2_DL_VALUE_V ("d.peers_max.set",         std::tr1::bind(&torrent::ConnectionList::set_max_size, CMD2_BIND_CL, std::tr1::placeholders::_2));
  CMD2_DL         ("d.uploads_max",           std::tr1::bind(&torrent::Download::uploads_max, CMD2_BIND_DL));
  CMD2_DL_VALUE_V ("d.uploads_max.set",       std::tr1::bind(&torrent::Download::set_uploads_max, CMD2_BIND_DL, std::tr1::placeholders::_2));
  CMD2_DL         ("d.peers_connected",       std::tr1::bind(&torrent::ConnectionList::size, CMD2_BIND_CL));
  CMD2_DL         ("d.peers_not_connected",   std::tr1::bind(&torrent::PeerList::available_list_size, CMD2_BIND_PL));

  CMD2_DL         ("d.peers_complete", CMD2_ON_DL(peers_complete));
  CMD2_DL         ("d.peers_accounted", CMD2_ON_DL(peers_accounted));

  CMD2_DL         ("d.throttle_name",     std::tr1::bind(&download_get_variable, std::tr1::placeholders::_1, "rtorrent", "throttle_name"));
  CMD2_DL_STRING_V("d.throttle_name.set", std::tr1::bind(&core::Download::set_throttle_name, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL         ("d.bytes_done",     CMD2_ON_DL(bytes_done));
  CMD2_DL         ("d.ratio",          std::tr1::bind(&retrieve_d_ratio, std::tr1::placeholders::_1));
  CMD2_DL         ("d.chunks_hashed",  CMD2_ON_DL(chunks_hashed));
  CMD2_DL         ("d.free_diskspace", CMD2_ON_FL(free_diskspace));

  CMD2_DL         ("d.size_files",     CMD2_ON_FL(size_files));
  CMD2_DL         ("d.size_bytes",     CMD2_ON_FL(size_bytes));
  CMD2_DL         ("d.size_chunks",    CMD2_ON_FL(size_chunks));
  CMD2_DL         ("d.chunk_size",     CMD2_ON_FL(chunk_size));
  CMD2_DL         ("d.size_pex",       CMD2_ON_DL(size_pex));
  CMD2_DL         ("d.max_size_pex",   CMD2_ON_DL(max_size_pex));

  CMD2_DL         ("d.completed_bytes",  CMD2_ON_FL(completed_bytes));
  CMD2_DL         ("d.completed_chunks", CMD2_ON_FL(completed_chunks));
  CMD2_DL         ("d.left_bytes",       CMD2_ON_FL(left_bytes));

  CMD2_DL         ("d.tracker_numwant",      std::tr1::bind(&torrent::TrackerList::numwant, CMD2_BIND_TL));
  CMD2_DL_VALUE_V ("d.tracker_numwant.set",  std::tr1::bind(&torrent::TrackerList::set_numwant, CMD2_BIND_TL, std::tr1::placeholders::_2));
  CMD2_DL         ("d.tracker_focus",        std::tr1::bind(&torrent::TrackerList::focus_index, CMD2_BIND_TL));
  CMD2_DL         ("d.tracker_size",         std::tr1::bind(&core::Download::tracker_list_size, std::tr1::placeholders::_1));

  CMD2_DL         ("d.directory",          CMD2_ON_FL(root_dir));
  CMD2_DL_STRING_V("d.directory.set",      std::tr1::bind(&apply_d_directory, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_DL         ("d.directory_base",     CMD2_ON_FL(root_dir));
  CMD2_DL_STRING_V("d.directory_base.set", std::tr1::bind(&core::Download::set_root_directory, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL         ("d.priority",     std::tr1::bind(&core::Download::priority, std::tr1::placeholders::_1));
  CMD2_DL         ("d.priority_str", std::tr1::bind(&retrieve_d_priority_str, std::tr1::placeholders::_1));
  CMD2_DL_VALUE_V ("d.priority.set", std::tr1::bind(&core::Download::set_priority, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_DL         ("d.initialize_logs", std::tr1::bind(&cmd_d_initialize_logs, std::tr1::placeholders::_1));

  CMD2_DL_LIST    ("f.multicall", std::tr1::bind(&f_multicall, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_DL_LIST    ("p.multicall", std::tr1::bind(&p_multicall, std::tr1::placeholders::_1, std::tr1::placeholders::_2));
  CMD2_DL_LIST    ("t.multicall", std::tr1::bind(&t_multicall, std::tr1::placeholders::_1, std::tr1::placeholders::_2));

  CMD2_ANY_LIST   ("p.call_target", std::tr1::bind(&p_call_target, std::tr1::placeholders::_2));
}
