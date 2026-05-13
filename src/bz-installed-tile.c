/* bz-installed-tile.c
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

#include <glib/gi18n.h>

#include "bz-addons-dialog.h"
#include "bz-entry-group-util.h"
#include "bz-entry-group.h"
#include "bz-env.h"
#include "bz-error.h"
#include "bz-installed-tile.h"
#include "bz-library-page.h"
#include "bz-state-info.h"
#include "bz-transact-icon.h"
#include "bz-transact-icon-info.h"
#include "bz-window.h"

struct _BzInstalledTile
{
  BzListTile parent_instance;

  BzEntryGroup *group;

  GtkLabel   *title_label;
  GtkButton  *support_button;
  GtkButton  *remove_button;
};

G_DEFINE_FINAL_TYPE (BzInstalledTile, bz_installed_tile, BZ_TYPE_LIST_TILE)

enum
{
  PROP_0,
  PROP_GROUP,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_installed_tile_dispose (GObject *object)
{
  BzInstalledTile *self = BZ_INSTALLED_TILE (object);

  g_clear_object (&self->group);

  G_OBJECT_CLASS (bz_installed_tile_parent_class)->dispose (object);
}

static void
bz_installed_tile_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzInstalledTile *self = BZ_INSTALLED_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_installed_tile_get_group (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_installed_tile_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzInstalledTile *self = BZ_INSTALLED_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_installed_tile_set_group (self, g_value_get_object (value));
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

static gboolean
logical_and (gpointer object,
             gboolean a,
             gboolean b)
{
  return a && b;
}

static gboolean
logical_or (gpointer object,
            gboolean a,
            gboolean b)
{
  return a || b;
}

static char *
format_description (gpointer    object,
                    guint64     size,
                    GListModel *versions)
{
  g_autoptr (GString) result       = NULL;
  g_autoptr (GString) versions_str = NULL;
  g_autofree char *size_str        = NULL;
  guint            n_versions      = 0;

  result = g_string_new (NULL);

  if (versions != NULL)
    n_versions = g_list_model_get_n_items (versions);

  if (n_versions > 0)
    {
      versions_str = g_string_new (NULL);

      for (guint i = 0; i < n_versions; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;
          const char *version                = NULL;

          string  = g_list_model_get_item (versions, i);
          version = gtk_string_object_get_string (string);

          if (version != NULL && *version != '\0')
            {
              if (versions_str->len > 0)
                g_string_append_c (versions_str, ' ');
              g_string_append (versions_str, version);
            }
        }

      if (versions_str->len > 0)
        {
          g_string_append (result, versions_str->str);
          g_string_append (result, " • ");
        }
    }

  size_str = g_format_size (size);
  g_string_append (result, size_str);

  return g_string_free (g_steal_pointer (&result), FALSE);
}

static void
support_cb (BzInstalledTile *self,
            GtkButton       *button)
{
  const char *url = NULL;

  if (self->group == NULL)
    return;

  url = bz_entry_group_get_donation_url (self->group);
  if (url == NULL)
    return;

  g_app_info_launch_default_for_uri (url, NULL, NULL);
}

static void
permissions_cb (BzInstalledTile *self)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.permissions", "s",
                              bz_entry_group_get_id (self->group));
}

static void
install_addons_cb (BzInstalledTile *self)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.addons-group", "s",
                              bz_entry_group_get_id (self->group));
}

static void
remove_cb (BzInstalledTile *self,
           GtkButton       *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.remove-group", "(sb)",
                              bz_entry_group_get_id (self->group), FALSE);
}

static void
bz_installed_tile_class_init (BzInstalledTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_installed_tile_dispose;
  object_class->get_property = bz_installed_tile_get_property;
  object_class->set_property = bz_installed_tile_set_property;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_TRANSACT_ICON);
  g_type_ensure (BZ_TYPE_TRANSACT_ICON_INFO);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-installed-tile.ui");
  gtk_widget_class_bind_template_child (widget_class, BzInstalledTile, title_label);
  gtk_widget_class_bind_template_child (widget_class, BzInstalledTile, support_button);
  gtk_widget_class_bind_template_child (widget_class, BzInstalledTile, remove_button);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, logical_and);
  gtk_widget_class_bind_template_callback (widget_class, logical_or);
  gtk_widget_class_bind_template_callback (widget_class, format_description);
  gtk_widget_class_bind_template_callback (widget_class, support_cb);
  gtk_widget_class_bind_template_callback (widget_class, install_addons_cb);
  gtk_widget_class_bind_template_callback (widget_class, remove_cb);
  gtk_widget_class_bind_template_callback (widget_class, permissions_cb);

  gtk_widget_class_install_action (widget_class, "installed-tile.install-addons", NULL,
                                 (GtkWidgetActionActivateFunc) install_addons_cb);
  gtk_widget_class_install_action (widget_class, "installed-tile.permissions", NULL,
                                   (GtkWidgetActionActivateFunc) permissions_cb);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_installed_tile_init (BzInstalledTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_installed_tile_new (void)
{
  return g_object_new (BZ_TYPE_INSTALLED_TILE, NULL);
}

void
bz_installed_tile_set_group (BzInstalledTile *self,
                             BzEntryGroup    *group)
{
  g_return_if_fail (BZ_IS_INSTALLED_TILE (self));
  g_return_if_fail (group == NULL || BZ_IS_ENTRY_GROUP (group));

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

BzEntryGroup *
bz_installed_tile_get_group (BzInstalledTile *self)
{
  g_return_val_if_fail (BZ_IS_INSTALLED_TILE (self), NULL);
  return self->group;
}
