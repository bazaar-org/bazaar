/* bz-transact-icon.c
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

#include <bge.h>

#include "bz-transact-icon.h"
#include "progress-bar-designs/common.h"

struct _BzTransactIcon
{
  AdwBin parent_instance;

  BzTransactIconInfo *info;

  GSettings                 *settings;
  BzTransactionManager      *ts_manager;
  GListModel                *trackers;
  BzTransactionEntryTracker *tracker;

  char *pride_class;

  BgeWdgtRenderer *wdgt;
};

G_DEFINE_FINAL_TYPE (BzTransactIcon, bz_transact_icon, ADW_TYPE_BIN);

enum
{
  PROP_0,

  PROP_INFO,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
apply_state (BzTransactIcon *self);

static void
update_tracker (BzTransactIcon *self);

static void
check_tracker (BzTransactIcon *self);

static void
ensure_draw_css (BzTransactIcon *self);

static void
info_state_notify (BzTransactIcon     *self,
                   GParamSpec         *pspec,
                   BzTransactIconInfo *info);

static void
info_group_notify (BzTransactIcon     *self,
                   GParamSpec         *pspec,
                   BzTransactIconInfo *info);

static void
trackers_items_changed (BzTransactIcon *self,
                        guint           position,
                        guint           removed,
                        guint           added,
                        GListModel     *model);

static void
tracker_notify (BzTransactIcon            *self,
                GParamSpec                *pspec,
                BzTransactionEntryTracker *tracker);

static void
pride_flag_changed (BzTransactIcon *self,
                    const char     *key,
                    GSettings      *settings);

static void
bz_transact_icon_dispose (GObject *object)
{
  BzTransactIcon *self = BZ_TRANSACT_ICON (object);

  if (self->info != NULL)
    {
      g_signal_handlers_disconnect_by_func (
          self->info, info_state_notify, self);
      g_signal_handlers_disconnect_by_func (
          self->info, info_group_notify, self);
    }
  g_clear_pointer (&self->info, g_object_unref);

  if (self->trackers != NULL)
    g_signal_handlers_disconnect_by_func (
        self->trackers, trackers_items_changed, self);
  if (self->tracker != NULL)
    g_signal_handlers_disconnect_by_func (
        self->tracker, tracker_notify, self);
  if (self->settings != NULL)
    g_signal_handlers_disconnect_by_func (
        self->settings, pride_flag_changed, self);
  g_clear_object (&self->settings);
  g_clear_object (&self->ts_manager);
  g_clear_object (&self->trackers);
  g_clear_object (&self->tracker);

  g_clear_pointer (&self->pride_class, g_free);

  G_OBJECT_CLASS (bz_transact_icon_parent_class)->dispose (object);
}

static void
bz_transact_icon_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzTransactIcon *self = BZ_TRANSACT_ICON (object);

  switch (prop_id)
    {
    case PROP_INFO:
      g_value_set_object (value, bz_transact_icon_get_info (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_transact_icon_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzTransactIcon *self = BZ_TRANSACT_ICON (object);

  switch (prop_id)
    {
    case PROP_INFO:
      bz_transact_icon_set_info (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_transact_icon_class_init (BzTransactIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_transact_icon_set_property;
  object_class->get_property = bz_transact_icon_get_property;
  object_class->dispose      = bz_transact_icon_dispose;

  props[PROP_INFO] =
      g_param_spec_object (
          "info",
          NULL, NULL,
          BZ_TYPE_TRANSACT_ICON_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_transact_icon_init (BzTransactIcon *self)
{
  self->wdgt = bge_wdgt_renderer_new ();
  g_object_set (
      self->wdgt,
      "resource", "/io/github/kolunmi/Bazaar/bz-transact-icon.wdgt",
      "state", "inactive",
      NULL);
  g_object_bind_property (self, "info", self->wdgt, "reference", G_BINDING_DEFAULT);
  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->wdgt));
}

BzTransactIcon *
bz_transact_icon_new (void)
{
  return g_object_new (BZ_TYPE_TRANSACT_ICON, NULL);
}

BzTransactIconInfo *
bz_transact_icon_get_info (BzTransactIcon *self)
{
  g_return_val_if_fail (BZ_IS_TRANSACT_ICON (self), NULL);
  return self->info;
}

void
bz_transact_icon_set_info (BzTransactIcon     *self,
                           BzTransactIconInfo *info)
{
  g_return_if_fail (BZ_IS_TRANSACT_ICON (self));
  g_return_if_fail (info == NULL || BZ_IS_TRANSACT_ICON_INFO (info));

  if (info == self->info)
    return;

  if (self->info != NULL)
    {
      g_signal_handlers_disconnect_by_func (
          self->info, info_state_notify, self);
      g_signal_handlers_disconnect_by_func (
          self->info, info_group_notify, self);
    }

  g_clear_pointer (&self->info, g_object_unref);
  if (info != NULL)
    {
      self->info = g_object_ref (info);
      g_signal_connect_swapped (self->info, "notify::state", G_CALLBACK (info_state_notify), self);
      g_signal_connect_swapped (self->info, "notify::group", G_CALLBACK (info_group_notify), self);
    }

  apply_state (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INFO]);
}

static void
apply_state (BzTransactIcon *self)
{
  if (self->trackers != NULL)
    g_signal_handlers_disconnect_by_func (
        self->trackers, trackers_items_changed, self);
  if (self->tracker != NULL)
    g_signal_handlers_disconnect_by_func (
        self->tracker, tracker_notify, self);
  if (self->settings != NULL)
    g_signal_handlers_disconnect_by_func (
        self->settings, pride_flag_changed, self);
  g_clear_object (&self->settings);
  g_clear_object (&self->ts_manager);
  g_clear_object (&self->trackers);
  g_clear_object (&self->tracker);

  if (self->info != NULL)
    {
      BzStateInfo *state = NULL;

      state = bz_transact_icon_info_get_state (self->info);
      if (state != NULL)
        {
          g_object_get (
              state,
              "transaction-manager", &self->ts_manager,
              "settings", &self->settings,
              NULL);
          if (self->ts_manager != NULL)
            {
              g_object_get (
                  self->ts_manager,
                  "all-trackers", &self->trackers,
                  NULL);
              if (self->trackers != NULL)
                g_signal_connect_swapped (
                    self->trackers, "items-changed",
                    G_CALLBACK (trackers_items_changed), self);
            }
          if (self->settings != NULL)
            {
              g_signal_connect_swapped (
                  self->settings, "changed::global-progress-bar-theme",
                  G_CALLBACK (pride_flag_changed), self);
              g_signal_connect_swapped (
                  self->settings, "changed::rotate-flag",
                  G_CALLBACK (pride_flag_changed), self);
            }
        }
    }

  update_tracker (self);
  ensure_draw_css (self);
}

static void
update_tracker (BzTransactIcon *self)
{
  g_autoptr (BzTransactionEntryTracker) found = NULL;

  if (self->trackers != NULL)
    {
      BzEntryGroup *group = NULL;

      group = bz_transact_icon_info_get_group (self->info);
      if (group != NULL)
        {
          const char *id         = NULL;
          guint       n_trackers = 0;

          id         = bz_entry_group_get_id (group);
          n_trackers = g_list_model_get_n_items (self->trackers);

          for (guint i = 0; i < n_trackers; i++)
            {
              g_autoptr (BzTransactionEntryTracker) tracker = NULL;
              BzEntry *entry                                = NULL;

              tracker = g_list_model_get_item (self->trackers, i);
              entry   = bz_transaction_entry_tracker_get_entry (tracker);
              if (entry == NULL)
                continue;

              if (g_strcmp0 (bz_entry_get_id (entry), id) == 0)
                {
                  found = g_steal_pointer (&tracker);
                  break;
                }
            }
        }
    }

  bz_transact_icon_info_set_tracker (self->info, found);
  if (found != self->tracker)
    {
      if (self->tracker != NULL)
        g_signal_handlers_disconnect_by_func (
            self->tracker, tracker_notify, self);
      g_clear_object (&self->tracker);

      if (found != NULL)
        {
          self->tracker = g_object_ref (found);
          g_signal_connect_swapped (self->tracker, "notify::active", G_CALLBACK (tracker_notify), self);
          g_signal_connect_swapped (self->tracker, "notify::pending", G_CALLBACK (tracker_notify), self);
          g_signal_connect_swapped (self->tracker, "notify::status", G_CALLBACK (tracker_notify), self);
        }

      check_tracker (self);
    }
}

static void
check_tracker (BzTransactIcon *self)
{
  const char *state = NULL;

  if (self->tracker != NULL)
    {
      gboolean                 active  = FALSE;
      gboolean                 pending = FALSE;
      BzTransactionEntryStatus status  = 0;

      active  = bz_transaction_entry_tracker_get_active (self->tracker);
      pending = bz_transaction_entry_tracker_get_pending (self->tracker);
      status  = bz_transaction_entry_tracker_get_status (self->tracker);

      if ((active || pending) && status == BZ_TRANSACTION_ENTRY_STATUS_CANCELLED)
        state = "cancelling";
      else if (pending || status == BZ_TRANSACTION_ENTRY_STATUS_QUEUED)
        state = "pending";
      else if (!active || status == BZ_TRANSACTION_ENTRY_STATUS_DONE)
        state = "inactive";
      else
        state = "fraction";
    }
  else
    state = "inactive";

  bge_wdgt_renderer_set_state (self->wdgt, state);
}

static void
ensure_draw_css (BzTransactIcon *self)
{
  g_autoptr (GtkWidget) widget = NULL;
  g_autofree char *id          = NULL;
  g_autofree char *final_id    = NULL;
  g_autofree char *class       = NULL;
  gboolean         rotate      = FALSE;

  widget = bge_wdgt_renderer_lookup_object (self->wdgt, "flag");

  if (self->settings == NULL)
    {
      if (self->pride_class != NULL)
        gtk_widget_remove_css_class (widget, self->pride_class);
      g_clear_pointer (&self->pride_class, g_free);
      return;
    }

  id     = g_settings_get_string (self->settings, "global-progress-bar-theme");
  rotate = g_settings_get_boolean (self->settings, "rotate-flag");

  if (rotate && g_strcmp0 (id, "accent-color") != 0)
    final_id = g_strdup_printf ("%s-horizontal", id);
  else
    final_id = g_strdup (id);

  class = bz_dup_css_class_for_pride_id (final_id);

  if (self->pride_class != NULL &&
      g_strcmp0 (self->pride_class, class) == 0)
    return;

  if (self->pride_class != NULL)
    gtk_widget_remove_css_class (widget, self->pride_class);
  g_clear_pointer (&self->pride_class, g_free);
  gtk_widget_add_css_class (widget, class);
  self->pride_class = g_steal_pointer (&class);
}

static void
info_state_notify (BzTransactIcon     *self,
                   GParamSpec         *pspec,
                   BzTransactIconInfo *info)
{
  apply_state (self);
}

static void
info_group_notify (BzTransactIcon     *self,
                   GParamSpec         *pspec,
                   BzTransactIconInfo *info)
{
  update_tracker (self);
}

static void
trackers_items_changed (BzTransactIcon *self,
                        guint           position,
                        guint           removed,
                        guint           added,
                        GListModel     *model)
{
  update_tracker (self);
}

static void
tracker_notify (BzTransactIcon            *self,
                GParamSpec                *pspec,
                BzTransactionEntryTracker *tracker)
{
  check_tracker (self);
}

static void
pride_flag_changed (BzTransactIcon *self,
                    const char     *key,
                    GSettings      *settings)
{
  ensure_draw_css (self);
}

/* End of bz-transact-icon.c */
