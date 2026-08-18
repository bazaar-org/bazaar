/* bz-group-tile-css-watcher.c
 *
 * Copyright 2025 Adam Masciola
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

#include <adwaita.h>

#include "bz-group-tile-css-watcher.h"

#define LUMINANCE_THRESHOLD 130.0

struct _BzGroupTileCssWatcher
{
  GObject parent_instance;

  GWeakRef      widget;
  BzEntryGroup *group;

  GtkCssProvider *css;
  char           *light_class;
  char           *dark_class;
  char           *light_text_class;
  char           *dark_text_class;
};

G_DEFINE_FINAL_TYPE (BzGroupTileCssWatcher, bz_group_tile_css_watcher, G_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_WIDGET,
  PROP_GROUP,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
refresh (BzGroupTileCssWatcher *self);

static void
clear (BzGroupTileCssWatcher *self);

static void
bz_group_tile_css_watcher_dispose (GObject *object)
{
  BzGroupTileCssWatcher *self = BZ_GROUP_TILE_CSS_WATCHER (object);

  clear (self);

  g_weak_ref_clear (&self->widget);
  g_clear_pointer (&self->group, g_object_unref);

  G_OBJECT_CLASS (bz_group_tile_css_watcher_parent_class)->dispose (object);
}

static void
bz_group_tile_css_watcher_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  BzGroupTileCssWatcher *self = BZ_GROUP_TILE_CSS_WATCHER (object);

  switch (prop_id)
    {
    case PROP_WIDGET:
      g_value_take_object (value, bz_group_tile_css_watcher_dup_widget (self));
      break;
    case PROP_GROUP:
      g_value_set_object (value, bz_group_tile_css_watcher_get_group (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_group_tile_css_watcher_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  BzGroupTileCssWatcher *self = BZ_GROUP_TILE_CSS_WATCHER (object);

  switch (prop_id)
    {
    case PROP_WIDGET:
      bz_group_tile_css_watcher_set_widget (self, g_value_get_object (value));
      break;
    case PROP_GROUP:
      bz_group_tile_css_watcher_set_group (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_group_tile_css_watcher_class_init (BzGroupTileCssWatcherClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_group_tile_css_watcher_set_property;
  object_class->get_property = bz_group_tile_css_watcher_get_property;
  object_class->dispose      = bz_group_tile_css_watcher_dispose;

  props[PROP_WIDGET] =
      g_param_spec_object (
          "widget",
          NULL, NULL,
          GTK_TYPE_WIDGET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_GROUP] =
      g_param_spec_object (
          "group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
dark_changed (BzGroupTileCssWatcher *self,
              GParamSpec            *pspec,
              AdwStyleManager       *mgr)
{
  g_autoptr (GtkWidget) widget = NULL;
  gboolean is_dark;

  if (self->css == NULL)
    return;

  widget = g_weak_ref_get (&self->widget);
  if (widget == NULL)
    return;

  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  gtk_widget_remove_css_class (widget, self->light_class);
  gtk_widget_remove_css_class (widget, self->dark_class);
  gtk_widget_remove_css_class (widget, self->light_text_class);
  gtk_widget_remove_css_class (widget, self->dark_text_class);

  gtk_widget_add_css_class (widget, is_dark ? self->dark_class : self->light_class);
  gtk_widget_add_css_class (widget, is_dark ? self->dark_text_class : self->light_text_class);
}

static void
bz_group_tile_css_watcher_init (BzGroupTileCssWatcher *self)
{
  g_weak_ref_init (&self->widget, NULL);

  g_signal_connect_object (
      adw_style_manager_get_default (),
      "notify::dark",
      G_CALLBACK (dark_changed),
      self,
      G_CONNECT_SWAPPED);
}

BzGroupTileCssWatcher *
bz_group_tile_css_watcher_new (void)
{
  return g_object_new (BZ_TYPE_GROUP_TILE_CSS_WATCHER, NULL);
}

GtkWidget *
bz_group_tile_css_watcher_dup_widget (BzGroupTileCssWatcher *self)
{
  g_return_val_if_fail (BZ_IS_GROUP_TILE_CSS_WATCHER (self), NULL);
  return g_weak_ref_get (&self->widget);
}

BzEntryGroup *
bz_group_tile_css_watcher_get_group (BzGroupTileCssWatcher *self)
{
  g_return_val_if_fail (BZ_IS_GROUP_TILE_CSS_WATCHER (self), NULL);
  return self->group;
}

void
bz_group_tile_css_watcher_set_widget (BzGroupTileCssWatcher *self,
                                      GtkWidget             *widget)
{
  g_return_if_fail (BZ_IS_GROUP_TILE_CSS_WATCHER (self));

  g_weak_ref_clear (&self->widget);
  if (widget != NULL)
    g_weak_ref_init (&self->widget, widget);

  refresh (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WIDGET]);
}

void
bz_group_tile_css_watcher_set_group (BzGroupTileCssWatcher *self,
                                     BzEntryGroup          *group)
{
  g_return_if_fail (BZ_IS_GROUP_TILE_CSS_WATCHER (self));

  g_clear_pointer (&self->group, g_object_unref);
  if (group != NULL)
    self->group = g_object_ref (group);

  refresh (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}

static gdouble
get_luminance (GdkRGBA *rgba)
{
  return (0.299 * rgba->red * 255.0) +
         (0.587 * rgba->green * 255.0) +
         (0.114 * rgba->blue * 255.0);
}

static gboolean
color_is_light (const char *hex_color)
{
  GdkRGBA rgba;
  gdouble luminance;

  if (hex_color == NULL || !gdk_rgba_parse (&rgba, hex_color))
    return FALSE;

  luminance = get_luminance (&rgba);

  return luminance > LUMINANCE_THRESHOLD;
}

static void
refresh (BzGroupTileCssWatcher *self)
{
  g_autoptr (GtkWidget) widget   = NULL;
  const char *id                 = NULL;
  const char *light_accent_color = NULL;
  const char *dark_accent_color  = NULL;

  clear (self);

  widget = g_weak_ref_get (&self->widget);
  if (self->group == NULL ||
      widget == NULL)
    return;

  id                 = bz_entry_group_get_id (self->group);
  light_accent_color = bz_entry_group_get_light_accent_color (self->group);
  dark_accent_color  = bz_entry_group_get_dark_accent_color (self->group);

  if (light_accent_color != NULL ||
      dark_accent_color != NULL)
    {
      g_autoptr (GString) fixed_id = NULL;
      g_autofree char *css_string  = NULL;
      gboolean         is_dark;

      fixed_id = g_string_new (id);
      g_string_replace (fixed_id, ".", "--", 0);

      self->light_class = g_strdup_printf ("%s-light", fixed_id->str);
      self->dark_class  = g_strdup_printf ("%s-dark", fixed_id->str);

      self->light_text_class = g_strdup (
          color_is_light (light_accent_color != NULL ? light_accent_color : dark_accent_color)
              ? "flathub-gunmetal"
              : "flathub-lotion");

      self->dark_text_class = g_strdup (
          color_is_light (dark_accent_color != NULL ? dark_accent_color : light_accent_color)
              ? "flathub-gunmetal"
              : "flathub-lotion");

      css_string = g_strdup_printf (
          ".%s{background-color:%s;transition:background-color 200ms;}\n"
          ".%s:hover{background-color:shade(%s, 1.05);}\n"
          ".%s:active{background-color:shade(%s, 1.10);}\n"
          ".%s{background-color:%s;transition:background-color 200ms;}\n"
          ".%s:hover{background-color:shade(%s, 1.05);}\n"
          ".%s:active{background-color:shade(%s, 1.10);}",
          self->light_class,
          light_accent_color != NULL ? light_accent_color : dark_accent_color,
          self->light_class,
          light_accent_color != NULL ? light_accent_color : dark_accent_color,
          self->light_class,
          light_accent_color != NULL ? light_accent_color : dark_accent_color,
          self->dark_class,
          dark_accent_color != NULL ? dark_accent_color : light_accent_color,
          self->dark_class,
          dark_accent_color != NULL ? dark_accent_color : light_accent_color,
          self->dark_class,
          dark_accent_color != NULL ? dark_accent_color : light_accent_color);

      self->css = gtk_css_provider_new ();
      gtk_css_provider_load_from_string (
          self->css, css_string);
      gtk_style_context_add_provider_for_display (
          gdk_display_get_default (),
          GTK_STYLE_PROVIDER (self->css),
          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

      is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

      gtk_widget_add_css_class (widget, is_dark ? self->dark_class : self->light_class);
      gtk_widget_add_css_class (widget, is_dark ? self->dark_text_class : self->light_text_class);
    }
}

static void
clear (BzGroupTileCssWatcher *self)
{
  g_autoptr (GtkWidget) widget = NULL;

  widget = g_weak_ref_get (&self->widget);
  if (widget != NULL)
    {
      if (self->light_class != NULL)
        gtk_widget_remove_css_class (widget, self->light_class);
      if (self->dark_class != NULL)
        gtk_widget_remove_css_class (widget, self->dark_class);
      if (self->light_text_class != NULL)
        gtk_widget_remove_css_class (widget, self->light_text_class);
      if (self->dark_text_class != NULL)
        gtk_widget_remove_css_class (widget, self->dark_text_class);
    }

  g_clear_pointer (&self->light_class, g_free);
  g_clear_pointer (&self->dark_class, g_free);
  g_clear_pointer (&self->light_text_class, g_free);
  g_clear_pointer (&self->dark_text_class, g_free);

  if (self->css != NULL)
    gtk_style_context_remove_provider_for_display (
        gdk_display_get_default (),
        GTK_STYLE_PROVIDER (self->css));
  g_clear_pointer (&self->css, g_object_unref);
}

/* End of bz-group-tile-css-watcher.c */
