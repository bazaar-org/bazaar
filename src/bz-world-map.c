/* bz-world-map.c
 *
 * Copyright 2025 Alexander Vanhee
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

#include <adwaita.h>

#include "bz-country-data-point.h"
#include "bz-country.h"
#include "bz-world-map-parser.h"
#include "bz-world-map.h"

#define OPACITY_MULTIPLIER 2

struct _BzWorldMap
{
  GtkWidget parent_instance;

  BzWorldMapParser *parser;
  GListModel       *countries;
  GListModel       *model;

  double min_lon;
  double max_lon;
  double min_lat;
  double max_lat;

  GskPath **country_paths;
  guint    *path_to_country;
  guint     n_paths;

  gboolean cache_valid;
  int      last_width;
  int      last_height;

  GtkEventController *motion;
  GtkGesture         *gesture;
  double              offset_x;
  double              offset_y;
  double              scale;
  int                 hovered_country;
  double              motion_x;
  double              motion_y;

  guint max_downloads;

  GtkWidget *tooltip_box;
  GtkWidget *tooltip_label1;
  GtkWidget *tooltip_prefix_label;
  GtkWidget *tooltip_label2;
};

G_DEFINE_FINAL_TYPE (BzWorldMap, bz_world_map, GTK_TYPE_WIDGET)

enum
{
  PROP_0,

  PROP_MODEL,

  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static guint
get_downloads_for_country (BzWorldMap *self,
                           const char *iso_code)
{
  guint n_items = 0;

  if (self->model == NULL)
    return 0;

  n_items = g_list_model_get_n_items (self->model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzCountryDataPoint) point = g_list_model_get_item (self->model, i);
      const char *country_code             = bz_country_data_point_get_country_code (point);

      if (g_strcmp0 (country_code, iso_code) == 0)
        return bz_country_data_point_get_downloads (point);
    }

  return 0;
}

static void
calculate_max_downloads (BzWorldMap *self)
{
  guint n_items = 0;

  self->max_downloads = 0;

  if (self->model == NULL)
    return;

  n_items = g_list_model_get_n_items (self->model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzCountryDataPoint) point = g_list_model_get_item (self->model, i);
      guint downloads                      = bz_country_data_point_get_downloads (point);

      if (downloads > self->max_downloads)
        self->max_downloads = downloads;
    }
}

static void
calculate_bounds (BzWorldMap *self)
{
  guint n_items = 0;

  if (self->countries == NULL)
    return;

  n_items = g_list_model_get_n_items (self->countries);

  self->min_lon = 180.0;
  self->max_lon = -180.0;
  self->min_lat = 90.0;
  self->max_lat = -90.0;

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzCountry) country = g_list_model_get_item (self->countries, i);
      GVariant    *coordinates      = bz_country_get_coordinates (country);
      GVariantIter poly_iter, ring_iter, point_iter;
      GVariant    *polygon = NULL;
      GVariant    *ring    = NULL;
      double       lon, lat;

      if (coordinates == NULL)
        continue;

      g_variant_iter_init (&poly_iter, coordinates);
      while ((polygon = g_variant_iter_next_value (&poly_iter)))
        {
          g_variant_iter_init (&ring_iter, polygon);
          while ((ring = g_variant_iter_next_value (&ring_iter)))
            {
              g_variant_iter_init (&point_iter, ring);
              while (g_variant_iter_next (&point_iter, "(dd)", &lon, &lat))
                {
                  if (lon < self->min_lon)
                    self->min_lon = lon;
                  if (lon > self->max_lon)
                    self->max_lon = lon;
                  if (lat < self->min_lat)
                    self->min_lat = lat;
                  if (lat > self->max_lat)
                    self->max_lat = lat;
                }
              g_clear_pointer (&ring, g_variant_unref);
            }
          g_clear_pointer (&polygon, g_variant_unref);
        }
    }
}

static void
project_point (BzWorldMap *self,
               double      lon,
               double      lat,
               double      width,
               double      height,
               double     *x,
               double     *y)
{
  double lon_range = self->max_lon - self->min_lon;
  double lat_range = self->max_lat - self->min_lat;

  *x = ((lon - self->min_lon) / lon_range) * width;
  *y = height - ((lat - self->min_lat) / lat_range) * height;
}

static void
calculate_transform (BzWorldMap *self,
                     double      widget_width,
                     double      widget_height,
                     double      map_width,
                     double      map_height)
{
  double scale_x = widget_width / map_width;
  double scale_y = widget_height / map_height;

  self->scale = MIN (scale_x, scale_y);

  self->offset_x = (widget_width - map_width * self->scale) / 2.0;
  self->offset_y = (widget_height - map_height * self->scale) / 2.0;
}

static void
build_paths (BzWorldMap *self,
             double      width,
             double      height)
{
  guint n_items    = 0;
  guint path_index = 0;

  if (self->countries == NULL)
    return;

  if (self->country_paths != NULL)
    {
      for (guint i = 0; i < self->n_paths; i++)
        g_clear_pointer (&self->country_paths[i], gsk_path_unref);
      g_clear_pointer (&self->country_paths, g_free);
    }

  g_clear_pointer (&self->path_to_country, g_free);

  n_items = g_list_model_get_n_items (self->countries);

  self->n_paths = 0;
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzCountry) country = NULL;
      GVariant    *coordinates      = NULL;
      GVariantIter poly_iter        = { 0 };
      GVariant    *polygon          = NULL;

      country     = g_list_model_get_item (self->countries, i);
      coordinates = bz_country_get_coordinates (country);

      if (coordinates == NULL)
        continue;

      g_variant_iter_init (&poly_iter, coordinates);
      while ((polygon = g_variant_iter_next_value (&poly_iter)))
        {
          self->n_paths += g_variant_n_children (polygon);
          g_clear_pointer (&polygon, g_variant_unref);
        }
    }

  self->country_paths   = g_new0 (GskPath *, self->n_paths);
  self->path_to_country = g_new0 (guint, self->n_paths);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzCountry) country = NULL;
      GVariant    *coordinates      = NULL;
      GVariantIter poly_iter        = { 0 };
      GVariant    *polygon          = NULL;

      country     = g_list_model_get_item (self->countries, i);
      coordinates = bz_country_get_coordinates (country);

      if (coordinates == NULL)
        continue;

      g_variant_iter_init (&poly_iter, coordinates);
      while ((polygon = g_variant_iter_next_value (&poly_iter)))
        {
          GVariantIter ring_iter = { 0 };
          GVariant    *ring      = NULL;

          g_variant_iter_init (&ring_iter, polygon);
          while ((ring = g_variant_iter_next_value (&ring_iter)))
            {
              g_autoptr (GskPathBuilder) builder = NULL;
              GVariantIter point_iter            = { 0 };
              double       lon                   = 0.0;
              double       lat                   = 0.0;
              gboolean     first                 = TRUE;

              builder = gsk_path_builder_new ();

              g_variant_iter_init (&point_iter, ring);
              while (g_variant_iter_next (&point_iter, "(dd)", &lon, &lat))
                {
                  double x = 0.0;
                  double y = 0.0;

                  project_point (self, lon, lat, width, height, &x, &y);

                  if (first)
                    {
                      gsk_path_builder_move_to (builder, x, y);
                      first = FALSE;
                    }
                  else
                    {
                      gsk_path_builder_line_to (builder, x, y);
                    }
                }

              gsk_path_builder_close (builder);
              self->country_paths[path_index]   = gsk_path_builder_to_path (builder);
              self->path_to_country[path_index] = i;
              path_index++;

              g_clear_pointer (&ring, g_variant_unref);
            }
          g_clear_pointer (&polygon, g_variant_unref);
        }
    }

  self->cache_valid = TRUE;
}

static void
invalidate_cache (BzWorldMap *self)
{
  self->cache_valid = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_style_changed (AdwStyleManager *style_manager,
                  GParamSpec      *pspec,
                  BzWorldMap      *self)
{
  invalidate_cache (self);
}

static void
update_hovered_country (BzWorldMap *self,
                        double      x,
                        double      y)
{
  double map_x;
  double map_y;

  map_x = (x - self->offset_x) / self->scale;
  map_y = (y - self->offset_y) / self->scale;

  self->motion_x        = x;
  self->motion_y        = y;
  self->hovered_country = -1;

  for (guint i = 0; i < self->n_paths; i++)
    {
      if (gsk_path_in_fill (self->country_paths[i],
                            &GRAPHENE_POINT_INIT (map_x, map_y),
                            GSK_FILL_RULE_WINDING))
        {
          self->hovered_country = self->path_to_country[i];
          break;
        }
    }
}

static void
motion_event (BzWorldMap               *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller)
{
  int old_hovered;

  old_hovered = self->hovered_country;

  update_hovered_country (self, x, y);

  if (old_hovered != self->hovered_country || self->hovered_country >= 0)
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
motion_leave (BzWorldMap               *self,
              GtkEventControllerMotion *controller)
{
  if (self->hovered_country != -1)
    {
      self->hovered_country = -1;
      self->motion_x        = -1.0;
      self->motion_y        = -1.0;
      gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
gesture_begin (BzWorldMap     *self,
               double          start_x,
               double          start_y,
               GtkGestureDrag *gesture)
{
  update_hovered_country (self, start_x, start_y);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
gesture_update (BzWorldMap     *self,
                double          offset_x,
                double          offset_y,
                GtkGestureDrag *gesture)
{
  double start_x;
  double start_y;

  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);

  update_hovered_country (self, start_x + offset_x, start_y + offset_y);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
gesture_end (BzWorldMap     *self,
             double          offset_x,
             double          offset_y,
             GtkGestureDrag *gesture)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
bz_world_map_dispose (GObject *object)
{
  BzWorldMap *self = BZ_WORLD_MAP (object);

  g_signal_handlers_disconnect_by_func (adw_style_manager_get_default (),
                                        on_style_changed,
                                        self);

  if (self->country_paths != NULL)
    {
      for (guint i = 0; i < self->n_paths; i++)
        g_clear_pointer (&self->country_paths[i], gsk_path_unref);
      g_free (self->country_paths);
      self->country_paths = NULL;
    }

  if (self->path_to_country != NULL)
    {
      g_free (self->path_to_country);
      self->path_to_country = NULL;
    }

  if (self->tooltip_box != NULL)
    gtk_widget_unparent (self->tooltip_box);

  g_clear_object (&self->countries);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (bz_world_map_parent_class)->dispose (object);
}

static void
bz_world_map_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzWorldMap *self = BZ_WORLD_MAP (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_world_map_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzWorldMap *self = BZ_WORLD_MAP (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_clear_object (&self->model);
      self->model = g_value_dup_object (value);
      calculate_max_downloads (self);
      gtk_widget_queue_draw (GTK_WIDGET (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_world_map_size_allocate (GtkWidget *widget,
                            int        width,
                            int        height,
                            int        baseline)
{
  BzWorldMap *self = BZ_WORLD_MAP (widget);

  if (width == self->last_width && height == self->last_height)
    return;

  self->last_width  = width;
  self->last_height = height;

  invalidate_cache (self);
}

static void
bz_world_map_snapshot (GtkWidget   *widget,
                       GtkSnapshot *snapshot)
{
  BzWorldMap      *self              = BZ_WORLD_MAP (widget);
  double           widget_width      = gtk_widget_get_width (widget);
  double           widget_height     = gtk_widget_get_height (widget);
  AdwStyleManager *style_manager     = adw_style_manager_get_default ();
  g_autoptr (GdkRGBA) accent_color   = adw_style_manager_get_accent_color_rgba (style_manager);
  GdkRGBA stroke_color               = { 0 };
  g_autoptr (GskStroke) stroke       = gsk_stroke_new (0.5);
  g_autoptr (GskStroke) hover_stroke = gsk_stroke_new (1.5);
  double map_width                   = 1000.0;
  double map_height                  = 500.0;

  if (self->countries == NULL)
    return;

  gtk_widget_get_color (widget, &stroke_color);
  stroke_color.alpha = 0.3;

  if (!self->cache_valid)
    build_paths (self, map_width, map_height);

  calculate_transform (self, widget_width, widget_height, map_width, map_height);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (self->offset_x, self->offset_y));
  gtk_snapshot_scale (snapshot, self->scale, self->scale);

  for (guint i = 0; i < self->n_paths; i++)
    {
      guint country_idx             = self->path_to_country[i];
      g_autoptr (BzCountry) country = g_list_model_get_item (self->countries, country_idx);
      const char *iso_code          = bz_country_get_iso_code (country);
      guint       downloads         = get_downloads_for_country (self, iso_code);
      GdkRGBA     fill_color        = *accent_color;

      if (self->max_downloads > 0 && downloads > 0)
        {
          double ratio     = (double) downloads / (double) self->max_downloads;
          fill_color.alpha = CLAMP (ratio * OPACITY_MULTIPLIER, 0.1, 1.0);
        }
      else
        {
          fill_color.alpha = 0.0;
        }

      gtk_snapshot_append_fill (snapshot, self->country_paths[i], GSK_FILL_RULE_WINDING, &fill_color);
      gtk_snapshot_append_stroke (snapshot, self->country_paths[i], stroke, &stroke_color);
    }

  gtk_snapshot_restore (snapshot);

  if (self->hovered_country >= 0)
    {
      GdkRGBA hover_color = stroke_color;
      hover_color.alpha   = 1.0;

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (self->offset_x, self->offset_y));
      gtk_snapshot_scale (snapshot, self->scale, self->scale);

      for (guint i = 0; i < self->n_paths; i++)
        {
          if (self->path_to_country[i] == (guint) self->hovered_country)
            {
              gtk_snapshot_append_stroke (snapshot, self->country_paths[i], hover_stroke, &hover_color);
            }
        }

      gtk_snapshot_restore (snapshot);
    }

  if (self->hovered_country >= 0 && self->motion_x >= 0.0 && self->motion_y >= 0.0)
    {
      g_autoptr (BzCountry) country    = g_list_model_get_item (self->countries, self->hovered_country);
      const char      *iso_code        = bz_country_get_iso_code (country);
      guint            download_number = get_downloads_for_country (self, iso_code);
      const char      *country_name    = bz_country_get_name (country);
      g_autofree char *label1_text     = g_strdup_printf ("<b>%s</b>", country_name);
      g_autofree char *label2_text     = g_strdup_printf ("%'u", download_number);
      GtkRequisition   natural_size;
      double           card_x = 0.0;
      double           card_y = 0.0;

      gtk_label_set_markup (GTK_LABEL (self->tooltip_label1), label1_text);
      gtk_label_set_text (GTK_LABEL (self->tooltip_prefix_label), _("Installs:"));
      gtk_label_set_text (GTK_LABEL (self->tooltip_label2), label2_text);

      gtk_widget_get_preferred_size (self->tooltip_box, NULL, &natural_size);

      gtk_widget_allocate (self->tooltip_box, natural_size.width, natural_size.height, -1, NULL);

      if (self->motion_x > widget_width / 2.0)
        card_x = self->motion_x - natural_size.width - 10.0;
      else
        card_x = self->motion_x + 10.0;
      card_y = self->motion_y + 10.0;

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (card_x, card_y));
      gtk_widget_snapshot_child (widget, self->tooltip_box, snapshot);
      gtk_snapshot_restore (snapshot);
    }
}

static void
bz_world_map_class_init (BzWorldMapClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_world_map_dispose;
  object_class->get_property = bz_world_map_get_property;
  object_class->set_property = bz_world_map_set_property;

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  widget_class->snapshot      = bz_world_map_snapshot;
  widget_class->size_allocate = bz_world_map_size_allocate;
}

static void
bz_world_map_init (BzWorldMap *self)
{
  AdwStyleManager *style_manager = adw_style_manager_get_default ();
  g_autoptr (GError) error       = NULL;
  GtkWidget *inner_box           = NULL;
  GtkWidget *label2_box          = NULL;

  self->parser          = bz_world_map_parser_new ();
  self->hovered_country = -1;
  self->motion_x        = -1.0;
  self->motion_y        = -1.0;
  self->max_downloads   = 0;

  self->motion = gtk_event_controller_motion_new ();
  g_signal_connect_swapped (self->motion, "motion", G_CALLBACK (motion_event), self);
  g_signal_connect_swapped (self->motion, "leave", G_CALLBACK (motion_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->motion);

  self->gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (self->gesture), TRUE);
  g_signal_connect_swapped (self->gesture, "drag-begin", G_CALLBACK (gesture_begin), self);
  g_signal_connect_swapped (self->gesture, "drag-update", G_CALLBACK (gesture_update), self);
  g_signal_connect_swapped (self->gesture, "drag-end", G_CALLBACK (gesture_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->gesture));

  self->tooltip_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (self->tooltip_box, "floating-tooltip");
  gtk_widget_add_css_class (self->tooltip_box, "card");

  inner_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start (inner_box, 12);
  gtk_widget_set_margin_end (inner_box, 12);
  gtk_widget_set_margin_top (inner_box, 12);
  gtk_widget_set_margin_bottom (inner_box, 12);

  self->tooltip_label1 = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_label1, "heading");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_label1), 0.0);
  gtk_label_set_use_markup (GTK_LABEL (self->tooltip_label1), TRUE);
  gtk_box_append (GTK_BOX (inner_box), self->tooltip_label1);

  label2_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  self->tooltip_prefix_label = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_prefix_label, "body");
  gtk_widget_add_css_class (self->tooltip_prefix_label, "dim-label");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_prefix_label), 0.0);
  gtk_box_append (GTK_BOX (label2_box), self->tooltip_prefix_label);

  self->tooltip_label2 = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_label2, "monospace");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_label2), 0.0);
  gtk_box_append (GTK_BOX (label2_box), self->tooltip_label2);

  gtk_box_append (GTK_BOX (inner_box), label2_box);

  gtk_box_append (GTK_BOX (self->tooltip_box), inner_box);

  gtk_widget_set_parent (self->tooltip_box, GTK_WIDGET (self));

  g_signal_connect (style_manager, "notify::dark",
                    G_CALLBACK (on_style_changed), self);
  g_signal_connect (style_manager, "notify::accent-color",
                    G_CALLBACK (on_style_changed), self);

  if (bz_world_map_parser_load_from_resource (self->parser,
                                              "/io/github/kolunmi/Bazaar/countries.gvariant",
                                              &error))
    {
      self->countries = g_object_ref (bz_world_map_parser_get_countries (self->parser));
      calculate_bounds (self);
      g_clear_object (&self->parser);
    }
  else
    {
      g_warning ("BzWorldMap: Failed to load countries: %s", error->message);
      g_clear_object (&self->parser);
    }
}

GtkWidget *
bz_world_map_new (void)
{
  return g_object_new (BZ_TYPE_WORLD_MAP, NULL);
}
