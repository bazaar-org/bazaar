/* bz-rich-app-tile.c
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

#include "bz-rich-app-tile.h"
#include "bz-application.h"
#include "bz-entry.h"
#include "bz-rounded-picture.h"
#include "bz-themed-entry-group-rect.h"
#include "bz-transact-icon.h"
#include "bz-util.h"

#include <glib/gi18n.h>

struct _BzRichAppTile
{
  BzListTile    parent_instance;
  BzEntryGroup *group;
  BzEntry      *ui_entry;
  DexFuture    *ui_entry_resolve;
  gboolean      removable_at_start;

  BzTransactionEntryTracker *tracker;
  GListModel                *all_trackers;

  GtkWidget          *picture_box;
  BzTransactIconInfo *transact_icon_info;
};

G_DEFINE_FINAL_TYPE (BzRichAppTile, bz_rich_app_tile, BZ_TYPE_LIST_TILE);

enum
{
  PROP_0,
  PROP_GROUP,
  PROP_UI_ENTRY,
  PROP_REMOVABLE_AT_START,
  PROP_TRACKER,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void update_ui_entry (BzRichAppTile *self);

static void
update_tracker (BzRichAppTile *self)
{
  BzTransactionManager *manager               = NULL;
  g_autoptr (GListModel) all                  = NULL;
  const char *group_id                        = NULL;
  g_autoptr (BzTransactionEntryTracker) found = NULL;

  manager = bz_state_info_get_transaction_manager (bz_state_info_get_default ());
  if (manager != NULL)
    g_object_get (manager, "all-trackers", &all, NULL);
  if (self->group != NULL)
    group_id = bz_entry_group_get_id (self->group);

  if (all != NULL && group_id != NULL)
    {
      for (guint i = 0; i < g_list_model_get_n_items (all); i++)
        {
          g_autoptr (BzTransactionEntryTracker) tracker = NULL;
          BzEntry *entry                                = NULL;

          tracker = g_list_model_get_item (all, i);
          entry   = bz_transaction_entry_tracker_get_entry (tracker);

          if (g_strcmp0 (entry != NULL ? bz_entry_get_id (entry) : NULL, group_id) == 0)
            {
              found = g_steal_pointer (&tracker);
              break;
            }
        }
    }

  if (g_set_object (&self->tracker, found))
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TRACKER]);
}

static void
on_all_trackers_changed (GListModel    *model,
                         guint          position,
                         guint          removed,
                         guint          added,
                         BzRichAppTile *self)
{
  update_tracker (self);
}

static DexFuture *
ui_entry_resolved_finally (DexFuture *future,
                           GWeakRef  *wr)
{
  g_autoptr (BzRichAppTile) self = NULL;
  const GValue *value            = NULL;

  bz_weak_get_or_return_reject (self, wr);
  value = dex_future_get_value (future, NULL);
  if (value != NULL)
    {
      BzEntry *ui_entry = g_value_get_object (value);
      g_set_object (&self->ui_entry, ui_entry);
    }
  else
    {
      g_clear_object (&self->ui_entry);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UI_ENTRY]);

  return NULL;
}

static void
update_ui_entry (BzRichAppTile *self)
{
  g_autoptr (BzResult) ui_entry_result = NULL;

  dex_clear (&self->ui_entry_resolve);

  if (self->ui_entry != NULL)
    {
      g_clear_object (&self->ui_entry);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UI_ENTRY]);
    }

  ui_entry_result        = bz_entry_group_dup_ui_entry (self->group);
  self->ui_entry_resolve = dex_future_finally (
      bz_result_dup_future (ui_entry_result),
      (DexFutureCallback) ui_entry_resolved_finally,
      bz_track_weak (self),
      bz_weak_release);
}

static void
bz_rich_app_tile_dispose (GObject *object)
{
  BzRichAppTile *self = BZ_RICH_APP_TILE (object);

  if (self->all_trackers != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->all_trackers, on_all_trackers_changed, self);
      g_clear_object (&self->all_trackers);
    }

  g_clear_object (&self->group);
  g_clear_object (&self->ui_entry);
  g_clear_object (&self->tracker);
  dex_clear (&self->ui_entry_resolve);

  G_OBJECT_CLASS (bz_rich_app_tile_parent_class)->dispose (object);
}

static void
bz_rich_app_tile_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzRichAppTile *self = BZ_RICH_APP_TILE (object);
  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_rich_app_tile_get_group (self));
      break;
    case PROP_UI_ENTRY:
      g_value_set_object (value, self->ui_entry);
      break;
    case PROP_REMOVABLE_AT_START:
      g_value_set_boolean (value, self->removable_at_start);
      break;
    case PROP_TRACKER:
      g_value_set_object (value, self->tracker);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_rich_app_tile_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzRichAppTile *self = BZ_RICH_APP_TILE (object);
  switch (prop_id)
    {
    case PROP_GROUP:
      bz_rich_app_tile_set_group (self, g_value_get_object (value));
      break;
    case PROP_UI_ENTRY:
    case PROP_REMOVABLE_AT_START:
    case PROP_TRACKER:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

static gboolean
is_zero (gpointer object,
         int      value)
{
  return value == 0;
}

static char *
get_visible_page (gpointer                   object,
                  int                        installable,
                  int                        removable,
                  gboolean                   removable_at_start,
                  BzTransactionEntryTracker *tracker,
                  BzTransactionEntryStatus   status)
{
  if (tracker != NULL &&
      (bz_transaction_entry_tracker_get_active (tracker) ||
       bz_transaction_entry_tracker_get_pending (tracker)) &&
      status != BZ_TRANSACTION_ENTRY_STATUS_CANCELLED &&
      status != BZ_TRANSACTION_ENTRY_STATUS_DONE)
    return g_strdup ("cancel");

  if (removable > 0)
    return g_strdup (removable_at_start ? "uninstall" : "open");

  if (installable > 0)
    return g_strdup ("get");

  return g_strdup ("empty");
}

static void
install_button_clicked_cb (BzRichAppTile *self,
                           GtkButton     *button)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.install-group", "(sb)",
                              bz_entry_group_get_id (self->group), TRUE);
}

static void
remove_button_clicked_cb (BzRichAppTile *self,
                          GtkButton     *button)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)",
                              bz_entry_group_get_id (self->group), FALSE);
}

static void
run_button_clicked_cb (BzRichAppTile *self,
                       GtkButton     *button)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.launch-group", "s",
                              bz_entry_group_get_id (self->group));
}

static void
cancel_button_clicked_cb (BzRichAppTile *self,
                          GtkButton     *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.cancel-group", "s",
                              bz_entry_group_get_id (self->group));
}

static void
bz_rich_app_tile_class_init (BzRichAppTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_rich_app_tile_set_property;
  object_class->get_property = bz_rich_app_tile_get_property;
  object_class->dispose      = bz_rich_app_tile_dispose;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_UI_ENTRY] =
      g_param_spec_object (
          "ui-entry",
          NULL, NULL,
          BZ_TYPE_ENTRY,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_REMOVABLE_AT_START] =
      g_param_spec_boolean (
          "removable-at-start",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_TRACKER] =
      g_param_spec_object (
          "tracker",
          NULL, NULL,
          BZ_TYPE_TRANSACTION_ENTRY_TRACKER,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_TRANSACT_ICON);
  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ROUNDED_PICTURE);
  g_type_ensure (BZ_TYPE_THEMED_ENTRY_GROUP_RECT);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-rich-app-tile.ui");
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, get_visible_page);
  gtk_widget_class_bind_template_callback (widget_class, install_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, run_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, cancel_button_clicked_cb);
  gtk_widget_class_bind_template_child (widget_class, BzRichAppTile, picture_box);
  gtk_widget_class_bind_template_child (widget_class, BzRichAppTile, transact_icon_info);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_rich_app_tile_init (BzRichAppTile *self)
{
  BzStateInfo          *state   = NULL;
  BzTransactionManager *manager = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  state   = bz_state_info_get_default ();
  manager = bz_state_info_get_transaction_manager (state);
  if (manager != NULL)
    g_object_get (manager, "all-trackers", &self->all_trackers, NULL);

  if (self->all_trackers != NULL)
    g_signal_connect (self->all_trackers, "items-changed",
                      G_CALLBACK (on_all_trackers_changed), self);

  update_tracker (self);
}

GtkWidget *
bz_rich_app_tile_new (void)
{
  return g_object_new (BZ_TYPE_RICH_APP_TILE, NULL);
}

BzEntryGroup *
bz_rich_app_tile_get_group (BzRichAppTile *self)
{
  g_return_val_if_fail (BZ_IS_RICH_APP_TILE (self), NULL);
  return self->group;
}

void
bz_rich_app_tile_set_group (BzRichAppTile *self,
                            BzEntryGroup  *group)
{
  const char      *title              = NULL;
  gboolean         verified           = FALSE;
  gboolean         removable_at_start = FALSE;
  g_autofree char *label              = NULL;

  g_return_if_fail (BZ_IS_RICH_APP_TILE (self));

  g_clear_object (&self->group);

  if (group != NULL)
    {
      self->group = g_object_ref (group);

      title    = bz_entry_group_get_title (self->group);
      verified = bz_entry_group_get_is_verified (self->group);

      removable_at_start = bz_entry_group_get_removable (self->group) != 0;
      if (self->removable_at_start != removable_at_start)
        {
          self->removable_at_start = removable_at_start;
          g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REMOVABLE_AT_START]);
        }

      if (verified)
        {
          label = g_strdup_printf ("%s, %s", title, _ ("Verified"));
          gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                          GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                          -1);
        }
      else
        {
          gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                          GTK_ACCESSIBLE_PROPERTY_LABEL, title,
                                          -1);
        }
    }

  update_ui_entry (self);
  update_tracker (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}
/* End of bz-rich-app-tile.c */
