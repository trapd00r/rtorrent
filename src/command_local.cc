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

#include <fcntl.h>
#include <functional>
#include <stdio.h>
#include <unistd.h>
#include <rak/path.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <torrent/torrent.h>
#include <torrent/chunk_manager.h>
#include <torrent/log_files.h>
#include <torrent/data/file_manager.h>
#include <torrent/data/chunk_utils.h>

#include "core/download.h"
#include "core/download_list.h"
#include "core/download_store.h"
#include "core/manager.h"
#include "rak/string_manip.h"
#include "rpc/parse_commands.h"
#include "rpc/scgi.h"
#include "utils/file_status_cache.h"

#include "globals.h"
#include "control.h"
#include "command_helpers.h"

typedef torrent::ChunkManager CM_t;
typedef torrent::FileManager  FM_t;

torrent::Object
apply_log(const torrent::Object::string_type& arg, int logType) {
  if (rpc::execFile.log_fd() != -1) {
    switch (logType) {
    case 0: ::close(rpc::execFile.log_fd()); rpc::execFile.set_log_fd(-1); break;
    case 1:
//       if (control->scgi()) {
//         ::close(control->scgi()->log_fd());
//         control->scgi()->set_log_fd(-1);
//       }
      break;
    default: break;
    }
  }

  if (arg.empty()) {
    int logFd = open(rak::path_expand(arg).c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);

    if (logFd < 0)
      throw torrent::input_error("Could not open execute log file.");

    switch (logType) {
    case 0: rpc::execFile.set_log_fd(logFd); break;
//     case 1: if (control->scgi()) control->scgi()->set_log_fd(logFd); break;
    default: break;
    }

    control->core()->push_log("Opened log file.");

  } else {
    control->core()->push_log("Closed log file.");
  }

  return torrent::Object();
}

torrent::Object
apply_log_libtorrent(const torrent::Object::list_type& args) {
  if (args.empty() || args.size() > 2)
    throw torrent::input_error("Invalid argument count.");
  
  torrent::log_file* log_file = torrent::find_log_file(args.front().as_string().c_str());

  if (log_file == NULL)
    throw torrent::input_error("Invalid log name.");

  if (args.size() == 1) {
    log_file->close();
    return torrent::Object();
  }
  
  if (!log_file->open_file(args.back().as_string().c_str()))
    throw torrent::input_error("Could not open log file.");

  return torrent::Object();
}

torrent::Object
system_hostname() {
  char buffer[1024];

  if (gethostname(buffer, 1023) == -1)
    throw torrent::input_error("Unable to read hostname.");

//   if (shorten)
//     *std::find(buffer, buffer + 1023, '.') = '\0';

  return std::string(buffer);
}

torrent::Object
system_get_cwd() {
  char* buffer = getcwd(NULL, 0);

  if (buffer == NULL)
    throw torrent::input_error("Unable to read cwd.");

  torrent::Object result = torrent::Object(std::string(buffer));
  free(buffer);

  return result;
}

torrent::Object
system_set_cwd(const torrent::Object::string_type& rawArgs) {
  if (::chdir(rawArgs.c_str()) != 0)
    throw torrent::input_error("Could not change current working directory.");

  return torrent::Object();
}

inline torrent::Object::list_const_iterator
post_increment(torrent::Object::list_const_iterator& itr, const torrent::Object::list_const_iterator& last) {
  if (itr == last)
    throw torrent::input_error("Invalid number of arguments.");

  return itr++;
}

inline const std::string&
check_name(const std::string& str) {
  if (!rak::is_all_name(str))
    throw torrent::input_error("Non-alphanumeric characters found.");

  return str;
}  

torrent::Object
group_insert(const torrent::Object::list_type& args) {
  torrent::Object::list_const_iterator itr = args.begin();
  torrent::Object::list_const_iterator last = args.end();

  const std::string& name = check_name(post_increment(itr, last)->as_string());
  const std::string& view = check_name(post_increment(itr, last)->as_string());

  rpc::commands.call("method.insert", rpc::create_object_list("group." + name + ".ratio.enable", "simple",
                                                              "schedule2=group." + name + ".ratio,5,60,on_ratio=" + name));
  rpc::commands.call("method.insert", rpc::create_object_list("group." + name + ".ratio.disable", "simple",
                                                              "schedule_remove2=group." + name + ".ratio"));
  rpc::commands.call("method.insert", rpc::create_object_list("group."  + name + ".ratio.command", "simple",
                                                              "d.try_close= ;d.ignore_commands.set=1"));
  rpc::commands.call("method.insert", rpc::create_object_list("group2." + name + ".view", "string", view));
  rpc::commands.call("method.insert", rpc::create_object_list("group2." + name + ".ratio.min", "value", (int64_t)200));
  rpc::commands.call("method.insert", rpc::create_object_list("group2." + name + ".ratio.max", "value", (int64_t)300));
  rpc::commands.call("method.insert", rpc::create_object_list("group2." + name + ".ratio.upload", "value", (int64_t)20 << 20));

  if (rpc::call_command_value("method.use_intermediate") == 1) {
    // Deprecated in 0.7.0:
    
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".view",          "group2." + name + ".view");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".view.set",      "group2." + name + ".view.set");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.min",     "group2." + name + ".ratio.min");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.min.set",    "group2." + name + ".ratio.min.set");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.max",        "group2." + name + ".ratio.max");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.max.set",    "group2." + name + ".ratio.max.set");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.upload",     "group2." + name + ".ratio.upload");
    CMD2_REDIRECT_GENERIC_STR("group." + name + ".ratio.upload.set", "group2." + name + ".ratio.upload.set");

  } if (rpc::call_command_value("method.use_intermediate") == 2) {
    // Deprecated in 0.7.0:
    
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".view",          "group2." + name + ".view");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".view.set",      "group2." + name + ".view.set");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.min",     "group2." + name + ".ratio.min");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.min.set",    "group2." + name + ".ratio.min.set");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.max",        "group2." + name + ".ratio.max");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.max.set",    "group2." + name + ".ratio.max.set");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.upload",     "group2." + name + ".ratio.upload");
    CMD2_REDIRECT_GENERIC_STR_NO_EXPORT("group." + name + ".ratio.upload.set", "group2." + name + ".ratio.upload.set");
  }

  return name;
}

torrent::Object
log_vmmap_dump(const std::string& str) {
  core::DownloadList* d_list = control->core()->download_list();
  std::vector<torrent::vm_mapping> all_mappings;

  for (core::DownloadList::iterator itr = d_list->begin(), last = d_list->end(); itr != last; itr++) {
    std::vector<torrent::vm_mapping> tmp_mappings = torrent::chunk_list_mapping((*itr)->download());

    all_mappings.insert(all_mappings.end(), tmp_mappings.begin(), tmp_mappings.end());    
  }

  FILE* log_file = fopen(str.c_str(), "w");

  for (std::vector<torrent::vm_mapping>::iterator itr = all_mappings.begin(), last = all_mappings.end(); itr != last; itr++) {
    fprintf(log_file, "%8p-%8p [%5llxk]\n", itr->ptr, (char*)itr->ptr + itr->length, itr->length / 1024);
  }

  fclose(log_file);
  return torrent::Object();
}

void
initialize_command_local() {
  torrent::ChunkManager* chunkManager = torrent::chunk_manager();
  torrent::FileManager*  fileManager = torrent::file_manager();
  core::DownloadList*    dList = control->core()->download_list();
  core::DownloadStore*   dStore = control->core()->download_store();

  CMD2_ANY         ("system.hostname", std::tr1::bind(&system_hostname));
  CMD2_ANY         ("system.pid",      std::tr1::bind(&getpid));

  CMD2_VAR_C_STRING("system.client_version",        PACKAGE_VERSION);
  CMD2_VAR_C_STRING("system.library_version",       torrent::version());
  CMD2_VAR_VALUE   ("system.file.allocate",         0);
  CMD2_VAR_VALUE   ("system.file.max_size",         -1);
  CMD2_VAR_VALUE   ("system.file.split_size",       -1);
  CMD2_VAR_STRING  ("system.file.split_suffix",     ".part");

  CMD2_ANY         ("system.file_status_cache.size",   std::tr1::bind(&utils::FileStatusCache::size,
                                                                      (utils::FileStatusCache::base_type*)control->core()->file_status_cache()));
  CMD2_ANY_V       ("system.file_status_cache.prune",  std::tr1::bind(&utils::FileStatusCache::prune, control->core()->file_status_cache()));

  CMD2_ANY         ("system.files.opened_counter",     std::tr1::bind(&FM_t::files_opened_counter, fileManager));
  CMD2_ANY         ("system.files.closed_counter",     std::tr1::bind(&FM_t::files_closed_counter, fileManager));
  CMD2_ANY         ("system.files.failed_counter",     std::tr1::bind(&FM_t::files_failed_counter, fileManager));

  CMD2_ANY         ("system.time",                     std::tr1::bind(&rak::timer::seconds, &cachedTime));
  CMD2_ANY         ("system.time_seconds",             std::tr1::bind(&rak::timer::current_seconds));
  CMD2_ANY         ("system.time_usec",                std::tr1::bind(&rak::timer::current_usec));

  CMD2_ANY_VALUE_V ("system.umask.set",                std::tr1::bind(&umask, std::tr1::placeholders::_2));

  CMD2_ANY         ("system.cwd",                      std::tr1::bind(&system_get_cwd));
  CMD2_ANY_STRING  ("system.cwd.set",                  std::tr1::bind(&system_set_cwd, std::tr1::placeholders::_2));

  CMD2_ANY         ("pieces.sync.always_safe",         std::tr1::bind(&CM_t::safe_sync, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.sync.always_safe.set",     std::tr1::bind(&CM_t::set_safe_sync, chunkManager, std::tr1::placeholders::_2));
  CMD2_ANY         ("pieces.sync.safe_free_diskspace", std::tr1::bind(&CM_t::safe_free_diskspace, chunkManager));
  CMD2_ANY         ("pieces.sync.timeout",             std::tr1::bind(&CM_t::timeout_sync, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.sync.timeout.set",         std::tr1::bind(&CM_t::set_timeout_sync, chunkManager, std::tr1::placeholders::_2));
  CMD2_ANY         ("pieces.sync.timeout_safe",        std::tr1::bind(&CM_t::timeout_safe_sync, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.sync.timeout_safe.set",    std::tr1::bind(&CM_t::set_timeout_safe_sync, chunkManager, std::tr1::placeholders::_2));

  CMD2_ANY         ("pieces.preload.type",             std::tr1::bind(&CM_t::preload_type, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.preload.type.set",         std::tr1::bind(&CM_t::set_preload_type, chunkManager, std::tr1::placeholders::_2));
  CMD2_ANY         ("pieces.preload.min_size",         std::tr1::bind(&CM_t::preload_min_size, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.preload.min_size.set",     std::tr1::bind(&CM_t::set_preload_min_size, chunkManager, std::tr1::placeholders::_2));
  CMD2_ANY         ("pieces.preload.min_rate",         std::tr1::bind(&CM_t::preload_required_rate, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.preload.min_rate.set",     std::tr1::bind(&CM_t::set_preload_required_rate, chunkManager, std::tr1::placeholders::_2));

  CMD2_ANY         ("pieces.memory.current",           std::tr1::bind(&CM_t::memory_usage, chunkManager));
  CMD2_ANY         ("pieces.memory.max",               std::tr1::bind(&CM_t::max_memory_usage, chunkManager));
  CMD2_ANY_VALUE_V ("pieces.memory.max.set",           std::tr1::bind(&CM_t::set_max_memory_usage, chunkManager, std::tr1::placeholders::_2));
  CMD2_ANY         ("pieces.stats_preloaded",          std::tr1::bind(&CM_t::stats_preloaded, chunkManager));
  CMD2_ANY         ("pieces.stats_not_preloaded",      std::tr1::bind(&CM_t::stats_not_preloaded, chunkManager));

  CMD2_VAR_BOOL    ("pieces.hash.on_completion",       true);

  CMD2_VAR_STRING  ("directory.default",       "./");

  CMD2_VAR_STRING  ("session.name",            "");
  CMD2_VAR_BOOL    ("session.use_lock",        true);
  CMD2_VAR_BOOL    ("session.on_completion",   true);

  CMD2_ANY         ("session.path",            std::tr1::bind(&core::DownloadStore::path, dStore));
  CMD2_ANY_STRING_V("session.path.set",        std::tr1::bind(&core::DownloadStore::set_path, dStore, std::tr1::placeholders::_2));

  CMD2_ANY_V       ("session.save",            std::tr1::bind(&core::DownloadList::session_save, dList));

#define CMD2_EXECUTE(key, flags)                                         \
  CMD2_ANY(key, std::tr1::bind(&rpc::ExecFile::execute_object, &rpc::execFile, std::tr1::placeholders::_2, flags));

  CMD2_EXECUTE     ("execute2",                rpc::ExecFile::flag_expand_tilde | rpc::ExecFile::flag_throw);
  CMD2_EXECUTE     ("execute.throw",           rpc::ExecFile::flag_expand_tilde | rpc::ExecFile::flag_throw);
  CMD2_EXECUTE     ("execute.nothrow",         rpc::ExecFile::flag_expand_tilde);
  CMD2_EXECUTE     ("execute.raw",             rpc::ExecFile::flag_throw);
  CMD2_EXECUTE     ("execute.raw_nothrow",     0);
  CMD2_EXECUTE     ("execute.capture",         rpc::ExecFile::flag_throw | rpc::ExecFile::flag_expand_tilde | rpc::ExecFile::flag_capture);
  CMD2_EXECUTE     ("execute.capture_nothrow", rpc::ExecFile::flag_expand_tilde | rpc::ExecFile::flag_capture);

  CMD2_ANY_STRING  ("log.execute",    std::tr1::bind(&apply_log, std::tr1::placeholders::_2, 0));
  CMD2_ANY_STRING  ("log.vmmap.dump", std::tr1::bind(&log_vmmap_dump, std::tr1::placeholders::_2));
  CMD2_ANY_STRING_V("log.xmlrpc",     std::tr1::bind(&ThreadWorker::set_xmlrpc_log, worker_thread, std::tr1::placeholders::_2));

  CMD2_ANY_LIST    ("log.libtorrent", std::tr1::bind(&apply_log_libtorrent, std::tr1::placeholders::_2));

  // TODO: Convert to new command types:
  *rpc::command_base::argument(0) = "placeholder.0";
  *rpc::command_base::argument(1) = "placeholder.1";
  *rpc::command_base::argument(2) = "placeholder.2";
  *rpc::command_base::argument(3) = "placeholder.3";
  CMD2_ANY_P("argument.0", std::tr1::bind(&rpc::command_base::argument_ref, 0));
  CMD2_ANY_P("argument.1", std::tr1::bind(&rpc::command_base::argument_ref, 1));
  CMD2_ANY_P("argument.2", std::tr1::bind(&rpc::command_base::argument_ref, 2));
  CMD2_ANY_P("argument.3", std::tr1::bind(&rpc::command_base::argument_ref, 3));

  CMD2_ANY_LIST  ("group.insert", std::tr1::bind(&group_insert, std::tr1::placeholders::_2));
}
