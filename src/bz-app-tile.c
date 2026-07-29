/* bz-app-tile.c
 *
 * Copyright 2025 Adam Masciola
 *
 * Layout manager adapted from GNOME Softwares GsSummaryTileLayout
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
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

#include "bz-app-tile.h"

#define BZ_APP_TILE_PREFERRED_WIDTH 270

#define BZ_TYPE_APP_TILE_LAYOUT (bz_app_tile_layout_get_type ())
G_DECLARE_FINAL_TYPE (BzAppTileLayout, bz_app_tile_layout, BZ, APP_TILE_LAYOUT, GtkLayoutManager)

struct _BzAppTileLayout
{
  GtkLayoutManager parent_instance;
};

G_DEFINE_FINAL_TYPE (BzAppTileLayout, bz_app_tile_layout, GTK_TYPE_LAYOUT_MANAGER)

static void
bz_app_tile_layout_measure (GtkLayoutManager *layout_manager,
                            GtkWidget        *widget,
                            GtkOrientation    orientation,
                            gint              for_size,
                            gint             *minimum,
                            gint             *natural,
                            gint             *minimum_baseline,
                            gint             *natural_baseline)
{
  GtkWidget *child = NULL;
  gint       min   = 0;
  gint       nat   = 0;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      gint child_min_baseline = -1;
      gint child_nat_baseline = -1;
      gint child_min          = 0;
      gint child_nat          = 0;

      if (!gtk_widget_should_layout (child))
        continue;

      gtk_widget_measure (child, orientation,
                          for_size,
                          &child_min, &child_nat,
                          &child_min_baseline,
                          &child_nat_baseline);

      min = MAX (min, child_min);
      nat = MAX (nat, child_nat);

      if (child_min_baseline > -1)
        *minimum_baseline = MAX (*minimum_baseline, child_min_baseline);
      if (child_nat_baseline > -1)
        *natural_baseline = MAX (*natural_baseline, child_nat_baseline);
    }

  *minimum = min;
  *natural = nat;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    *natural = MAX (*minimum, BZ_APP_TILE_PREFERRED_WIDTH);
}

static void
bz_app_tile_layout_allocate (GtkLayoutManager *layout_manager,
                             GtkWidget        *widget,
                             gint              width,
                             gint              height,
                             gint              baseline)
{
  GtkWidget *child = NULL;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (child && gtk_widget_should_layout (child))
        gtk_widget_allocate (child, width, height, baseline, NULL);
    }
}

static void
bz_app_tile_layout_class_init (BzAppTileLayoutClass *klass)
{
  GtkLayoutManagerClass *layout_manager_class = GTK_LAYOUT_MANAGER_CLASS (klass);
  layout_manager_class->measure               = bz_app_tile_layout_measure;
  layout_manager_class->allocate              = bz_app_tile_layout_allocate;
}

static void
bz_app_tile_layout_init (BzAppTileLayout *self)
{
}

struct _BzAppTile
{
  GtkButton parent_instance;

  BzEntryGroup *group;
  GdkPaintable *icon_override;
};

G_DEFINE_FINAL_TYPE (BzAppTile, bz_app_tile, GTK_TYPE_BUTTON);

enum
{
  PROP_0,

  PROP_GROUP,
  PROP_ICON_OVERRIDE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_app_tile_dispose (GObject *object)
{
  BzAppTile *self = BZ_APP_TILE (object);

  g_clear_object (&self->group);
  g_clear_object (&self->icon_override);

  G_OBJECT_CLASS (bz_app_tile_parent_class)->dispose (object);
}

static void
bz_app_tile_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BzAppTile *self = BZ_APP_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_app_tile_get_group (self));
      break;
    case PROP_ICON_OVERRIDE:
      g_value_set_object (value, bz_app_tile_get_icon_override (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_app_tile_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BzAppTile *self = BZ_APP_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_app_tile_set_group (self, g_value_get_object (value));
      break;
    case PROP_ICON_OVERRIDE:
      bz_app_tile_set_icon_override (self, g_value_get_object (value));
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
description_line_amount (gpointer object,
                         bool     value)
{
  return value ? 2 : 1;
}

static void
bz_app_tile_class_init (BzAppTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_app_tile_set_property;
  object_class->get_property = bz_app_tile_get_property;
  object_class->dispose      = bz_app_tile_dispose;

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ICON_OVERRIDE] =
      g_param_spec_object (
          "icon-override",
          NULL, NULL,
          GDK_TYPE_PAINTABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-app-tile.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, BZ_TYPE_APP_TILE_LAYOUT);

  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, is_zero);
  gtk_widget_class_bind_template_callback (widget_class, description_line_amount);
}

static void
bz_app_tile_init (BzAppTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_app_tile_new (void)
{
  return g_object_new (BZ_TYPE_APP_TILE, NULL);
}

BzEntryGroup *
bz_app_tile_get_group (BzAppTile *self)
{
  g_return_val_if_fail (BZ_IS_APP_TILE (self), NULL);
  return self->group;
}

void
bz_app_tile_set_group (BzAppTile    *self,
                       BzEntryGroup *group)
{
  g_return_if_fail (BZ_IS_APP_TILE (self));

  g_clear_object (&self->group);
  if (group != NULL)
    self->group = g_object_ref (group);

  if (group != NULL)
    {
      gtk_actionable_set_action_target (GTK_ACTIONABLE (self), "s", bz_entry_group_get_id (group));
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self), "window.show-group");
    }
  else
    gtk_actionable_set_action_name (GTK_ACTIONABLE (self), NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

GdkPaintable *
bz_app_tile_get_icon_override (BzAppTile *self)
{
  g_return_val_if_fail (BZ_IS_APP_TILE (self), NULL);
  return self->icon_override;
}

void
bz_app_tile_set_icon_override (BzAppTile    *self,
                               GdkPaintable *icon_override)
{
  g_return_if_fail (BZ_IS_APP_TILE (self));

  g_clear_object (&self->icon_override);
  if (icon_override != NULL)
    self->icon_override = g_object_ref (icon_override);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_OVERRIDE]);
}

/* End of bz-app-tile.c */
