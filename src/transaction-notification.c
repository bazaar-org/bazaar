/* transaction-notification.c
 *
 * Copyright 2026 Alexander Vanhee
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

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "bz-async-texture.h"
#include "bz-entry.h"
#include "bz-transaction-entry-tracker.h"
#include "env.h"
#include "transaction-notification.h"
#include "util.h"

BZ_DEFINE_DATA (
    notify_finished,
    NotifyFinished,
    {
      BzTransaction *transaction;
      gboolean       success;
    },
    BZ_RELEASE_DATA (transaction, g_object_unref))

static DexFuture *
notify_finished_release_hold (DexFuture    *future,
                              GApplication *app);

static DexFuture *
notify_finished_fiber (NotifyFinishedData *data);

void
bz_transaction_notify_finished (BzTransaction *transaction,
                                gboolean       success)
{
  g_autoptr (NotifyFinishedData) data = NULL;
  GApplication *app                   = NULL;

  g_return_if_fail (BZ_IS_TRANSACTION (transaction));

  app = g_application_get_default ();
  g_application_hold (app);

  data              = notify_finished_data_new ();
  data->transaction = g_object_ref (transaction);
  data->success     = success;

  dex_future_disown (dex_future_finally (
      dex_scheduler_spawn (
          dex_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) notify_finished_fiber,
          notify_finished_data_ref (data),
          notify_finished_data_unref),
      (DexFutureCallback) notify_finished_release_hold,
      app, NULL));
}

static DexFuture *
notify_finished_release_hold (DexFuture    *future,
                              GApplication *app)
{
  g_application_release (app);
  return dex_ref (future);
}

static DexFuture *
notify_finished_fiber (NotifyFinishedData *data)
{
  BzTransaction *transaction = data->transaction;
  GApplication  *app         = NULL;
  GList         *windows     = NULL;
  GListModel    *trackers    = NULL;
  guint          n_trackers  = 0;

  if (!data->success)
    return dex_future_new_true ();

  app = g_application_get_default ();

  windows = gtk_application_get_windows (GTK_APPLICATION (app));
  if (windows != NULL)
    return dex_future_new_true ();

  trackers = bz_transaction_get_trackers (transaction);
  if (trackers != NULL)
    n_trackers = g_list_model_get_n_items (trackers);

  for (guint i = 0; i < n_trackers; i++)
    {
      g_autoptr (BzTransactionEntryTracker) tracker = NULL;
      BzTransactionEntryKind kind                   = 0;
      BzEntry               *entry                  = NULL;
      const char            *title                  = NULL;
      g_autofree char       *summary                = NULL;
      g_autofree char       *notif_id               = NULL;
      g_autoptr (GNotification) notification        = NULL;
      g_autoptr (GIcon) icon                        = NULL;
      GdkPaintable *paintable                       = NULL;

      tracker = g_list_model_get_item (trackers, i);
      kind    = bz_transaction_entry_tracker_get_kind (tracker);
      if (kind != BZ_TRANSACTION_ENTRY_KIND_INSTALL)
        continue;

      entry = bz_transaction_entry_tracker_get_entry (tracker);
      if (entry == NULL || !bz_entry_is_of_kinds (entry, BZ_ENTRY_KIND_APPLICATION))
        continue;

      title = bz_entry_get_title (entry);
      if (title == NULL)
        title = bz_entry_get_id (entry);
      if (title == NULL)
        continue;

      summary      = g_strdup_printf (_ ("%s Installed"), title);
      notification = g_notification_new (summary);
      g_notification_set_body (notification, _ ("The app is ready to be used"));

      paintable = bz_entry_get_icon_paintable (entry);
      if (paintable != NULL && BZ_IS_ASYNC_TEXTURE (paintable))
        icon = (GIcon *) dex_await_object (
            bz_async_texture_dup_icon_future (BZ_ASYNC_TEXTURE (paintable)),
            NULL);

      if (icon != NULL)
        g_notification_set_icon (notification, icon);

      g_notification_set_priority (notification, G_NOTIFICATION_PRIORITY_NORMAL);

      notif_id = g_strdup_printf ("app-installed-%s", bz_entry_get_unique_id (entry));
      g_application_send_notification (app, notif_id, notification);
    }

  return dex_future_new_true ();
}
