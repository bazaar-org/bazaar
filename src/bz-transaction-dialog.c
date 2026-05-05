/* bz-transaction-dialog.c
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

#include "bz-application.h"
#include "bz-entry-selection-row.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-flatpak-entry.h"
#include "bz-safety-calculator.h"
#include "bz-state-info.h"
#include "bz-transaction-dialog.h"
#include "bz-transaction-list-dialog.h"
#include "bz-util.h"

static gboolean
should_skip_entry (BzEntry *entry,
                   gboolean remove)
{
  gboolean is_installed;

  if (bz_entry_is_holding (entry))
    return TRUE;

  if (!remove && !bz_entry_is_reinstallable (entry))
    return TRUE;

  is_installed = bz_entry_is_installed (entry);

  return (!remove && is_installed) || (remove && !is_installed);
}

static GtkWidget *
create_entry_radio_button (BzEntry    *entry,
                           GtkWidget **out_radio)
{
  BzStateInfo *state_info       = NULL;
  GListModel  *repositories     = NULL;
  g_autoptr (BzRepository) repo = NULL;
  BzEntrySelectionRow *row      = NULL;
  GtkCheckButton      *radio    = NULL;

  state_info   = bz_state_info_get_default ();
  repositories = bz_state_info_get_repositories (state_info);

  if (repositories != NULL)
    repo = bz_entry_get_repository (entry, repositories);

  row   = bz_entry_selection_row_new (BZ_FLATPAK_ENTRY (entry), repo);
  radio = bz_entry_selection_row_get_radio (row);

  if (out_radio != NULL)
    *out_radio = GTK_WIDGET (radio);

  return GTK_WIDGET (row);
}

static GPtrArray *
create_entry_radio_buttons (AdwAlertDialog *alert,
                            GListStore     *store,
                            gboolean        remove,
                            gboolean        show_user_data)
{
  g_autoptr (GPtrArray) radios = NULL;
  GtkWidget *container         = NULL;

  container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

  radios = g_ptr_array_new ();
  if (store != NULL)
    {
      guint n_total_entries = g_list_model_get_n_items (G_LIST_MODEL (store));

      if (n_total_entries > 1)
        {
          GtkWidget      *listbox           = NULL;
          GtkCheckButton *first_valid_radio = NULL;
          GtkCheckButton *dummy_radio       = NULL;

          listbox = gtk_list_box_new ();
          gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);
          gtk_widget_add_css_class (listbox, "boxed-list");

          dummy_radio = GTK_CHECK_BUTTON (gtk_check_button_new ());

          for (guint i = 0; i < n_total_entries; i++)
            {
              g_autoptr (BzEntry) entry = NULL;
              GtkWidget *row            = NULL;
              GtkWidget *radio          = NULL;
              gboolean   should_skip    = FALSE;

              entry       = g_list_model_get_item (G_LIST_MODEL (store), i);
              should_skip = should_skip_entry (entry, remove);

              row = create_entry_radio_button (entry, &radio);
              g_ptr_array_add (radios, radio);

              gtk_check_button_set_group (GTK_CHECK_BUTTON (radio), dummy_radio);

              if (should_skip)
                {
                  gtk_widget_set_sensitive (row, FALSE);
                  gtk_widget_set_sensitive (radio, FALSE);
                }
              else
                {
                  if (first_valid_radio == NULL)
                    {
                      gtk_check_button_set_active (GTK_CHECK_BUTTON (radio), TRUE);
                      first_valid_radio = (GtkCheckButton *) radio;
                    }
                }

              gtk_list_box_append (GTK_LIST_BOX (listbox), row);
            }

          gtk_box_append (GTK_BOX (container), listbox);
        }
    }

  if (remove && show_user_data)
    {
      GtkWidget *listbox         = NULL;
      GtkWidget *keep_data_row   = NULL;
      GtkWidget *delete_data_row = NULL;
      GtkWidget *keep_radio      = NULL;
      GtkWidget *delete_radio    = NULL;

      listbox = gtk_list_box_new ();
      gtk_list_box_set_selection_mode (GTK_LIST_BOX (listbox), GTK_SELECTION_NONE);
      gtk_widget_add_css_class (listbox, "boxed-list");

      keep_data_row = adw_action_row_new ();
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (keep_data_row), _ ("Keep User Data"));
      adw_action_row_set_subtitle (ADW_ACTION_ROW (keep_data_row), _ ("Allow restoring personal settings &amp; content"));
      keep_radio = gtk_check_button_new ();
      gtk_widget_set_valign (keep_radio, GTK_ALIGN_CENTER);
      gtk_check_button_set_active (GTK_CHECK_BUTTON (keep_radio), TRUE);
      adw_action_row_add_prefix (ADW_ACTION_ROW (keep_data_row), keep_radio);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (keep_data_row), keep_radio);
      gtk_list_box_append (GTK_LIST_BOX (listbox), keep_data_row);

      delete_data_row = adw_action_row_new ();
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (delete_data_row), _ ("Delete All Data"));
      adw_action_row_set_subtitle (ADW_ACTION_ROW (delete_data_row), _ ("Permanently erase user data to save space"));
      delete_radio = gtk_check_button_new ();
      gtk_widget_set_valign (delete_radio, GTK_ALIGN_CENTER);
      gtk_check_button_set_group (GTK_CHECK_BUTTON (delete_radio), GTK_CHECK_BUTTON (keep_radio));
      adw_action_row_add_prefix (ADW_ACTION_ROW (delete_data_row), delete_radio);
      adw_action_row_set_activatable_widget (ADW_ACTION_ROW (delete_data_row), delete_radio);
      gtk_list_box_append (GTK_LIST_BOX (listbox), delete_data_row);

      g_ptr_array_add (radios, keep_radio);
      g_ptr_array_add (radios, delete_radio);
      gtk_box_append (GTK_BOX (container), listbox);
    }

  adw_alert_dialog_set_extra_child (alert, container);
  return g_steal_pointer (&radios);
}

static void
configure_install_dialog (AdwAlertDialog *alert,
                          const char     *title,
                          const char     *id,
                          gboolean        has_multiple_entries)
{
  g_autofree char *heading = NULL;

  heading = g_strdup_printf (_ ("Install %s?"), title);

  adw_alert_dialog_set_heading (alert, heading);

  if (has_multiple_entries)
    adw_alert_dialog_set_body (alert, _ ("Select which version to install. May install additional shared components"));
  else
    adw_alert_dialog_set_body (alert, _ ("May install additional shared components"));

  adw_alert_dialog_add_responses (alert,
                                  "cancel", _ ("_Cancel"),
                                  "install", _ ("_Install"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (alert, "install", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (alert, "install");
  adw_alert_dialog_set_close_response (alert, "cancel");
}

static void
configure_remove_dialog (AdwAlertDialog *alert,
                         const char     *title,
                         const char     *id,
                         gboolean        has_multiple_entries)
{
  g_autofree char *heading = NULL;
  g_autofree char *body    = NULL;

  heading = g_strdup_printf (_ ("Remove %s?"), title);

  if (has_multiple_entries)
    body = g_strdup (_ ("Select which version to remove."));
  else
    body = g_strdup_printf (_ ("It will not be possible to use %s after it is uninstalled."), title);

  adw_alert_dialog_set_heading (alert, heading);
  adw_alert_dialog_set_body (alert, body);

  adw_alert_dialog_add_responses (alert,
                                  "cancel", _ ("_Cancel"),
                                  "remove", _ ("_Remove"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (alert, "remove", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (alert, "remove");
  adw_alert_dialog_set_close_response (alert, "cancel");
}

static void
configure_high_risk_warning_dialog (AdwAlertDialog *alert,
                                    const char     *title,
                                    BzHighRiskGroup risk_groups)
{
  g_autofree char *heading = NULL;
  g_autofree char *body    = NULL;

  heading = g_strdup_printf (_ ("“%s” is High Risk"), title);

  if (risk_groups & BZ_HIGH_RISK_GROUP_DISK)
    {
      body = g_strdup (_ ("This app has full access to your system, including all "
                          "<b>your files, browser history, saved passwords</b>, and "
                          "more. It also has access to the internet, meaning it "
                          "could send your data to outside parties.\n\n"
                          "Because the app is proprietary, it can not be audited "
                          "for what it does with these permissions."));
    }
  else if (risk_groups & BZ_HIGH_RISK_GROUP_X11)
    {
      body = g_strdup (_ ("This app uses the legacy X11 windowing system, which "
                          "allows it to <b>record all keystrokes, capture screenshots, "
                          "and monitor other applications</b>. It also has access "
                          "to the internet, meaning it could send your data to "
                          "outside parties.\n\n"
                          "Because the app is proprietary, it can not be audited "
                          "for what it does with these permissions."));
    }

  adw_alert_dialog_set_heading (alert, heading);
  adw_alert_dialog_set_body (alert, body);
  adw_alert_dialog_set_body_use_markup (alert, TRUE);
  adw_alert_dialog_set_prefer_wide_layout (alert, TRUE);

  adw_alert_dialog_add_responses (alert,
                                  "cancel", _ ("_Cancel"),
                                  "install", _ ("_Install Anyway"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (alert, "install", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (alert, "cancel");
  adw_alert_dialog_set_close_response (alert, "cancel");
}

static BzHighRiskGroup
get_entry_high_risk_groups (BzEntry *entry)
{
  if (bz_entry_get_is_foss (entry))
    return BZ_HIGH_RISK_GROUP_NONE;

  return bz_safety_calculator_get_high_risk_groups (entry);
}

BZ_DEFINE_DATA (
    show_dialog,
    ShowDialog,
    {
      GtkWidget    *parent;
      BzEntry      *entry;
      BzEntryGroup *group;
      gboolean      remove;
      gboolean      auto_confirm;
    },
    BZ_RELEASE_DATA (entry, g_object_unref);
    BZ_RELEASE_DATA (group, g_object_unref))

static DexFuture *
show_dialog_fiber (ShowDialogData *data)
{
  g_autoptr (GError) local_error               = NULL;
  g_autoptr (GListStore) store                 = NULL;
  const char *title                            = NULL;
  const char *id                               = NULL;
  g_autoptr (AdwDialog) alert                  = NULL;
  g_autoptr (AdwDialog) risk_alert             = NULL;
  g_autoptr (GPtrArray) radios                 = NULL;
  g_autofree char *dialog_response             = NULL;
  g_autofree char *risk_response               = NULL;
  g_autoptr (BzTransactionDialogResult) result = NULL;
  g_autoptr (BzEntry) check_entry              = NULL;
  BzHighRiskGroup risk_groups                  = BZ_HIGH_RISK_GROUP_NONE;
  guint           n_total_entries              = 0;
  gboolean        confirmed                    = 0;
  gboolean        show_user_data               = TRUE;

  result = bz_transaction_dialog_result_new ();

  if (data->group != NULL)
    {
      store = dex_await_object (bz_entry_group_dup_all_into_store (data->group), &local_error);
      if (store == NULL)
        {
          bz_show_error_for_widget (data->parent, _ ("Failed to load transaction dialog"), local_error->message);
          return dex_future_new_for_error (g_steal_pointer (&local_error));
        }

      for (guint i = g_list_model_get_n_items (G_LIST_MODEL (store)); i > 0; i--)
        {
          g_autoptr (BzEntry) entry = NULL;

          entry = g_list_model_get_item (G_LIST_MODEL (store), i - 1);
          if (!bz_entry_is_reinstallable (entry) &&
              (!data->remove || !bz_entry_is_installed (entry)))
            g_list_store_remove (store, i - 1);
        }

      title = bz_entry_group_get_title (data->group);
      id    = bz_entry_group_get_id (data->group);

      n_total_entries = g_list_model_get_n_items (G_LIST_MODEL (store));
      if (n_total_entries > 0)
        check_entry = g_list_model_get_item (G_LIST_MODEL (store), 0);
    }
  else
    {
      title       = bz_entry_get_title (data->entry);
      id          = bz_entry_get_id (data->entry);
      check_entry = g_object_ref (data->entry);
    }

  if (check_entry != NULL && !BZ_IS_FLATPAK_ENTRY (check_entry))
    show_user_data = FALSE;

  if (!data->remove && check_entry != NULL)
    risk_groups = get_entry_high_risk_groups (check_entry);

  if (risk_groups != BZ_HIGH_RISK_GROUP_NONE)
    {
      risk_alert = g_object_ref_sink (adw_alert_dialog_new (NULL, NULL));
      configure_high_risk_warning_dialog (ADW_ALERT_DIALOG (risk_alert), title, risk_groups);

      adw_dialog_present (risk_alert, data->parent);
      risk_response = dex_await_string (
          bz_make_alert_dialog_future (ADW_ALERT_DIALOG (risk_alert)),
          &local_error);

      if (risk_response == NULL)
        return dex_future_new_for_error (g_steal_pointer (&local_error));

      if (g_strcmp0 (risk_response, "install") != 0)
        {
          bz_transaction_dialog_result_set_confirmed (result, FALSE);
          return dex_future_new_for_object (result);
        }
      data->auto_confirm = TRUE;
    }

  alert = g_object_ref_sink (adw_alert_dialog_new (NULL, NULL));
  if (data->remove)
    configure_remove_dialog (ADW_ALERT_DIALOG (alert), title, id, n_total_entries > 1);
  else
    configure_install_dialog (ADW_ALERT_DIALOG (alert), title, id, n_total_entries > 1);

  radios = create_entry_radio_buttons (ADW_ALERT_DIALOG (alert), store, data->remove, show_user_data);

  if (!data->remove && data->auto_confirm && radios->len <= 1)
    {
      dialog_response = g_strdup ("install");
      g_ptr_array_set_size (radios, 0);
      g_clear_object (&alert);
    }
  else if (data->remove && data->auto_confirm && radios->len <= 1)
    {
      dialog_response = g_strdup ("remove");
      g_ptr_array_set_size (radios, 0);
      g_clear_object (&alert);
    }
  else
    {
      adw_dialog_present (alert, data->parent);
      dialog_response = dex_await_string (
          bz_make_alert_dialog_future (ADW_ALERT_DIALOG (alert)),
          &local_error);
      if (dialog_response == NULL)
        return dex_future_new_for_error (g_steal_pointer (&local_error));

      if (data->remove && show_user_data && radios->len >= 2)
        {
          GtkCheckButton *delete_radio = g_ptr_array_index (radios, radios->len - 1);
          bz_transaction_dialog_result_set_delete_user_data (result, gtk_check_button_get_active (delete_radio));
        }
    }

  confirmed = (g_strcmp0 (dialog_response, "install") == 0) ||
              (g_strcmp0 (dialog_response, "remove") == 0);
  bz_transaction_dialog_result_set_confirmed (result, confirmed);
  if (!confirmed)
    return dex_future_new_for_object (result);

  if (data->group != NULL)
    {
      guint n_entries = g_list_model_get_n_items (G_LIST_MODEL (store));

      for (guint i = 0; i < MIN (n_entries, radios->len); i++)
        {
          GtkCheckButton *check = g_ptr_array_index (radios, i);

          if (gtk_check_button_get_active (check))
            {
              g_autoptr (BzEntry) entry = NULL;

              entry = g_list_model_get_item (G_LIST_MODEL (store), i);
              bz_transaction_dialog_result_set_selected_entry (result, entry);
              break;
            }
        }

      if (bz_transaction_dialog_result_get_selected_entry (result) == NULL &&
          n_entries > 0)
        {
          g_autoptr (BzEntry) entry = NULL;

          entry = g_list_model_get_item (G_LIST_MODEL (store), 0);
          bz_transaction_dialog_result_set_selected_entry (result, entry);
        }
    }
  else
    bz_transaction_dialog_result_set_selected_entry (result, data->entry);

  return dex_future_new_for_object (result);
}

DexFuture *
bz_transaction_dialog_show (GtkWidget    *parent,
                            BzEntry      *entry,
                            BzEntryGroup *group,
                            gboolean      remove,
                            gboolean      auto_confirm)
{
  g_autoptr (ShowDialogData) data = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (parent), NULL);
  g_return_val_if_fail (entry != NULL || group != NULL, NULL);

  data               = show_dialog_data_new ();
  data->parent       = parent;
  data->entry        = bz_object_maybe_ref (entry);
  data->group        = bz_object_maybe_ref (group);
  data->remove       = remove;
  data->auto_confirm = auto_confirm;

  return dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) show_dialog_fiber,
      g_steal_pointer (&data),
      show_dialog_data_unref);
}

BZ_DEFINE_DATA (
    bulk_install_dialog,
    BulkInstallDialog,
    {
      GtkWidget  *parent;
      GListModel *groups;
    },
    BZ_RELEASE_DATA (groups, g_object_unref));

static DexFuture *
bulk_install_dialog_fiber (BulkInstallDialogData *data)
{
  g_autoptr (GError) local_error               = NULL;
  g_autoptr (BzBulkInstallDialogResult) result = NULL;
  g_autoptr (GPtrArray) resolved_entries       = NULL;
  g_autoptr (GListStore) entries_store         = NULL;
  AdwDialog       *dialog                      = NULL;
  g_autofree char *dialog_response             = NULL;
  g_autofree char *heading                     = NULL;
  guint            n_groups                    = 0;
  gboolean         confirmed                   = FALSE;

  result           = bz_bulk_install_dialog_result_new ();
  resolved_entries = g_ptr_array_new_with_free_func (g_object_unref);

  if (data->groups == NULL)
    {
      bz_bulk_install_dialog_result_set_confirmed (result, FALSE);
      return dex_future_new_for_object (result);
    }

  n_groups = g_list_model_get_n_items (data->groups);

  for (guint i = 0; i < n_groups; i++)
    {
      g_autoptr (BzEntryGroup) group = NULL;
      g_autoptr (GListStore) store   = NULL;
      g_autoptr (BzEntry) entry      = NULL;

      group = g_list_model_get_item (data->groups, i);

      if (bz_entry_group_get_removable (group) > 0)
        continue;

      store = dex_await_object (bz_entry_group_dup_all_into_store (group), &local_error);
      if (store == NULL || g_list_model_get_n_items (G_LIST_MODEL (store)) == 0)
        continue;

      entry = g_list_model_get_item (G_LIST_MODEL (store), 0);
      if (entry == NULL)
        continue;

      if (bz_entry_is_installed (entry) || bz_entry_is_holding (entry))
        continue;

      g_ptr_array_add (resolved_entries, g_object_ref (entry));
    }

  if (resolved_entries->len == 0)
    {
      g_autoptr (AdwDialog) info_alert = NULL;

      info_alert = g_object_ref_sink (adw_alert_dialog_new (
          _ ("All apps are already installed"), NULL));

      adw_alert_dialog_add_response (ADW_ALERT_DIALOG (info_alert), "ok", _ ("_OK"));
      adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (info_alert), "ok");
      adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (info_alert), "ok");

      adw_dialog_present (info_alert, data->parent);

      dex_await (bz_make_alert_dialog_future (ADW_ALERT_DIALOG (info_alert)), NULL);

      bz_bulk_install_dialog_result_set_confirmed (result, FALSE);
      return dex_future_new_for_object (result);
    }

  entries_store = g_list_store_new (BZ_TYPE_ENTRY);
  for (guint i = 0; i < resolved_entries->len; i++)
    g_list_store_append (entries_store, g_ptr_array_index (resolved_entries, i));

  heading = g_strdup_printf (ngettext ("Install %u App?",
                                       "Install %u Apps?",
                                       resolved_entries->len),
                             resolved_entries->len);

  dialog = bz_transaction_list_dialog_new (
      G_LIST_MODEL (entries_store),
      heading,
      _ ("The following will be installed. Additional shared components may also be installed"),
      _ ("%d addons will be installed."),
      _ ("Additionally, addons will be installed."),
      _ ("_Cancel"),
      _ ("_Install All"));

  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "confirm");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  adw_dialog_present (dialog, data->parent);

  dialog_response = dex_await_string (
      bz_make_alert_dialog_future (ADW_ALERT_DIALOG (dialog)),
      &local_error);

  if (dialog_response == NULL)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  confirmed = bz_transaction_list_dialog_was_confirmed (
      BZ_TRANSACTION_LIST_DIALOG (dialog));
  bz_bulk_install_dialog_result_set_confirmed (result, confirmed);
  if (confirmed)
    {
      g_autoptr (GListStore) store = NULL;

      store = g_list_store_new (BZ_TYPE_ENTRY);
      for (guint i = 0; i < resolved_entries->len; i++)
        {
          BzEntry *entry = g_ptr_array_index (resolved_entries, i);
          g_list_store_append (store, entry);
        }

      bz_bulk_install_dialog_result_set_entries (result, G_LIST_MODEL (store));
    }
  return dex_future_new_for_object (result);
}

DexFuture *
bz_bulk_install_dialog_show (GtkWidget  *parent,
                             GListModel *groups)
{
  g_autoptr (BulkInstallDialogData) data = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (parent), NULL);
  g_return_val_if_fail (G_IS_LIST_MODEL (groups), NULL);

  data         = bulk_install_dialog_data_new ();
  data->parent = parent;
  data->groups = g_object_ref (groups);

  return dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) bulk_install_dialog_fiber,
      g_steal_pointer (&data),
      bulk_install_dialog_data_unref);
}
