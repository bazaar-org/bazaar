/* refresh-worker.c
 *
 * Copyright 2026 Eva M
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

#define G_LOG_DOMAIN "BAZAAR::REFRESH-WORKER"

#include "bz-backend-notification.h"
#include "bz-backend.h"
#include "bz-entry-cache-manager.h"
#include "bz-env.h"
#include "bz-flatpak-instance.h"
#include "bz-util.h"

BZ_DEFINE_DATA (
    main,
    Main,
    {
      GMainLoop  *loop;
      GIOChannel *stdout_channel;
      int         rv;
    },
    BZ_RELEASE_DATA (loop, g_main_loop_unref);
    BZ_RELEASE_DATA (stdout_channel, g_io_channel_unref));

static DexFuture *
run (MainData *data);

int
main (int   argc,
      char *argv[])
{
  g_autoptr (GIOChannel) stdout_channel = NULL;
  g_autoptr (GMainLoop) main_loop       = NULL;
  g_autoptr (MainData) data             = NULL;
  g_autoptr (DexFuture) future          = NULL;

  g_log_writer_default_set_use_stderr (TRUE);
  dex_init ();

  stdout_channel = g_io_channel_unix_new (STDOUT_FILENO);
  g_assert (g_io_channel_set_encoding (stdout_channel, NULL, NULL));
  g_io_channel_set_buffered (stdout_channel, FALSE);

  main_loop = g_main_loop_new (NULL, FALSE);

  data                 = main_data_new ();
  data->loop           = g_main_loop_ref (main_loop);
  data->stdout_channel = g_io_channel_ref (stdout_channel);
  data->rv             = EXIT_SUCCESS;

  future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) run,
      main_data_ref (data), main_data_unref);
  g_main_loop_run (main_loop);

  return data->rv;
}

static DexFuture *
run (MainData *data)
{
  gboolean result                       = FALSE;
  g_autoptr (GError) local_error        = NULL;
  g_autoptr (BzEntryCacheManager) cache = NULL;
  g_autoptr (BzFlatpakInstance) flatpak = NULL;
  g_autoptr (DexChannel) channel        = NULL;
  g_autoptr (GHashTable) installed_set  = NULL;
  g_autoptr (DexFuture) all_notifs      = NULL;
  guint n_notifs                        = 0;
  g_autoptr (GPtrArray) write_backs     = NULL;

  cache = bz_entry_cache_manager_new ();

  flatpak = dex_await_object (
      bz_flatpak_instance_new (),
      &local_error);
  if (flatpak == NULL)
    goto err;

  channel = bz_backend_create_notification_channel (BZ_BACKEND (flatpak));
  if (channel == NULL)
    goto err;

  result = dex_await (
      bz_backend_retrieve_remote_entries (
          BZ_BACKEND (flatpak), NULL),
      &local_error);
  if (!result)
    goto err;

  installed_set = dex_await_boxed (
      bz_backend_retrieve_install_ids (
          BZ_BACKEND (flatpak), NULL),
      &local_error);
  if (installed_set == NULL)
    goto err;

  all_notifs = dex_channel_receive_all (channel);
  n_notifs   = dex_future_set_get_size (DEX_FUTURE_SET (all_notifs));

  write_backs = g_ptr_array_new_with_free_func (dex_unref);
  for (guint i = 0; i < n_notifs; i++)
    {
      DexFuture *future                       = NULL;
      g_autoptr (BzBackendNotification) notif = NULL;
      BzBackendNotificationKind kind          = 0;

      future = dex_future_set_get_future_at (
          DEX_FUTURE_SET (all_notifs), i);

      notif = dex_await_object (dex_ref (future), NULL);
      if (notif == NULL)
        continue;

      kind = bz_backend_notification_get_kind (notif);
      if (kind == BZ_BACKEND_NOTIFICATION_KIND_REPLACE_ENTRY)
        {
          BzEntry    *entry     = NULL;
          const char *unique_id = NULL;

          entry     = bz_backend_notification_get_entry (notif);
          unique_id = bz_entry_get_unique_id (entry);
          bz_entry_set_installed (entry, g_hash_table_contains (installed_set, unique_id));

          g_ptr_array_add (
              write_backs,
              bz_entry_cache_manager_add (cache, entry));
        }
    }
  if (write_backs->len > 0)
    dex_await (
        dex_future_allv (
            (DexFuture *const *) write_backs->pdata,
            write_backs->len),
        NULL);

  data->rv = EXIT_SUCCESS;
  g_main_loop_quit (data->loop);
  return dex_future_new_true ();

err:
  if (local_error != NULL)
    g_critical ("Unable to complete refresh: %s", local_error->message);
  data->rv = EXIT_FAILURE;
  g_main_loop_quit (data->loop);
  return dex_future_new_false ();
}
