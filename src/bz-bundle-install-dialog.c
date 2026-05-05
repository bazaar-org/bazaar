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

#include "bz-bundle-install-dialog.h"
#include "bz-env.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"

struct _BzBundleInstallDialog
{
  AdwBreakpointBin parent_instance;

  BzStateInfo *state;
  BzEntry     *entry;

  GtkStack    *main_stack;
  AdwCarousel *carousel;
  GtkWidget   *page_info;
  GtkWidget   *page_progress;
  GtkWidget   *page_finish;

  GtkButton      *install_btn;
  GtkProgressBar *progress_bar;
  AdwStatusPage  *error_status;

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

static void
install_cb (BzBundleInstallDialog *self,
            GtkButton             *button)
{
  if (self->entry == NULL ||
      self->state == NULL)
    return;

  gtk_widget_set_sensitive (GTK_WIDGET (self->install_btn), FALSE);

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

  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, main_stack);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, carousel);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_info);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_progress);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, page_finish);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, install_btn);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, BzBundleInstallDialog, error_status);
  gtk_widget_class_bind_template_callback (widget_class, install_cb);
  gtk_widget_class_bind_template_callback (widget_class, run_cb);
  gtk_widget_class_bind_template_callback (widget_class, show_cb);
}

static void
bz_bundle_install_dialog_init (BzBundleInstallDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
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
  g_return_if_fail (BZ_IS_BUNDLE_INSTALL_DIALOG (self));
  g_return_if_fail (entry == NULL || BZ_IS_ENTRY (entry));

  if (entry == self->entry)
    return;

  g_clear_pointer (&self->entry, g_object_unref);
  if (entry != NULL)
    self->entry = g_object_ref (entry);

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
