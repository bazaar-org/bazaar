/* bz-gnome-extension-tile.c
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

#include <glib/gi18n.h>

#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-entry-group.h"
#include "bz-gnome-extension-entry.h"
#include "bz-gnome-extension-info.h"
#include "bz-gnome-extension-tile.h"
#include "bz-list-tile.h"
#include "bz-state-info.h"
#include "bz-template-callbacks.h"

struct _BzGnomeExtensionTile
{
  BzListTile parent_instance;

  BzGnomeExtensionInfo *info;
  BzEntryGroup         *group;
  char                 *target_id;
};

G_DEFINE_FINAL_TYPE (BzGnomeExtensionTile, bz_gnome_extension_tile, BZ_TYPE_LIST_TILE)

enum
{
  PROP_0,
  PROP_INFO,
  PROP_GROUP,
  PROP_STATE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
on_groups_changed (BzGnomeExtensionTile *self,
                   guint                 position,
                   guint                 removed,
                   guint                 added,
                   GListModel           *model)
{
  if (self->info == NULL || self->group != NULL || added == 0 || self->target_id == NULL)
    return;

  for (guint i = position; i < position + added; i++)
    {
      g_autoptr (BzEntryGroup) group = g_list_model_get_item (model, i);
      const char *id                 = bz_entry_group_get_id (group);

      if (g_strcmp0 (id, self->target_id) == 0)
        {
          self->group = g_steal_pointer (&group);
          g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);

          g_signal_handlers_disconnect_by_func (model, on_groups_changed, self);
          break;
        }
    }
}

static void
bz_gnome_extension_tile_dispose (GObject *object)
{
  BzGnomeExtensionTile *self = BZ_GNOME_EXTENSION_TILE (object);

  if (self->target_id != NULL)
    {
      BzStateInfo *state  = bz_state_info_get_default ();
      GListModel  *groups = state != NULL ? bz_state_info_get_all_entry_groups (state) : NULL;

      if (groups != NULL)
        g_signal_handlers_disconnect_by_func (groups, on_groups_changed, self);
    }

  g_clear_object (&self->info);
  g_clear_object (&self->group);
  g_clear_pointer (&self->target_id, g_free);

  G_OBJECT_CLASS (bz_gnome_extension_tile_parent_class)->dispose (object);
}

static void
bz_gnome_extension_tile_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  BzGnomeExtensionTile *self = BZ_GNOME_EXTENSION_TILE (object);

  switch (prop_id)
    {
    case PROP_INFO:
      g_value_set_object (value, bz_gnome_extension_tile_get_info (self));
      break;
    case PROP_GROUP:
      g_value_set_object (value, bz_gnome_extension_tile_get_group (self));
      break;
    case PROP_STATE:
      g_value_set_object (value, bz_state_info_get_default ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_gnome_extension_tile_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  BzGnomeExtensionTile *self = BZ_GNOME_EXTENSION_TILE (object);

  switch (prop_id)
    {
    case PROP_INFO:
      bz_gnome_extension_tile_set_info (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
is_outdated (GtkWidget   *widget,
             BzStateInfo *state,
             GListModel  *shell_versions)
{
  const char *shell_version = NULL;
  guint       n_items       = 0;

  if (state == NULL || shell_versions == NULL)
    return FALSE;

  shell_version = bz_state_info_get_shell_version (state);
  if (shell_version == NULL)
    return FALSE;

  n_items = g_list_model_get_n_items (shell_versions);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (GtkStringObject) obj = g_list_model_get_item (shell_versions, i);
      const char *ver                 = gtk_string_object_get_string (obj);

      if (g_strcmp0 (ver, shell_version) == 0)
        return FALSE;
    }

  return n_items > 0;
}

static void
settings_cb (BzGnomeExtensionTile *self)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.open-extension-prefs", "s",
                              bz_gnome_extension_info_get_uuid (self->info));
}

static void
remove_cb (BzGnomeExtensionTile *self)
{
  const char *id = NULL;

  if (self->group == NULL)
    return;

  id = bz_entry_group_get_id (self->group);
  if (id == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)", id, FALSE);
}

static gboolean
toggle_cb (BzGnomeExtensionTile *self,
           gboolean              state,
           GtkSwitch            *sw)
{
  gtk_widget_activate_action (GTK_WIDGET (self), "window.toggle-extension", "(sb)", bz_gnome_extension_info_get_uuid (self->info), state);
  return FALSE;
}

static void
bz_gnome_extension_tile_class_init (BzGnomeExtensionTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_gnome_extension_tile_dispose;
  object_class->get_property = bz_gnome_extension_tile_get_property;
  object_class->set_property = bz_gnome_extension_tile_set_property;

  props[PROP_INFO] =
      g_param_spec_object (
          "info",
          NULL, NULL,
          BZ_TYPE_GNOME_EXTENSION_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_GNOME_EXTENSION_INFO);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);

  gtk_widget_class_set_template_from_resource (
      widget_class,
      "/io/github/kolunmi/Bazaar/bz-gnome-extension-tile.ui");

  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_callback (widget_class, settings_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_cb);
  gtk_widget_class_bind_template_callback (widget_class, toggle_cb);
  gtk_widget_class_bind_template_callback (widget_class, is_outdated);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_gnome_extension_tile_init (BzGnomeExtensionTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_gnome_extension_tile_new (void)
{
  return g_object_new (BZ_TYPE_GNOME_EXTENSION_TILE, NULL);
}

void
bz_gnome_extension_tile_set_info (BzGnomeExtensionTile *self,
                                  BzGnomeExtensionInfo *info)
{
  g_return_if_fail (BZ_IS_GNOME_EXTENSION_TILE (self));
  g_return_if_fail (info == NULL || BZ_IS_GNOME_EXTENSION_INFO (info));

  if (self->info == info)
    return;

  if (self->target_id != NULL)
    {
      BzStateInfo *state  = bz_state_info_get_default ();
      GListModel  *groups = state != NULL ? bz_state_info_get_all_entry_groups (state) : NULL;

      if (groups != NULL)
        g_signal_handlers_disconnect_by_func (groups, on_groups_changed, self);
    }

  g_clear_object (&self->info);
  g_clear_object (&self->group);
  g_clear_pointer (&self->target_id, g_free);

  if (info != NULL)
    {
      const char *uuid = NULL;

      self->info = g_object_ref (info);
      uuid       = bz_gnome_extension_info_get_uuid (info);

      if (uuid != NULL)
        {
          BzStateInfo             *state   = bz_state_info_get_default ();
          BzApplicationMapFactory *factory = state != NULL ? bz_state_info_get_application_factory (state) : NULL;

          self->target_id = bz_gnome_extension_entry_uuid_to_id (uuid);

          if (factory != NULL)
            {
              GtkStringObject *item = gtk_string_object_new (self->target_id);
              self->group           = bz_application_map_factory_convert_one (factory, item);
            }

          if (self->group == NULL && state != NULL)
            {
              GListModel *groups = bz_state_info_get_all_entry_groups (state);

              if (groups != NULL)
                g_signal_connect_object (
                    groups, "items-changed",
                    G_CALLBACK (on_groups_changed),
                    self, G_CONNECT_SWAPPED);
            }
        }
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INFO]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

BzGnomeExtensionInfo *
bz_gnome_extension_tile_get_info (BzGnomeExtensionTile *self)
{
  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_TILE (self), NULL);
  return self->info;
}

BzEntryGroup *
bz_gnome_extension_tile_get_group (BzGnomeExtensionTile *self)
{
  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_TILE (self), NULL);
  return self->group;
}
