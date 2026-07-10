/* bz-user-data-tile.c
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

#include "config.h"

#include <glib/gi18n.h>

#include "bz-entry-group.h"
#include "env.h"
#include "error.h"
#include "bz-state-info.h"
#include "bz-user-data-page.h"
#include "bz-user-data-tile.h"
#include "util.h"
#include "bz-window.h"

struct _BzUserDataTile
{
  BzListTile parent_instance;

  BzEntryGroup *group;
};

G_DEFINE_FINAL_TYPE (BzUserDataTile, bz_user_data_tile, ADW_TYPE_BIN)

enum
{
  PROP_0,
  PROP_GROUP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_user_data_tile_dispose (GObject *object)
{
  BzUserDataTile *self = BZ_USER_DATA_TILE (object);

  g_clear_object (&self->group);

  G_OBJECT_CLASS (bz_user_data_tile_parent_class)->dispose (object);
}

static void
bz_user_data_tile_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzUserDataTile *self = BZ_USER_DATA_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_user_data_tile_get_group (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_user_data_tile_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzUserDataTile *self = BZ_USER_DATA_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_user_data_tile_set_group (self, g_value_get_object (value));
      break;
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
format_size (gpointer object, guint64 value)
{
  return g_format_size (value);
}

static DexFuture *
reap_user_data_done (DexFuture *future,
                     GWeakRef  *wr)
{
  g_autoptr (BzUserDataTile) self = NULL;
  g_autoptr (GError) local_error  = NULL;

  dex_future_get_value (future, &local_error);

  self = g_weak_ref_get (wr);
  if (self == NULL)
    return dex_future_new_true ();

  if (local_error != NULL)
    bz_show_error_for_widget (
        GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self))),
        _ ("Failed to Remove User Data"),
        local_error->message);
  else
    {
      g_autofree char *message = NULL;

      message = g_strdup_printf (_ ("Trashed User Data for %s"),
                                 bz_entry_group_get_title (self->group));
      bz_window_add_toast (
          BZ_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))),
          adw_toast_new (message));
    }

  return dex_future_new_true ();
}

static void
folder_cb (BzUserDataTile *self,
           GtkButton      *button)
{
  gtk_widget_activate_action (GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self))), "window.open-user-data-folder", "s", bz_entry_group_get_id (self->group));
}

static void
remove_cb (BzUserDataTile *self,
           GtkButton      *button)
{
  g_autoptr (DexFuture) future = NULL;

  if (self->group == NULL)
    return;

  future = bz_entry_group_reap_user_data (self->group);
  if (future != NULL)
    dex_future_disown (dex_future_finally (
        dex_ref (future),
        (DexFutureCallback) reap_user_data_done,
        bz_track_weak (self),
        bz_weak_release));
}

static void
bz_user_data_tile_class_init (BzUserDataTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_user_data_tile_dispose;
  object_class->get_property = bz_user_data_tile_get_property;
  object_class->set_property = bz_user_data_tile_set_property;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-user-data-tile.ui");
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, format_size);
  gtk_widget_class_bind_template_callback (widget_class, folder_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_cb);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_user_data_tile_init (BzUserDataTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_user_data_tile_new (void)
{
  return g_object_new (BZ_TYPE_USER_DATA_TILE, NULL);
}

void
bz_user_data_tile_set_group (BzUserDataTile *self,
                             BzEntryGroup   *group)
{
  g_return_if_fail (BZ_IS_USER_DATA_TILE (self));
  g_return_if_fail (group == NULL || BZ_IS_ENTRY_GROUP (group));

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

BzEntryGroup *
bz_user_data_tile_get_group (BzUserDataTile *self)
{
  g_return_val_if_fail (BZ_IS_USER_DATA_TILE (self), NULL);
  return self->group;
}
