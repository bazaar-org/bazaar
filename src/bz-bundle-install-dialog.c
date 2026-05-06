/* bz-bundle-install-dialog.c
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

#include <glib/gi18n.h>

#include "bz-app-permissions.h"
#include "bz-bundle-install-dialog.h"
#include "bz-env.h"
#include "bz-template-callbacks.h"
#include "bz-safety-calculator.h"
#include "bz-util.h"
#include "bz-context-tile-callbacks.h"
#include "bz-release.h"
#include "bz-app-size-dialog.h"
#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-safety-dialog.h"
#include "bz-state-info.h"

struct _BzBundleInstallDialog
{
  AdwBreakpointBin parent_instance;

  BzStateInfo *state;
  BzEntry     *entry;

  AdwNavigationView *nav_view;
  GtkStack    *main_stack;
  AdwCarousel *carousel;
  GtkWidget   *page_info;
  GtkWidget   *page_progress;
  GtkWidget   *page_finish;

  GtkProgressBar *progress_bar;
  AdwStatusPage  *error_status;
  GtkImage *safety_icon;

  guint pulse_source_id;
};

G_DEFINE_FINAL_TYPE (BzBundleInstallDialog, bz_bundle_install_dialog, ADW_TYPE_BREAKPOINT_BIN);

enum
{
  PROP_0,

  PROP_STATE,
  PROP_ENTRY,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
install_fiber (GWeakRef *wr);

static void
bz_bundle_install_dialog_dispose (GObject *object)
{
  BzBundleInstallDialog *self = BZ_BUNDLE_INSTALL_DIALOG (object);

  g_clear_handle_id (&self->pulse_source_id, g_source_remove);
  g_clear_pointer (&self->state, g_object_unref);
  g_clear_pointer (&self->entry, g_object_unref);

  G_OBJECT_CLASS (bz_bundle_install_dialog_parent_class)->dispose (object);
}

static void
bz_bundle_install_dialog_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  BzBundleInstallDialog *self = BZ_BUNDLE_INSTALL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_bundle_install_dialog_get_state (self));
      break;
    case PROP_ENTRY:
      g_value_set_object (value, bz_bundle_install_dialog_get_entry (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_bundle_install_dialog_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  BzBundleInstallDialog *self = BZ_BUNDLE_INSTALL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_bundle_install_dialog_set_state (self, g_value_get_object (value));
      break;
    case PROP_ENTRY:
      bz_bundle_install_dialog_set_entry (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
pulse_progress_bar (gpointer user_data)
{
  BzBundleInstallDialog *self = BZ_BUNDLE_INSTALL_DIALOG (user_data);
  gtk_progress_bar_pulse (self->progress_bar);
  return G_SOURCE_CONTINUE;
}

static char *
get_version (gpointer    object,
             GListModel *version_history)
{
  g_autoptr (BzRelease) release = NULL;

  if (version_history == NULL ||
      g_list_model_get_n_items (version_history) == 0)
    return NULL;

  release = g_list_model_get_item (version_history, 0);
  return g_strdup (bz_release_get_version (release));
}

static char *
get_disk_title (gpointer object,
                guint64  installed_size)
{
  g_autofree char *size_str = NULL;

  if (installed_size == 0)
    return g_strdup (_("Unknown install size"));

  size_str = g_format_size (installed_size);
  return g_strdup_printf (_("About %s to install"), size_str);
}

static char *
get_safety_subtitle (gpointer object,
                     BzEntry *entry)
{
  g_autoptr (GListModel) model   = NULL;
  g_autoptr (GString)    result  = NULL;
  guint                  n_items = 0;
  guint                  n_picked = 0;
  guint                  n_total  = 0;

  if (entry == NULL)
    return g_strdup (_("N/A"));

  model   = bz_safety_calculator_analyze_entry (entry);
  n_items = g_list_model_get_n_items (model);
  result  = g_string_new (NULL);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzSafetyRow) row = g_list_model_get_item (model, i);
      BzImportance importance     = BZ_IMPORTANCE_UNIMPORTANT;
      g_autofree char *title      = NULL;

      g_object_get (row, "importance", &importance, "title", &title, NULL);

      if (importance <= BZ_IMPORTANCE_UNIMPORTANT || title == NULL)
        continue;

      n_total++;

      if (n_picked < 2)
        {
          if (n_picked > 0)
            g_string_append (result, ", ");
          g_string_append (result, title);
          n_picked++;
        }
    }

  if (n_picked == 0)
    return g_strdup (_("No special permissions"));

  if (n_total > 2)
    g_string_append (result, ", …");

  return g_string_free (g_steal_pointer (&result), FALSE);
}

static char *
get_safety_icon_name (gpointer object,
                      BzEntry *entry)
{
  BzImportance importance;

  if (entry == NULL)
    return g_strdup ("app-safety-unknown-symbolic");

  importance = bz_safety_calculator_calculate_rating (entry);

  switch (importance)
    {
    case BZ_IMPORTANCE_UNIMPORTANT:
      return g_strdup ("app-safety-ok-symbolic");
    case BZ_IMPORTANCE_NEUTRAL:
      return g_strdup ("permissions-sandboxed-symbolic");
    case BZ_IMPORTANCE_INFORMATION:
    case BZ_IMPORTANCE_WARNING:
      return g_strdup ("permissions-warning-symbolic");
    case BZ_IMPORTANCE_IMPORTANT:
      return g_strdup ("app-safety-unsafe-symbolic");
    default:
      return g_strdup ("app-safety-unknown-symbolic");
    }
}

static void
install_cb (BzBundleInstallDialog *self,
            GtkButton             *button)
{
  if (self->entry == NULL ||
      self->state == NULL)
    return;

  adw_carousel_scroll_to (self->carousel, self->page_progress, TRUE);

  self->pulse_source_id = g_timeout_add (80, pulse_progress_bar, self);

  dex_future_disown (dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) install_fiber,
      bz_track_weak (self),
      bz_weak_release));
}

static void
run_cb (BzBundleInstallDialog *self,
        GtkButton             *button)
{
  const char *id = NULL;

  if (self->entry == NULL)
    return;

  id = bz_entry_get_id (self->entry);
  gtk_widget_activate_action (GTK_WIDGET (self), "window.launch-group",
                              "s", id);
}

static void
show_cb (BzBundleInstallDialog *self,
         GtkButton             *button)
{
  const char *id = NULL;

  if (self->entry == NULL)
    return;

  id = bz_entry_get_id (self->entry);
  adw_dialog_close (ADW_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_DIALOG)));
  gtk_widget_activate_action (GTK_WIDGET (self), "window.show-group",
                              "s", id);
}

static void
permission_cb (AdwActionRow          *row,
               BzBundleInstallDialog *self)
{
  AdwNavigationPage *page = NULL;

  if (self->entry == NULL)
    return;

  page = bz_safety_dialog_page_new (self->entry);
  adw_navigation_view_push (self->nav_view, page);
}

static void
bz_bundle_install_dialog_class_init (BzBundleInstallDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_bundle_install_dialog_set_property;
  object_class->get_property = bz_bundle_install_dialog_get_property;
  object_class->dispose      = bz_bundle_install_dialog_dispose;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ENTRY] =
      g_param_spec_object (
          "entry",
          NULL, NULL,
          BZ_TYPE_ENTRY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-bundle-install-dialog.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  bz_widget_class_bind_all_context_tile_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, nav_view);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, carousel);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_info);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_progress);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_finish);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, error_status);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, safety_icon);
  gtk_widget_class_bind_template_callback (widget_class, install_cb);
  gtk_widget_class_bind_template_callback (widget_class, run_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_cb);
  gtk_widget_class_bind_template_callback (widget_class, permission_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_version);
  gtk_widget_class_bind_template_callback (widget_class, get_disk_title);
  gtk_widget_class_bind_template_callback (widget_class, get_safety_subtitle);
  gtk_widget_class_bind_template_callback (widget_class, get_safety_icon_name);
}

static void
bz_bundle_install_dialog_init (BzBundleInstallDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

static void
bz_bundle_install_dialog_update_visible_page (BzBundleInstallDialog *self)
{
  if (self->entry == NULL)
    return;

  if (bz_entry_is_installed (self->entry))
    gtk_stack_set_visible_child_name (self->main_stack, "installed");
  else
    gtk_stack_set_visible_child_name (self->main_stack, "carousel");
}

BzBundleInstallDialog *
bz_bundle_install_dialog_new (void)
{
  return g_object_new (BZ_TYPE_BUNDLE_INSTALL_DIALOG, NULL);
}

BzStateInfo *
bz_bundle_install_dialog_get_state (BzBundleInstallDialog *self)
{
  g_return_val_if_fail (BZ_IS_BUNDLE_INSTALL_DIALOG (self), NULL);
  return self->state;
}

BzEntry *
bz_bundle_install_dialog_get_entry (BzBundleInstallDialog *self)
{
  g_return_val_if_fail (BZ_IS_BUNDLE_INSTALL_DIALOG (self), NULL);
  return self->entry;
}

void
bz_bundle_install_dialog_set_state (BzBundleInstallDialog *self,
                                    BzStateInfo           *state)
{
  g_return_if_fail (BZ_IS_BUNDLE_INSTALL_DIALOG (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  if (state == self->state)
    return;

  g_clear_pointer (&self->state, g_object_unref);
  if (state != NULL)
    self->state = g_object_ref (state);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

void
bz_bundle_install_dialog_set_entry (BzBundleInstallDialog *self,
                                    BzEntry               *entry)
{
  BzImportance importance = 0;
  const char  *style      = NULL;

  g_return_if_fail (BZ_IS_BUNDLE_INSTALL_DIALOG (self));
  g_return_if_fail (entry == NULL || BZ_IS_ENTRY (entry));

  if (entry == self->entry)
    return;

  g_clear_pointer (&self->entry, g_object_unref);
  if (entry != NULL)
    self->entry = g_object_ref (entry);

  bz_bundle_install_dialog_update_visible_page (self);

  if (self->entry != NULL)
    {
      importance = bz_safety_calculator_calculate_rating (self->entry);
      style      = bz_safety_style_for_importance (importance);
      gtk_widget_add_css_class (GTK_WIDGET (self->safety_icon), style);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY]);
}

static DexFuture *
install_fiber (GWeakRef *wr)
{
  g_autoptr (BzBundleInstallDialog) self      = NULL;
  g_autoptr (GError) local_error              = NULL;
  g_autoptr (BzTransaction) transaction       = NULL;
  g_autoptr (BzTransactionManager) ts_manager = NULL;

  bz_weak_get_or_return_reject (self, wr);

  if (self->entry == NULL ||
      self->state == NULL)
    return dex_future_new_false ();

  transaction = bz_transaction_new_full (
      &self->entry, 1,
      NULL, 0,
      NULL, 0);

  ts_manager = g_object_ref (bz_state_info_get_transaction_manager (self->state));

  dex_await (
      bz_transaction_manager_add (
          ts_manager, transaction),
      &local_error);

  g_clear_handle_id (&self->pulse_source_id, g_source_remove);
  if (local_error != NULL)
    {
      adw_status_page_set_description (self->error_status, local_error->message);
      gtk_stack_set_visible_child_name (self->main_stack, "error");
    }
  else
    adw_carousel_scroll_to (self->carousel, self->page_finish, TRUE);

  return dex_future_new_true ();
}

/* End of bz-bundle-install-dialog.c */
