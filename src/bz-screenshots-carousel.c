/* bz-screenshots-carousel.c
 *
 * Copyright 2025 Alexander Vanhee
 *
 * Adapted from gs-screenshot-carousel.c
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015-2019 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2019 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2021 Adrien Plazas <adrien.plazas@puri.sm>
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
#include <bge.h>
#include <glib/gi18n.h>

#include "bz-decorated-screenshot.h"
#include "bz-screenshots-carousel.h"
#include "bz-template-callbacks.h"

#define LIGHT_CLASS          "screenshot-carousel-light"
#define DARK_CLASS           "screenshot-carousel-dark"
#define LIGHT_MIX_PERCENTAGE 15
#define DARK_MIX_PERCENTAGE  4

struct _BzScreenshotsCarousel
{
  GtkWidget parent_instance;

  BgeCarousel *carousel;
  // GtkWidget          *carousel_indicator;
  GtkButton          *prev_button;
  GtkWidget          *prev_button_revealer;
  GtkButton          *next_button;
  GtkWidget          *next_button_revealer;
  GtkSingleSelection *selection;

  GListModel     *model;
  gboolean        compact;
  char           *light_accent_color;
  char           *dark_accent_color;
  char           *widget_css_class;
  GtkCssProvider *css;
  gulong          items_changed_id;
};

G_DEFINE_FINAL_TYPE (BzScreenshotsCarousel, bz_screenshots_carousel, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_MODEL,
  PROP_COMPACT,
  PROP_LIGHT_ACCENT_COLOR,
  PROP_DARK_ACCENT_COLOR,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
  SIGNAL_CLICKED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void refresh_css (BzScreenshotsCarousel *self);
static void clear_css (BzScreenshotsCarousel *self);

static void
update_button_visibility (BzScreenshotsCarousel *self)
{
  gdouble position;
  guint   n_pages;

  if (!self->carousel)
    return;

  position = gtk_single_selection_get_selected (self->selection);
  n_pages  = g_list_model_get_n_items (G_LIST_MODEL (self->selection));

  if (n_pages == 0)
  {
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->prev_button_revealer), FALSE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->next_button_revealer), FALSE);
    return;
  }

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->prev_button_revealer), position >= 0.5);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->next_button_revealer), position < n_pages - 1.5);
}

static void
carousel_navigate (BzScreenshotsCarousel *self,
                   AdwNavigationDirection direction)
{
  guint   n_children = 0;
  gdouble position   = 0.0;

  n_children = g_list_model_get_n_items (G_LIST_MODEL (self->selection));
  position   = gtk_single_selection_get_selected (self->selection);
  position += (direction == ADW_NAVIGATION_DIRECTION_BACK) ? -1 : 1;
  position = round (position);
  position = MIN (position, n_children - 1);
  position = MAX (0, position);

  gtk_single_selection_set_selected (self->selection, position);
}

static void
on_prev_clicked (BzScreenshotsCarousel *self)
{
  if (!self->carousel)
    return;

  carousel_navigate (self, ADW_NAVIGATION_DIRECTION_BACK);
}

static void
on_next_clicked (BzScreenshotsCarousel *self)
{
  if (!self->carousel)
    return;

  carousel_navigate (self, ADW_NAVIGATION_DIRECTION_FORWARD);
}

static void
on_notify_selected (BzScreenshotsCarousel *self)
{
  update_button_visibility (self);
}

static void
on_notify_model (BzScreenshotsCarousel *self)
{
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->selection));

  if (!self->compact && n_items >= 3)
    gtk_single_selection_set_selected (self->selection, 1);

  update_button_visibility (self);
}

static void
open_screenshot_at_index (BzScreenshotsCarousel *self, guint index)
{
  guint n_items;

  if (!self->model)
    return;

  n_items = g_list_model_get_n_items (self->model);
  if (index >= n_items)
    return;

  g_signal_emit (self, signals[SIGNAL_CLICKED], 0, index);
}

static void
on_screenshot_clicked (BzDecoratedScreenshot *screenshot, BzScreenshotsCarousel *self)
{
  BzAsyncTexture *async_texture = NULL;
  guint           index         = 0;
  guint           n_items       = 0;

  if (!self->model)
    return;

  async_texture = bz_decorated_screenshot_get_async_texture (screenshot);
  if (async_texture == NULL)
    return;

  n_items = g_list_model_get_n_items (self->model);
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzAsyncTexture) item = NULL;

      item = g_list_model_get_item (self->model, i);
      if (item == async_texture)
        {
          index = i;
          break;
        }
    }

  open_screenshot_at_index (self, index);
}

static void
on_expand_clicked (BzScreenshotsCarousel *self)
{
  gdouble position;
  guint   index;
  guint   n_pages;

  if (!self->carousel || !self->model)
    return;

  n_pages = g_list_model_get_n_items (G_LIST_MODEL (self->selection));
  if (n_pages == 0)
    return;

  position = gtk_single_selection_get_selected (self->selection);
  index    = (guint) round (position);
  index    = MIN (index, n_pages - 1);

  open_screenshot_at_index (self, index);
}

static int
get_carousel_height (BzScreenshotsCarousel *self)
{
  return self->compact ? 250 : 375;
}

static void
on_screenshot_focus_changed (BzDecoratedScreenshot *screenshot,
                             GParamSpec            *pspec,
                             BzScreenshotsCarousel *self)
{
  if (!self->carousel)
    return;

  if (gtk_widget_has_focus (GTK_WIDGET (screenshot)))
    {
      BzAsyncTexture *texture = NULL;
      guint           n_items = 0;

      texture = bz_decorated_screenshot_get_async_texture (screenshot);

      n_items = g_list_model_get_n_items (self->model);
      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr (BzAsyncTexture) item = NULL;

          item = g_list_model_get_item (self->model, i);
          if (item == texture)
            {
              gtk_single_selection_set_selected (self->selection, i);
              break;
            }
        }
    }
}

static GtkWidget *
on_create_widget (BzScreenshotsCarousel *self,
                  BzAsyncTexture        *item,
                  BgeCarousel           *carousel)
{
  GtkWidget       *screenshot = NULL;
  g_autofree char *caption    = NULL;
  guint            n_items    = 0;
  guint            index      = 0;

  screenshot = g_object_new (BZ_TYPE_DECORATED_SCREENSHOT,
                             "async-texture", item,
                             NULL);

  n_items = g_list_model_get_n_items (self->model);
  for (index = 0; index < n_items; index++)
    {
      g_autoptr (BzAsyncTexture) it = g_list_model_get_item (self->model, index);
      if (it == item)
        break;
    }

  caption = g_strdup_printf (_ ("Screenshot %u of %u"), index + 1, n_items);
  gtk_accessible_update_property (GTK_ACCESSIBLE (screenshot),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, caption,
                                  -1);

  g_signal_connect (screenshot, "clicked",
                    G_CALLBACK (on_screenshot_clicked), self);
  g_signal_connect (screenshot, "notify::has-focus",
                    G_CALLBACK (on_screenshot_focus_changed), self);

  return screenshot;
}

static void
on_remove_widget (BzScreenshotsCarousel *self,
                  BzDecoratedScreenshot *screenshot,
                  BzAsyncTexture        *item,
                  BgeCarousel           *carousel)
{
}

static void
dark_changed (BzScreenshotsCarousel *self,
              GParamSpec            *pspec,
              AdwStyleManager       *mgr)
{
  gboolean is_dark;

  if (self->css == NULL)
    return;

  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  gtk_widget_remove_css_class (GTK_WIDGET (self), LIGHT_CLASS);
  gtk_widget_remove_css_class (GTK_WIDGET (self), DARK_CLASS);

  gtk_widget_add_css_class (GTK_WIDGET (self), is_dark ? DARK_CLASS : LIGHT_CLASS);
}

static void
bz_screenshots_carousel_dispose (GObject *object)
{
  BzScreenshotsCarousel *self       = BZ_SCREENSHOTS_CAROUSEL (object);
  GtkWidget             *root_child = gtk_widget_get_first_child (GTK_WIDGET (self));

  if (self->model && self->items_changed_id)
    {
      g_signal_handler_disconnect (self->model, self->items_changed_id);
      self->items_changed_id = 0;
    }

  g_clear_object (&self->model);

  clear_css (self);

  g_clear_pointer (&self->light_accent_color, g_free);
  g_clear_pointer (&self->dark_accent_color, g_free);
  g_clear_pointer (&self->widget_css_class, g_free);

  if (root_child != NULL)
    gtk_widget_unparent (root_child);

  gtk_widget_dispose_template (GTK_WIDGET (self), BZ_TYPE_SCREENSHOTS_CAROUSEL);

  G_OBJECT_CLASS (bz_screenshots_carousel_parent_class)->dispose (object);
}

static void
bz_screenshots_carousel_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  BzScreenshotsCarousel *self = BZ_SCREENSHOTS_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;
    case PROP_COMPACT:
      g_value_set_boolean (value, self->compact);
      break;
    case PROP_LIGHT_ACCENT_COLOR:
      g_value_set_string (value, self->light_accent_color);
      break;
    case PROP_DARK_ACCENT_COLOR:
      g_value_set_string (value, self->dark_accent_color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_screenshots_carousel_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  BzScreenshotsCarousel *self = BZ_SCREENSHOTS_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      bz_screenshots_carousel_set_model (self, g_value_get_object (value));
      break;
    case PROP_COMPACT:
      bz_screenshots_carousel_set_compact (self, g_value_get_boolean (value));
      break;
    case PROP_LIGHT_ACCENT_COLOR:
      bz_screenshots_carousel_set_light_accent_color (self, g_value_get_string (value));
      break;
    case PROP_DARK_ACCENT_COLOR:
      bz_screenshots_carousel_set_dark_accent_color (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_screenshots_carousel_class_init (BzScreenshotsCarouselClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_screenshots_carousel_dispose;
  object_class->get_property = bz_screenshots_carousel_get_property;
  object_class->set_property = bz_screenshots_carousel_set_property;

  properties[PROP_MODEL] =
      g_param_spec_object ("model",
                           NULL,
                           NULL,
                           G_TYPE_LIST_MODEL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_COMPACT] =
      g_param_spec_boolean ("compact",
                            NULL,
                            NULL,
                            FALSE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_LIGHT_ACCENT_COLOR] =
      g_param_spec_string ("light-accent-color",
                           NULL,
                           NULL,
                           NULL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_DARK_ACCENT_COLOR] =
      g_param_spec_string ("dark-accent-color",
                           NULL,
                           NULL,
                           NULL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[SIGNAL_CLICKED] =
      g_signal_new ("clicked",
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_LAST,
                    0,
                    NULL, NULL,
                    NULL,
                    G_TYPE_NONE,
                    1,
                    G_TYPE_UINT);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-screenshots-carousel.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, carousel);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, prev_button);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, prev_button_revealer);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, next_button);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, next_button_revealer);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotsCarousel, selection);
  gtk_widget_class_bind_template_callback (widget_class, on_prev_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_next_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_notify_selected);
  gtk_widget_class_bind_template_callback (widget_class, on_notify_model);
  gtk_widget_class_bind_template_callback (widget_class, on_create_widget);
  gtk_widget_class_bind_template_callback (widget_class, on_remove_widget);
  gtk_widget_class_bind_template_callback (widget_class, on_expand_clicked);
  gtk_widget_class_bind_template_callback (widget_class, get_carousel_height);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "screenshot-carousel");
}

static void
bz_screenshots_carousel_init (BzScreenshotsCarousel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (
      adw_style_manager_get_default (),
      "notify::dark",
      G_CALLBACK (dark_changed),
      self,
      G_CONNECT_SWAPPED);
}

GtkWidget *
bz_screenshots_carousel_new (void)
{
  return g_object_new (BZ_TYPE_SCREENSHOTS_CAROUSEL, NULL);
}

void
bz_screenshots_carousel_set_model (BzScreenshotsCarousel *self, GListModel *model)
{
  g_return_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  if (self->model && self->items_changed_id)
    {
      g_signal_handler_disconnect (self->model, self->items_changed_id);
      self->items_changed_id = 0;
    }

  g_clear_object (&self->model);
  if (model)
    {
      self->model = g_object_ref (model);
      gtk_single_selection_set_selected (self->selection, 0);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

GListModel *
bz_screenshots_carousel_get_model (BzScreenshotsCarousel *self)
{
  g_return_val_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self), NULL);
  return self->model;
}

void
bz_screenshots_carousel_set_compact (BzScreenshotsCarousel *self, gboolean compact)
{
  g_return_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self));
  if (self->compact == compact)
    return;
  self->compact = compact;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPACT]);
}

gboolean
bz_screenshots_carousel_get_compact (BzScreenshotsCarousel *self)
{
  g_return_val_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self), FALSE);
  return self->compact;
}

void
bz_screenshots_carousel_set_light_accent_color (BzScreenshotsCarousel *self,
                                                const char            *color)
{
  g_return_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self));

  if (color == self->light_accent_color ||
      (color != NULL &&
       g_strcmp0 (self->light_accent_color, color) == 0))
    return;

  g_clear_pointer (&self->light_accent_color, g_free);
  if (color != NULL)
    self->light_accent_color = g_strdup (color);

  refresh_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LIGHT_ACCENT_COLOR]);
}

const char *
bz_screenshots_carousel_get_light_accent_color (BzScreenshotsCarousel *self)
{
  g_return_val_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self), NULL);
  return self->light_accent_color;
}

void
bz_screenshots_carousel_set_dark_accent_color (BzScreenshotsCarousel *self,
                                               const char            *color)
{
  g_return_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self));

  if (color == self->dark_accent_color ||
      (color != NULL &&
       g_strcmp0 (self->dark_accent_color, color) == 0))
    return;

  g_clear_pointer (&self->dark_accent_color, g_free);
  if (color != NULL)
    self->dark_accent_color = g_strdup (color);

  refresh_css (self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DARK_ACCENT_COLOR]);
}

const char *
bz_screenshots_carousel_get_dark_accent_color (BzScreenshotsCarousel *self)
{
  g_return_val_if_fail (BZ_IS_SCREENSHOTS_CAROUSEL (self), NULL);
  return self->dark_accent_color;
}

static void
refresh_css (BzScreenshotsCarousel *self)
{
  g_autofree char *css_string = NULL;
  g_autofree char *light_bg   = NULL;
  g_autofree char *dark_bg    = NULL;
  gboolean         is_dark;

  clear_css (self);

  if (self->light_accent_color == NULL &&
      self->dark_accent_color == NULL)
    return;

  self->widget_css_class = g_strdup_printf ("screenshot-carousel-%p", (void *) self);
  gtk_widget_add_css_class (GTK_WIDGET (self), self->widget_css_class);

  if (self->light_accent_color != NULL && self->dark_accent_color != NULL)
    light_bg = g_strdup_printf ("color-mix(in srgb, %s %d%%, rgb(255,255,255))",
                                self->light_accent_color, LIGHT_MIX_PERCENTAGE);
  else if (self->light_accent_color != NULL)
    light_bg = g_strdup (self->light_accent_color);
  else if (self->dark_accent_color != NULL)
    light_bg = g_strdup_printf ("color-mix(in srgb, %s %d%%, rgb(255,255,255))",
                                self->dark_accent_color, LIGHT_MIX_PERCENTAGE);

  if (self->light_accent_color != NULL && self->dark_accent_color != NULL)
    dark_bg = g_strdup_printf ("color-mix(in srgb, %s %d%%, rgb(29,29,32))",
                               self->dark_accent_color, DARK_MIX_PERCENTAGE);
  else if (self->dark_accent_color != NULL)
    dark_bg = g_strdup (self->dark_accent_color);
  else if (self->light_accent_color != NULL)
    dark_bg = g_strdup_printf ("color-mix(in srgb, %s %d%%, rgb(29,29,32))",
                               self->light_accent_color, DARK_MIX_PERCENTAGE);

  css_string = g_strdup_printf (
      ".%s.%s{background-color:%s;}\n"
      ".%s.%s{background-color:%s;}",
      self->widget_css_class, LIGHT_CLASS, light_bg,
      self->widget_css_class, DARK_CLASS, dark_bg);

  self->css = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (self->css, css_string);
  gtk_style_context_add_provider_for_display (
      gdk_display_get_default (),
      GTK_STYLE_PROVIDER (self->css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());

  gtk_widget_add_css_class (GTK_WIDGET (self), is_dark ? DARK_CLASS : LIGHT_CLASS);
}

static void
clear_css (BzScreenshotsCarousel *self)
{
  gtk_widget_remove_css_class (GTK_WIDGET (self), LIGHT_CLASS);
  gtk_widget_remove_css_class (GTK_WIDGET (self), DARK_CLASS);

  if (self->widget_css_class != NULL)
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self), self->widget_css_class);
      g_clear_pointer (&self->widget_css_class, g_free);
    }

  if (self->css != NULL)
    gtk_style_context_remove_provider_for_display (
        gdk_display_get_default (),
        GTK_STYLE_PROVIDER (self->css));
  g_clear_pointer (&self->css, g_object_unref);
}
