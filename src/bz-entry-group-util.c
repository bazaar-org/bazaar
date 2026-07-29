/* bz-entry-group-util.c
 *
 * Copyright 2025 Adam Masciola, Alexander Vanhee
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

#include <adwaita.h>
#include <glib/gi18n.h>

#include "bz-entry-group-util.h"
#include "error.h"

BzEntry *
bz_entry_group_find_entry (BzEntryGroup *group,
                           gboolean (*test) (BzEntry *entry),
                           GtkWidget *window,
                           GError   **error)
{
  g_autoptr (GListModel) model     = NULL;
  guint n_items                    = 0;
  g_autoptr (GPtrArray) candidates = NULL;

  g_return_val_if_fail (BZ_IS_ENTRY_GROUP (group), NULL);

  model = dex_await_object (bz_entry_group_dup_all_into_store (group), error);
  if (model == NULL)
    return NULL;
  n_items = g_list_model_get_n_items (model);

  candidates = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzEntry) entry = NULL;

      entry = g_list_model_get_item (model, i);

      if (bz_entry_is_installed (entry) &&
          (test == NULL || test (entry)))
        g_ptr_array_add (candidates, g_steal_pointer (&entry));
    }

  if (candidates->len == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "BUG: No entry candidates satisfied this test condition");
      return NULL;
    }
  else if (candidates->len == 1)
    return g_ptr_array_steal_index_fast (candidates, 0);
  else if (window != NULL)
    {
      AdwDialog       *alert    = NULL;
      g_autofree char *response = NULL;

      alert = adw_alert_dialog_new (NULL, NULL);
      adw_alert_dialog_set_prefer_wide_layout (ADW_ALERT_DIALOG (alert), TRUE);
      adw_alert_dialog_format_heading (
          ADW_ALERT_DIALOG (alert),
          _ ("Choose an Installation"));
      adw_alert_dialog_format_body (
          ADW_ALERT_DIALOG (alert),
          _ ("You have multiple versions of this app installed. Which "
             "one would you like to proceed with?"));
      adw_alert_dialog_add_responses (
          ADW_ALERT_DIALOG (alert),
          "cancel", _ ("Cancel"),
          NULL);
      adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");
      adw_alert_dialog_set_response_appearance (
          ADW_ALERT_DIALOG (alert), "cancel", ADW_RESPONSE_DESTRUCTIVE);

      for (guint i = 0; i < candidates->len; i++)
        {
          BzEntry    *entry     = NULL;
          const char *unique_id = NULL;

          entry     = g_ptr_array_index (candidates, i);
          unique_id = bz_entry_get_unique_id (entry);

          adw_alert_dialog_add_responses (
              ADW_ALERT_DIALOG (alert),
              unique_id, unique_id,
              NULL);
          if (i == 0)
            adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), unique_id);
        }

      adw_dialog_present (alert, GTK_WIDGET (window));
      response = dex_await_string (
          bz_make_alert_dialog_future (ADW_ALERT_DIALOG (alert)),
          NULL);

      if (response != NULL)
        {
          for (guint i = 0; i < candidates->len; i++)
            {
              BzEntry    *entry     = NULL;
              const char *unique_id = NULL;

              entry     = g_ptr_array_index (candidates, i);
              unique_id = bz_entry_get_unique_id (entry);

              if (g_strcmp0 (unique_id, response) == 0)
                return g_object_ref (entry);
            }
        }
    }

  return NULL;
}
