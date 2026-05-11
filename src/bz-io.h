/* bz-io.h
 *
 * Copyright 2025 Adam Masciola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <libdex.h>

G_BEGIN_DECLS

char *
bz_dup_user_data_path (const char *app_id);

char *
bz_dup_user_cache_path (const char *app_id);

DexScheduler *
bz_get_io_scheduler (void);

void
bz_reap_file (GFile *file);

void
bz_reap_path (const char *path);

DexFuture *
bz_reap_file_dex (GFile *file);

DexFuture *
bz_reap_path_dex (const char *path);

DexFuture *
bz_reap_user_data_dex (const char *app_id);

DexFuture *
bz_reap_user_cache_dex (const char *app_id);

DexFuture *
bz_get_user_sizes_dex (const char *app_id);

DexFuture *
bz_get_user_data_ids_dex (void);

char *
bz_dup_root_cache_dir (void);

char *
bz_dup_cache_dir (const char *submodule);

#define bz_dup_module_dir() bz_dup_cache_dir (BAZAAR_MODULE)

#define bz_discard_path(_path)                          \
  G_STMT_START                                          \
  {                                                     \
    if (g_file_test ((_path), G_FILE_TEST_IS_DIR))      \
      bz_reap_path ((_path));                           \
    else if (g_file_test ((_path), G_FILE_TEST_EXISTS)) \
      {                                                 \
        g_autoptr (GFile) file = NULL;                  \
                                                        \
        file = g_file_new_for_path ((_path));           \
        g_file_delete (file, NULL, NULL);               \
      }                                                 \
  }                                                     \
  G_STMT_END

#define bz_discard_module_dir()    \
  G_STMT_START                     \
  {                                \
    g_autofree char *_path = NULL; \
                                   \
    _path = bz_dup_module_dir ();  \
    bz_discard_path (_path);       \
  }                                \
  G_STMT_END

G_END_DECLS
