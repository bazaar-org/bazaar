/* bz-addon-tile.c
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

#include "bz-addon-tile.h"
#include "bz-addons-dialog.h"
#include "bz-entry-group.h"
#include "bz-state-info.h"
#include "bz-window.h"

struct _BzAddonTile
{
  BzListTile parent_instance;

  BzEntryGroup *group;
  BzResult     *parent_ui_entry;

  GtkButton *install_remove_button;
};

G_DEFINE_FINAL_TYPE (BzAddonTile, bz_addon_tile, BZ_TYPE_LIST_TILE)

enum
{
  PROP_0,

  PROP_GROUP,
  PROP_PARENT_UI_ENTRY,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
install_remove_cb (BzAddonTile *self,
                   GtkButton   *button)
{
  int removable = 0;

  if (self->group == NULL)
    return;

  removable = bz_entry_group_get_removable (self->group);

  if (removable > 0)
    gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)",
                                bz_entry_group_get_id (self->group), FALSE);
  else
    gtk_widget_activate_action (GTK_WIDGET (self), "window.install-group", "(sb)",
                                bz_entry_group_get_id (self->group), TRUE);
}

static void
bz_addon_tile_dispose (GObject *object)
{
  BzAddonTile *self = BZ_ADDON_TILE (object);

  g_clear_object (&self->group);
  g_clear_object (&self->parent_ui_entry);

  G_OBJECT_CLASS (bz_addon_tile_parent_class)->dispose (object);
}

static void
bz_addon_tile_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzAddonTile *self = BZ_ADDON_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_addon_tile_get_group (self));
      break;
    case PROP_PARENT_UI_ENTRY:
      g_value_set_object (value, self->parent_ui_entry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_addon_tile_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BzAddonTile *self = BZ_ADDON_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_addon_tile_set_group (self, g_value_get_object (value));
      break;
    case PROP_PARENT_UI_ENTRY:
      g_clear_object (&self->parent_ui_entry);
      self->parent_ui_entry = g_value_dup_object (value);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARENT_UI_ENTRY]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
is_zero (gpointer object,
         int      value)
{
  return value == 0;
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
is_empty (gpointer    object,
          const char *str)
{
  return str == NULL || str[0] == '\0';
}

static gboolean
logical_and (gpointer object,
             gboolean value1,
             gboolean value2)
{
  return value1 && value2;
}

static char *
get_install_remove_tooltip (gpointer object,
                            int      removable)
{
  if (removable > 0)
    return g_strdup (_ ("Uninstall"));
  else
    return g_strdup (_ ("Install"));
}

static char *
get_install_remove_icon (gpointer object,
                         int      removable)
{
  if (removable > 0)
    return g_strdup ("user-trash-symbolic");
  else
    return g_strdup ("document-save-symbolic");
}

static gboolean
switch_bool (gpointer object,
             gboolean condition,
             gboolean true_value,
             gboolean false_value)
{
  return condition ? true_value : false_value;
}

static void
bz_addon_tile_realize (GtkWidget *widget)
{
  BzAddonTile *self          = BZ_ADDON_TILE (widget);
  GtkWidget   *parent        = widget;
  GtkWidget   *addons_dialog = NULL;

  GTK_WIDGET_CLASS (bz_addon_tile_parent_class)->realize (widget);

  while ((parent = gtk_widget_get_parent (parent)) != NULL)
    {
      if (BZ_IS_ADDONS_DIALOG (parent))
        {
          addons_dialog = parent;
          break;
        }
    }

  if (addons_dialog == NULL)
    return;

  g_object_bind_property (
      addons_dialog, "parent-ui-entry",
      self,          "parent-ui-entry",
      G_BINDING_SYNC_CREATE);
}

static void
bz_addon_tile_class_init (BzAddonTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_addon_tile_dispose;
  object_class->get_property = bz_addon_tile_get_property;
  object_class->set_property = bz_addon_tile_set_property;

  widget_class->realize = bz_addon_tile_realize;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PARENT_UI_ENTRY] =
      g_param_spec_object (
          "parent-ui-entry",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-addon-tile.ui");
  gtk_widget_class_bind_template_child (widget_class, BzAddonTile, install_remove_button);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, logical_and);
  gtk_widget_class_bind_template_callback (widget_class, is_empty);
  gtk_widget_class_bind_template_callback (widget_class, get_install_remove_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_install_remove_icon);
  gtk_widget_class_bind_template_callback (widget_class, switch_bool);
  gtk_widget_class_bind_template_callback (widget_class, install_remove_cb);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_addon_tile_init (BzAddonTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_addon_tile_new (void)
{
  return g_object_new (BZ_TYPE_ADDON_TILE, NULL);
}

void
bz_addon_tile_set_group (BzAddonTile  *self,
                         BzEntryGroup *group)
{
  g_return_if_fail (BZ_IS_ADDON_TILE (self));
  g_return_if_fail (group == NULL || BZ_IS_ENTRY_GROUP (group));

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

BzEntryGroup *
bz_addon_tile_get_group (BzAddonTile *self)
{
  g_return_val_if_fail (BZ_IS_ADDON_TILE (self), NULL);
  return self->group;
}
