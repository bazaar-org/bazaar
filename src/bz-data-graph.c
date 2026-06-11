/* bz-data-graph.c
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

#include "config.h"

#include <adwaita.h>
#include <math.h>

#include "bz-data-graph.h"
#include "bz-data-point.h"

#define LABEL_MARGIN       75.0
#define LABEL_MARGIN_RIGHT 35.0
#define TICK_LENGTH        5.0

struct _BzDataGraph
{
  GtkWidget parent_instance;

  GListModel *model;
  char       *independent_axis_label;
  char       *dependent_axis_label;
  char       *tooltip_prefix;
  int         independent_decimals;
  int         dependent_decimals;
  double      transition_progress;
  double      rounded_axis_max;

  AdwStyleManager *style_manager;

  GskPath        *path;
  GskPathMeasure *path_measure;

  GskPath        *trend;
  GskPathMeasure *trend_measure;

  GskRenderNode *other;
  GskRenderNode *graph;
  GskRenderNode *overlay;

  gboolean wants_animate_open;

  GtkEventController *motion;
  double              motion_x;
  double              motion_y;
  GtkGesture         *gesture;

  GtkWidget *tooltip_box;
  GtkWidget *tooltip_label1;
  GtkWidget *tooltip_prefix_label;
  GtkWidget *tooltip_label2;

  int allocated_width;
  int allocated_height;
};

G_DEFINE_FINAL_TYPE (BzDataGraph, bz_data_graph, GTK_TYPE_WIDGET)

enum
{
  PROP_0,

  PROP_MODEL,
  PROP_INDEPENDENT_AXIS_LABEL,
  PROP_DEPENDENT_AXIS_LABEL,
  PROP_TOOLTIP_PREFIX,
  PROP_INDEPENDENT_DECIMALS,
  PROP_DEPENDENT_DECIMALS,
  PROP_TRANSITION_PROGRESS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
items_changed (GListModel  *model,
               guint        position,
               guint        removed,
               guint        added,
               BzDataGraph *self);

static void
refresh_path (BzDataGraph *self,
              double       width,
              double       height);

static double
calculate_axis_tick_value (double value, gboolean round_up);

static void
on_style_changed (BzDataGraph     *self,
                  GParamSpec      *pspec,
                  AdwStyleManager *style_manager);

static void
refresh_graph (BzDataGraph *self);

static void
refresh_overlay (BzDataGraph *self);

static void
bz_data_graph_dispose (GObject *object)
{
  BzDataGraph *self = BZ_DATA_GRAPH (object);

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (
        self->model, items_changed, self);

  g_signal_handlers_disconnect_by_func (self->style_manager,
                                        on_style_changed,
                                        self);

  g_clear_object (&self->model);
  g_clear_pointer (&self->independent_axis_label, g_free);
  g_clear_pointer (&self->dependent_axis_label, g_free);
  g_clear_pointer (&self->tooltip_prefix, g_free);
  g_clear_pointer (&self->path, gsk_path_unref);
  g_clear_pointer (&self->path_measure, gsk_path_measure_unref);
  g_clear_pointer (&self->trend, gsk_path_unref);
  g_clear_pointer (&self->trend_measure, gsk_path_measure_unref);
  g_clear_pointer (&self->other, gsk_render_node_unref);
  g_clear_pointer (&self->graph, gsk_render_node_unref);
  g_clear_pointer (&self->overlay, gsk_render_node_unref);

  if (self->tooltip_box != NULL)
    gtk_widget_unparent (self->tooltip_box);

  G_OBJECT_CLASS (bz_data_graph_parent_class)->dispose (object);
}

static void
bz_data_graph_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzDataGraph *self = BZ_DATA_GRAPH (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, bz_data_graph_get_model (self));
      break;
    case PROP_INDEPENDENT_AXIS_LABEL:
      g_value_set_string (value, bz_data_graph_get_independent_axis_label (self));
      break;
    case PROP_DEPENDENT_AXIS_LABEL:
      g_value_set_string (value, bz_data_graph_get_dependent_axis_label (self));
      break;
    case PROP_TOOLTIP_PREFIX:
      g_value_set_string (value, bz_data_graph_get_tooltip_prefix (self));
      break;
    case PROP_INDEPENDENT_DECIMALS:
      g_value_set_int (value, bz_data_graph_get_independent_decimals (self));
      break;
    case PROP_DEPENDENT_DECIMALS:
      g_value_set_int (value, bz_data_graph_get_dependent_decimals (self));
      break;
    case PROP_TRANSITION_PROGRESS:
      g_value_set_double (value, bz_data_graph_get_transition_progress (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_data_graph_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BzDataGraph *self = BZ_DATA_GRAPH (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      bz_data_graph_set_model (self, g_value_get_object (value));
      break;
    case PROP_INDEPENDENT_AXIS_LABEL:
      bz_data_graph_set_independent_axis_label (self, g_value_get_string (value));
      break;
    case PROP_DEPENDENT_AXIS_LABEL:
      bz_data_graph_set_dependent_axis_label (self, g_value_get_string (value));
      break;
    case PROP_TOOLTIP_PREFIX:
      bz_data_graph_set_tooltip_prefix (self, g_value_get_string (value));
      break;
    case PROP_INDEPENDENT_DECIMALS:
      bz_data_graph_set_independent_decimals (self, g_value_get_int (value));
      break;
    case PROP_DEPENDENT_DECIMALS:
      bz_data_graph_set_dependent_decimals (self, g_value_get_int (value));
      break;
    case PROP_TRANSITION_PROGRESS:
      bz_data_graph_set_transition_progress (self, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_data_graph_size_allocate (GtkWidget *widget,
                             int        width,
                             int        height,
                             int        baseline)
{
  BzDataGraph     *self          = BZ_DATA_GRAPH (widget);
  GtkRequisition   natural_size  = { 0 };
  graphene_point_t card_position = { 0 };

  if (width != self->allocated_width ||
      height != self->allocated_height)
    {
      refresh_path (
          self,
          (double) width - LABEL_MARGIN - LABEL_MARGIN_RIGHT,
          (double) height - LABEL_MARGIN);
      refresh_graph (self);
      refresh_overlay (self);

      self->allocated_width  = width;
      self->allocated_height = height;
    }

  gtk_widget_get_preferred_size (self->tooltip_box, NULL, &natural_size);
  if (self->motion_x > width / 2.0)
    card_position.x = self->motion_x - natural_size.width - 10.0;
  else
    card_position.x = self->motion_x + 10.0;
  card_position.y = self->motion_y + 10.0;

  gtk_widget_allocate (
      self->tooltip_box,
      natural_size.width,
      natural_size.height,
      baseline,
      gsk_transform_translate (NULL, &card_position));

  gtk_widget_queue_draw (widget);
}

static void
on_style_changed (BzDataGraph     *self,
                  GParamSpec      *pspec,
                  AdwStyleManager *style_manager)
{
  refresh_graph (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
bz_data_graph_snapshot (GtkWidget   *widget,
                        GtkSnapshot *snapshot)
{
  BzDataGraph *self = BZ_DATA_GRAPH (widget);

  if (self->path == NULL)
    return;

  if (self->graph != NULL)
    gtk_snapshot_append_node (snapshot, self->graph);

  if (self->other != NULL)
    {
      GdkRGBA widget_color = { 0 };

      gtk_widget_get_color (widget, &widget_color);
      if (self->other != NULL)
        {
          graphene_rect_t bounds = { 0 };

          gsk_render_node_get_bounds (self->other, &bounds);

          gtk_snapshot_push_mask (snapshot, GSK_MASK_MODE_ALPHA);
          gtk_snapshot_append_node (snapshot, self->other);
          gtk_snapshot_pop (snapshot);
          gtk_snapshot_append_color (snapshot, &widget_color, &bounds);
          gtk_snapshot_pop (snapshot);
        }
    }

  if (self->overlay != NULL)
    {
      gtk_snapshot_append_node (snapshot, self->overlay);

      /* tooltip card */
      gtk_widget_snapshot_child (GTK_WIDGET (self), self->tooltip_box, snapshot);
    }

  if (self->wants_animate_open)
    {
      AdwAnimationTarget *transition_target = NULL;
      AdwSpringParams    *transition_spring = NULL;
      g_autoptr (AdwAnimation) transition   = NULL;

      self->wants_animate_open = FALSE;

      transition_target = adw_property_animation_target_new (G_OBJECT (self), "transition-progress");
      transition_spring = adw_spring_params_new (1.0, 1.0, 80.0);
      transition        = adw_spring_animation_new (GTK_WIDGET (self), 0.0, 1.0, transition_spring, transition_target);
      adw_spring_animation_set_epsilon (ADW_SPRING_ANIMATION (transition), 0.000001);
      adw_animation_play (transition);
    }
}

static void
bz_data_graph_class_init (BzDataGraphClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_data_graph_dispose;
  object_class->get_property = bz_data_graph_get_property;
  object_class->set_property = bz_data_graph_set_property;

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_INDEPENDENT_AXIS_LABEL] =
      g_param_spec_string (
          "independent-axis-label",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DEPENDENT_AXIS_LABEL] =
      g_param_spec_string (
          "dependent-axis-label",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TOOLTIP_PREFIX] =
      g_param_spec_string (
          "tooltip-prefix",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_INDEPENDENT_DECIMALS] =
      g_param_spec_int (
          "independent-decimals",
          NULL, NULL,
          -1, 4, (int) 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DEPENDENT_DECIMALS] =
      g_param_spec_int (
          "dependent-decimals",
          NULL, NULL,
          -1, 4, (int) 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TRANSITION_PROGRESS] =
      g_param_spec_double (
          "transition-progress",
          NULL, NULL,
          0.0, G_MAXDOUBLE, 0.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  widget_class->size_allocate = bz_data_graph_size_allocate;
  widget_class->snapshot      = bz_data_graph_snapshot;
}

static void
update_cursor (BzDataGraph *self,
               gdouble      x,
               gdouble      y)
{
  double widget_width  = gtk_widget_get_width (GTK_WIDGET (self));
  double widget_height = gtk_widget_get_height (GTK_WIDGET (self));

  if (x >= LABEL_MARGIN &&
      y >= 0.0 &&
      x < widget_width - LABEL_MARGIN_RIGHT &&
      y < widget_height - LABEL_MARGIN)
    gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "crosshair");
  else
    gtk_widget_set_cursor (GTK_WIDGET (self), NULL);

  refresh_overlay (self);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
motion_enter (BzDataGraph              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller)
{
  self->motion_x = x;
  self->motion_y = y;
  update_cursor (self, x, y);
}

static void
motion_event (BzDataGraph              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller)
{
  self->motion_x = x;
  self->motion_y = y;
  update_cursor (self, x, y);
}

static void
motion_leave (BzDataGraph              *self,
              GtkEventControllerMotion *controller)
{
  self->motion_x = -1.0;
  self->motion_y = -1.0;
  update_cursor (self, -1.0, -1.0);
}

static void
gesture_begin (BzDataGraph    *self,
               double          start_x,
               double          start_y,
               GtkGestureDrag *gesture)
{
  self->motion_x = start_x;
  self->motion_y = start_y;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
gesture_update (BzDataGraph    *self,
                double          offset_x,
                double          offset_y,
                GtkGestureDrag *gesture)
{
  double start_x, start_y;
  gtk_gesture_drag_get_start_point (gesture, &start_x, &start_y);
  self->motion_x = start_x + offset_x;
  self->motion_y = start_y + offset_y;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
gesture_end (BzDataGraph    *self,
             double          offset_x,
             double          offset_y,
             GtkGestureDrag *gesture)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
bz_data_graph_init (BzDataGraph *self)
{
  GtkWidget *inner_box  = NULL;
  GtkWidget *label2_box = NULL;
  GtkWidget *icon_image = NULL;

  self->style_manager = adw_style_manager_get_default ();

  g_signal_connect_swapped (self->style_manager, "notify::dark",
                            G_CALLBACK (on_style_changed), self);
  g_signal_connect_swapped (self->style_manager, "notify::accent-color",
                            G_CALLBACK (on_style_changed), self);

  self->motion = gtk_event_controller_motion_new ();
  g_signal_connect_swapped (self->motion, "enter", G_CALLBACK (motion_enter), self);
  g_signal_connect_swapped (self->motion, "motion", G_CALLBACK (motion_event), self);
  g_signal_connect_swapped (self->motion, "leave", G_CALLBACK (motion_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->motion);

  self->gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (self->gesture), TRUE);
  g_signal_connect_swapped (self->gesture, "drag-begin", G_CALLBACK (gesture_begin), self);
  g_signal_connect_swapped (self->gesture, "drag-update", G_CALLBACK (gesture_update), self);
  g_signal_connect_swapped (self->gesture, "drag-end", G_CALLBACK (gesture_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->gesture));

  self->motion_x         = -1.0;
  self->motion_y         = -1.0;
  self->rounded_axis_max = 0.0;

  self->tooltip_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class (self->tooltip_box, "card");
  gtk_widget_add_css_class (self->tooltip_box, "floating-tooltip");

  inner_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_start (inner_box, 12);
  gtk_widget_set_margin_end (inner_box, 12);
  gtk_widget_set_margin_top (inner_box, 12);
  gtk_widget_set_margin_bottom (inner_box, 12);

  self->tooltip_label1 = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_label1, "heading");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_label1), 0.0);
  gtk_box_append (GTK_BOX (inner_box), self->tooltip_label1);

  label2_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  icon_image = gtk_image_new_from_icon_name ("square-filled-symbolic");
  gtk_widget_add_css_class (icon_image, "accent");
  gtk_box_append (GTK_BOX (label2_box), icon_image);

  self->tooltip_prefix_label = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_prefix_label, "body");
  gtk_widget_add_css_class (self->tooltip_prefix_label, "dim-label");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_prefix_label), 0.0);
  gtk_box_append (GTK_BOX (label2_box), self->tooltip_prefix_label);

  self->tooltip_label2 = gtk_label_new ("");
  gtk_widget_add_css_class (self->tooltip_label2, "monospace");
  gtk_widget_add_css_class (self->tooltip_label2, "dimmed");
  gtk_label_set_xalign (GTK_LABEL (self->tooltip_label2), 0.0);

  gtk_box_append (GTK_BOX (label2_box), self->tooltip_label2);
  gtk_box_append (GTK_BOX (inner_box), label2_box);
  gtk_box_append (GTK_BOX (self->tooltip_box), inner_box);
  gtk_widget_set_parent (self->tooltip_box, GTK_WIDGET (self));
}

GtkWidget *
bz_data_graph_new (void)
{
  return g_object_new (BZ_TYPE_DATA_GRAPH, NULL);
}

GListModel *
bz_data_graph_get_model (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), NULL);
  return self->model;
}

const char *
bz_data_graph_get_independent_axis_label (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), NULL);
  return self->independent_axis_label;
}

const char *
bz_data_graph_get_dependent_axis_label (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), NULL);
  return self->dependent_axis_label;
}

const char *
bz_data_graph_get_tooltip_prefix (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), NULL);
  return self->tooltip_prefix;
}

int
bz_data_graph_get_independent_decimals (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), 0);
  return self->independent_decimals;
}

int
bz_data_graph_get_dependent_decimals (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), 0);
  return self->dependent_decimals;
}

double
bz_data_graph_get_transition_progress (BzDataGraph *self)
{
  g_return_val_if_fail (BZ_IS_DATA_GRAPH (self), 0.0);
  return self->transition_progress;
}

void
bz_data_graph_set_model (BzDataGraph *self,
                         GListModel  *model)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model != NULL)
    g_signal_handlers_disconnect_by_func (
        self->model, items_changed, self);
  g_clear_object (&self->model);

  if (model != NULL)
    self->model = g_object_ref (model);

  self->allocated_width  = -1;
  self->allocated_height = -1;
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
}

void
bz_data_graph_set_independent_axis_label (BzDataGraph *self,
                                          const char  *independent_axis_label)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  g_clear_pointer (&self->independent_axis_label, g_free);
  if (independent_axis_label != NULL)
    self->independent_axis_label = g_strdup (independent_axis_label);

  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INDEPENDENT_AXIS_LABEL]);
}

void
bz_data_graph_set_dependent_axis_label (BzDataGraph *self,
                                        const char  *dependent_axis_label)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  g_clear_pointer (&self->dependent_axis_label, g_free);
  if (dependent_axis_label != NULL)
    self->dependent_axis_label = g_strdup (dependent_axis_label);

  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEPENDENT_AXIS_LABEL]);
}

void
bz_data_graph_set_tooltip_prefix (BzDataGraph *self,
                                  const char  *tooltip_prefix)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  g_clear_pointer (&self->tooltip_prefix, g_free);
  if (tooltip_prefix != NULL)
    self->tooltip_prefix = g_strdup (tooltip_prefix);

  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TOOLTIP_PREFIX]);
}

void
bz_data_graph_set_independent_decimals (BzDataGraph *self,
                                        int          independent_decimals)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  self->independent_decimals = CLAMP (independent_decimals, -1, 4);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INDEPENDENT_DECIMALS]);
}

void
bz_data_graph_set_dependent_decimals (BzDataGraph *self,
                                      int          dependent_decimals)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  self->dependent_decimals = CLAMP (dependent_decimals, -1, 4);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DEPENDENT_DECIMALS]);
}

void
bz_data_graph_set_transition_progress (BzDataGraph *self, double transition_progress)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  self->transition_progress = transition_progress;

  refresh_graph (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TRANSITION_PROGRESS]);
}

void
bz_data_graph_animate_open (BzDataGraph *self)
{
  g_return_if_fail (BZ_IS_DATA_GRAPH (self));

  self->wants_animate_open = TRUE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
items_changed (GListModel  *model,
               guint        position,
               guint        removed,
               guint        added,
               BzDataGraph *self)
{
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static double
calculate_axis_tick_value (double value, gboolean round_up)
{
  double exponent              = 0.0;
  double fraction              = 0.0;
  double rounded_axis_fraction = 0.0;

  exponent = floor (log10 (value));
  fraction = value / pow (10, exponent);

  if (round_up)
    {
      if (fraction <= 1.0)
        rounded_axis_fraction = 1.0;
      else if (fraction <= 2.0)
        rounded_axis_fraction = 2.0;
      else if (fraction <= 5.0)
        rounded_axis_fraction = 5.0;
      else
        rounded_axis_fraction = 10.0;
    }
  else if (fraction < 1.5)
    rounded_axis_fraction = 1.0;
  else if (fraction < 3.0)
    rounded_axis_fraction = 2.0;
  else if (fraction < 7.0)
    rounded_axis_fraction = 5.0;
  else
    rounded_axis_fraction = 10.0;

  return rounded_axis_fraction * pow (10, exponent);
}

static void
refresh_path (BzDataGraph *self,
              double       width,
              double       height)
{
  guint             n_items                = 0;
  double            min_independent        = 0.0;
  double            max_independent        = 0.0;
  double            max_dependent          = 0.0;
  double            sum_independent        = 0.0;
  double            sum_dependent          = 0.0;
  double            mean_independent       = 0.0;
  double            mean_dependent         = 0.0;
  PangoContext     *pango                  = NULL;
  PangoFontMetrics *metrics                = NULL;
  double            font_height            = 0.0;
  int               independent_label_step = 0;
  g_autoptr (GskPathBuilder) curve_builder = NULL;
  g_autoptr (GtkSnapshot) snapshot         = NULL;
  g_autoptr (GskPathBuilder) grid_builder  = NULL;
  g_autoptr (GskPath) grid                 = NULL;
  g_autoptr (GskStroke) grid_stroke        = NULL;
  double trend_slope_numerator             = 0.0;
  double trend_slope_denominator           = 0.0;
  double trend_slope                       = 0.0;
  double trend_intercept                   = 0.0;
  g_autoptr (GskPathBuilder) trend_builder = NULL;
  g_autoptr (GskPath) trend                = NULL;
  double rounded_axis_max                  = 0.0;
  double tick_spacing                      = 0.0;
  int    num_ticks                         = 0;

  g_clear_pointer (&self->path, gsk_path_unref);
  g_clear_pointer (&self->path_measure, gsk_path_measure_unref);
  g_clear_pointer (&self->trend, gsk_path_unref);
  g_clear_pointer (&self->trend_measure, gsk_path_measure_unref);
  g_clear_pointer (&self->other, gsk_render_node_unref);

  if (self->model == NULL)
    return;
  if (width < LABEL_MARGIN || height < LABEL_MARGIN)
    return;

  n_items = g_list_model_get_n_items (self->model);
  if (n_items <= 1)
    return;

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzDataPoint) point = NULL;
      double independent            = 0.0;
      double dependent              = 0.0;

      point       = g_list_model_get_item (self->model, i);
      independent = bz_data_point_get_independent (point);
      dependent   = bz_data_point_get_dependent (point);

      if (i == 0)
        {
          min_independent = independent;
          max_independent = independent;
          max_dependent   = dependent;
        }
      else
        {
          min_independent = MIN (independent, min_independent);
          max_independent = MAX (independent, max_independent);
          max_dependent   = MAX (dependent, max_dependent);
        }

      sum_independent += independent;
      sum_dependent += dependent;
    }

  rounded_axis_max = calculate_axis_tick_value (max_dependent, TRUE);

  mean_independent = sum_independent / (double) n_items;
  mean_dependent   = sum_dependent / (double) n_items;

  pango       = gtk_widget_get_pango_context (GTK_WIDGET (self));
  metrics     = pango_context_get_metrics (pango, NULL, NULL);
  font_height = (double) (int) PANGO_PIXELS_CEIL (pango_font_metrics_get_height (metrics));
  g_clear_pointer (&metrics, pango_font_metrics_unref);

  num_ticks = MIN (5, floor (height / (font_height + 25.0)));
  if (num_ticks < 2)
    num_ticks = 2;

  tick_spacing = calculate_axis_tick_value (rounded_axis_max / (double) num_ticks, FALSE);
  if (tick_spacing == 0.0)
    tick_spacing = 1.0;

  rounded_axis_max       = ceil (max_dependent / tick_spacing) * tick_spacing;
  self->rounded_axis_max = rounded_axis_max;

  independent_label_step = MAX (1, n_items / MAX (1, floor (width / MAX (font_height + 10.0, LABEL_MARGIN)))) * 1.5;

  curve_builder = gsk_path_builder_new ();
  snapshot      = gtk_snapshot_new ();
  grid_builder  = gsk_path_builder_new ();

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (LABEL_MARGIN, 0.0));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr (BzDataPoint) point = NULL;
      double independent            = 0.0;
      double dependent              = 0.0;
      double x                      = 0.0;
      double y                      = 0.0;

      point       = g_list_model_get_item (self->model, i);
      independent = bz_data_point_get_independent (point);
      dependent   = bz_data_point_get_dependent (point);

      x = (independent - min_independent) / (max_independent - min_independent) * width;
      y = (1.0 - dependent / rounded_axis_max) * height;

      if (i == 0)
        gsk_path_builder_move_to (curve_builder, x, y);
      else
        gsk_path_builder_line_to (curve_builder, x, y);

      if (i % independent_label_step == 0)
        {
          const char *label              = NULL;
          char        buf[32]            = { 0 };
          g_autoptr (PangoLayout) layout = NULL;
          PangoRectangle extents;

          label = bz_data_point_get_label (point);
          if (label == NULL)
            {
              switch (self->independent_decimals)
                {
                case 0:
                  g_snprintf (buf, sizeof (buf), "%d", (int) round (independent));
                  break;
                case 1:
                  g_snprintf (buf, sizeof (buf), "%.1f", independent);
                  break;
                case 2:
                  g_snprintf (buf, sizeof (buf), "%.2f", independent);
                  break;
                case 3:
                  g_snprintf (buf, sizeof (buf), "%.3f", independent);
                  break;
                default:
                  g_snprintf (buf, sizeof (buf), "%f", independent);
                  break;
                }
              label = buf;
            }

          layout = pango_layout_new (pango);
          pango_layout_set_text (layout, label, -1);

          pango_layout_get_pixel_extents (layout, NULL, &extents);

          gtk_snapshot_save (snapshot);
          gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, height + LABEL_MARGIN / 10.0));
          gtk_snapshot_rotate (snapshot, -LABEL_MARGIN_RIGHT);
          gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (-extents.width, 0));
          gtk_snapshot_append_layout (snapshot, layout, &(GdkRGBA){ 1.0, 1.0, 1.0, 1.0 });
          gtk_snapshot_restore (snapshot);

          gsk_path_builder_move_to (grid_builder, x, height);
          gsk_path_builder_line_to (grid_builder, x, height + TICK_LENGTH);
        }

      trend_slope_numerator += (independent - mean_independent) * (dependent - mean_dependent);
      trend_slope_denominator += (independent - mean_independent) * (independent - mean_independent);
    }

  gsk_path_builder_move_to (grid_builder, 0.0, height);
  gsk_path_builder_line_to (grid_builder, width, height);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (-LABEL_MARGIN * 0.75, -font_height / 2.0));

  for (double value = 0.0; value <= rounded_axis_max; value += tick_spacing)
    {
      char buf[32]                   = { 0 };
      g_autoptr (PangoLayout) layout = NULL;
      double y_pos                   = (1.0 - value / rounded_axis_max) * height;

      switch (self->dependent_decimals)
        {
        case 0:
          g_snprintf (buf, sizeof (buf), "%d", (int) round (value));
          break;
        case 1:
          g_snprintf (buf, sizeof (buf), "%.1f", value);
          break;
        case 2:
          g_snprintf (buf, sizeof (buf), "%.2f", value);
          break;
        case 3:
          g_snprintf (buf, sizeof (buf), "%.3f", value);
          break;
        default:
          g_snprintf (buf, sizeof (buf), "%f", value);
          break;
        }

      layout = pango_layout_new (pango);
      pango_layout_set_text (layout, buf, -1);

      gtk_snapshot_save (snapshot);
      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (0, y_pos));
      gtk_snapshot_append_layout (snapshot, layout, &(GdkRGBA){ 1.0, 1.0, 1.0, 1.0 });
      gtk_snapshot_restore (snapshot);

      gsk_path_builder_move_to (grid_builder, -TICK_LENGTH, y_pos);
      gsk_path_builder_line_to (grid_builder, 0.0, y_pos);
    }
  gtk_snapshot_restore (snapshot);

  gsk_path_builder_move_to (grid_builder, 0.0, 0.0);
  gsk_path_builder_line_to (grid_builder, 0.0, height);

  grid        = gsk_path_builder_to_path (grid_builder);
  grid_stroke = gsk_stroke_new (1.0);

  gtk_snapshot_append_stroke (snapshot, grid, grid_stroke, &(GdkRGBA){ 1.0, 1.0, 1.0, 1.0 });
  gtk_snapshot_restore (snapshot);

  if (!G_APPROX_VALUE (trend_slope_denominator, 0.0, 0.00001))
    trend_slope = trend_slope_numerator / trend_slope_denominator;
  trend_intercept = mean_dependent - trend_slope * mean_independent;

  trend_builder = gsk_path_builder_new ();
  gsk_path_builder_move_to (
      trend_builder,
      0.0,
      (1.0 - (trend_intercept + trend_slope * min_independent) /
                 rounded_axis_max) *
          height);
  gsk_path_builder_line_to (
      trend_builder,
      width,
      (1.0 - (trend_intercept + trend_slope * max_independent) /
                 rounded_axis_max) *
          height);

  self->path          = gsk_path_builder_to_path (curve_builder);
  self->path_measure  = gsk_path_measure_new (self->path);
  self->trend         = gsk_path_builder_to_path (trend_builder);
  self->trend_measure = gsk_path_measure_new (self->trend);
  self->other         = gtk_snapshot_to_node (snapshot);
}

static void
refresh_graph (BzDataGraph *self)
{
  double           widget_width           = 0.0;
  double           widget_height          = 0.0;
  AdwStyleManager *style_manager          = NULL;
  g_autoptr (GdkRGBA) accent_color        = NULL;
  GdkRGBA widget_color                    = { 0 };
  g_autoptr (GskPath) transitioning       = NULL;
  g_autoptr (GskPath) trend_transitioning = NULL;
  g_autoptr (GtkSnapshot) snapshot        = NULL;

  if (self->path == NULL)
    {
      g_clear_pointer (&self->graph, gsk_render_node_unref);
      return;
    }

  widget_width = gtk_widget_get_width (GTK_WIDGET (self));
  (void) widget_width;
  widget_height = gtk_widget_get_height (GTK_WIDGET (self));

  style_manager = adw_style_manager_get_default ();
  accent_color  = adw_style_manager_get_accent_color_rgba (style_manager);
  gtk_widget_get_color (GTK_WIDGET (self), &widget_color);

  if (self->transition_progress > 0.0 && self->transition_progress < 1.0)
    {
      GskPathPoint point0                = { 0 };
      double       path_distance         = 0.0;
      GskPathPoint point1                = { 0 };
      g_autoptr (GskPathBuilder) builder = NULL;

      gsk_path_get_start_point (self->path, &point0);
      path_distance = gsk_path_measure_get_length (self->path_measure) * self->transition_progress;
      gsk_path_measure_get_point (self->path_measure, path_distance, &point1);

      builder = gsk_path_builder_new ();
      gsk_path_builder_add_segment (builder, self->path, &point0, &point1);
      transitioning = gsk_path_builder_to_path (builder);
    }
  if (self->transition_progress > 0.0 && self->transition_progress < 1.0)
    {
      GskPathPoint point0                = { 0 };
      double       path_distance         = 0.0;
      GskPathPoint point1                = { 0 };
      g_autoptr (GskPathBuilder) builder = NULL;

      gsk_path_get_start_point (self->trend, &point0);
      path_distance = gsk_path_measure_get_length (self->trend_measure) * self->transition_progress;
      gsk_path_measure_get_point (self->trend_measure, path_distance, &point1);

      builder = gsk_path_builder_new ();
      gsk_path_builder_add_segment (builder, self->trend, &point0, &point1);
      trend_transitioning = gsk_path_builder_to_path (builder);
    }

  snapshot = gtk_snapshot_new ();
  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (LABEL_MARGIN, 0.0));

  if (self->transition_progress > 0.0)
    {
      g_autoptr (GskStroke) stroke       = NULL;
      g_autoptr (GskStroke) trend_stroke = NULL;
      g_autoptr (GskPathBuilder) builder = NULL;
      GskPathPoint     point0            = { 0 };
      GskPathPoint     point1            = { 0 };
      graphene_point_t start_position    = { 0 };
      graphene_point_t end_position      = { 0 };
      g_autoptr (GskPath) path           = NULL;

      const GskColorStop gradient_stops[] = {
        {
         0.0,
         {
         .red   = accent_color->red,
         .green = accent_color->green,
         .blue  = accent_color->blue,
         .alpha = 0.75,
         },
         },
        {
         1.0,
         {
         .red   = accent_color->red,
         .green = accent_color->green,
         .blue  = accent_color->blue,
         .alpha = 0.0,
         },
         },
      };
      const GskColorStop hfadeout_mask_stops[] = {
        {
         0.0,
         {
         .red   = 0.0,
         .green = 0.0,
         .blue  = 0.0,
         .alpha = 1.0,
         },
         },
        {
         0.75,
         {
         .red   = 0.0,
         .green = 0.0,
         .blue  = 0.0,
         .alpha = 1.0,
         },
         },
        {
         1.0,
         {
         .red   = 0.0,
         .green = 0.0,
         .blue  = 0.0,
         .alpha = 0.0,
         },
         },
      };
      const GdkRGBA trend_color = {
        .red   = widget_color.red,
        .green = widget_color.green,
        .blue  = widget_color.blue,
        .alpha = 0.8,
      };

      stroke = gsk_stroke_new (3.0);
      gsk_stroke_set_line_cap (stroke, GSK_LINE_CAP_ROUND);

      trend_stroke = gsk_stroke_new (2.0);
      gsk_stroke_set_line_cap (trend_stroke, GSK_LINE_CAP_ROUND);
      gsk_stroke_set_dash (trend_stroke, (const float[]){ 12, 16 }, 2);

      gtk_snapshot_append_stroke (
          snapshot,
          transitioning != NULL
              ? transitioning
              : self->path,
          stroke, accent_color);

      /* Gradient under the graph line */
      builder = gsk_path_builder_new ();

      gsk_path_get_start_point (
          transitioning != NULL
              ? transitioning
              : self->path,
          &point0);
      gsk_path_point_get_position (
          &point0,
          transitioning != NULL
              ? transitioning
              : self->path,
          &start_position);
      gsk_path_get_end_point (
          transitioning != NULL
              ? transitioning
              : self->path,
          &point1);
      gsk_path_point_get_position (
          &point1,
          transitioning != NULL
              ? transitioning
              : self->path,
          &end_position);

      gsk_path_builder_move_to (builder, start_position.x, start_position.y);
      gsk_path_builder_add_segment (
          builder,
          transitioning != NULL
              ? transitioning
              : self->path,
          &point0, &point1);
      gsk_path_builder_move_to (builder, end_position.x, end_position.y);

      /* close the loop for `gtk_snapshot_push_fill` */
      gsk_path_builder_line_to (builder, end_position.x, widget_height - LABEL_MARGIN);
      gsk_path_builder_line_to (builder, start_position.x, widget_height - LABEL_MARGIN);
      gsk_path_builder_line_to (builder, start_position.x, start_position.y);

      path = gsk_path_builder_to_path (builder);
      gtk_snapshot_push_fill (
          snapshot,
          path,
          GSK_FILL_RULE_WINDING);

      gtk_snapshot_push_mask (snapshot, GSK_MASK_MODE_ALPHA);

      gtk_snapshot_append_linear_gradient (
          snapshot,
          &GRAPHENE_RECT_INIT (
              0.0,
              0.0,
              end_position.x,
              widget_height - LABEL_MARGIN),
          &GRAPHENE_POINT_INIT (0.0, 0.0),
          &GRAPHENE_POINT_INIT (end_position.x, 0.0),
          hfadeout_mask_stops,
          G_N_ELEMENTS (hfadeout_mask_stops));
      gtk_snapshot_pop (snapshot); /* Push Mask (mask) */

      gtk_snapshot_append_linear_gradient (
          snapshot,
          &GRAPHENE_RECT_INIT (
              0.0,
              0.0,
              end_position.x,
              widget_height - LABEL_MARGIN),
          &GRAPHENE_POINT_INIT (0.0, 0.0),
          &GRAPHENE_POINT_INIT (0.0, widget_height - LABEL_MARGIN),
          gradient_stops,
          G_N_ELEMENTS (gradient_stops));

      gtk_snapshot_pop (snapshot); /* Push Mask (image) */
      gtk_snapshot_pop (snapshot); /* Push Fill */

      /* Trend line */
      gtk_snapshot_append_stroke (
          snapshot,
          trend_transitioning != NULL
              ? trend_transitioning
              : self->trend,
          trend_stroke, &trend_color);
    }
  gtk_snapshot_restore (snapshot);

  g_clear_pointer (&self->graph, gsk_render_node_unref);
  self->graph = gtk_snapshot_to_node (snapshot);
}

static void
refresh_overlay (BzDataGraph *self)
{
  double           widget_width          = 0.0;
  double           widget_height         = 0.0;
  GdkRGBA          widget_color          = { 0 };
  AdwStyleManager *style_manager         = NULL;
  g_autoptr (GdkRGBA) accent_color       = NULL;
  guint n_items                          = 0;
  guint hovered_idx                      = 0;
  g_autoptr (BzDataPoint) point          = NULL;
  g_autoptr (GskStroke) crosshair_stroke = NULL;
  double           graph_height          = 0.0;
  double           graph_width           = 0.0;
  double           fraction              = 0.0;
  double           point_x               = 0.0;
  double           point_y               = 0.0;
  GskRoundedRect   rounded_rect          = { { { 0 } } };
  GdkRGBA          line_color            = { 0 };
  double           rounded_axis_max      = 0.0;
  const char      *prefix                = NULL;
  g_autofree char *line2_text            = NULL;
  g_autoptr (GtkSnapshot) snapshot       = NULL;

  if (self->path == NULL)
    {
      g_clear_pointer (&self->overlay, gsk_render_node_unref);
      return;
    }

  widget_width  = gtk_widget_get_width (GTK_WIDGET (self));
  widget_height = gtk_widget_get_height (GTK_WIDGET (self));

  if (self->motion_x < LABEL_MARGIN ||
      self->motion_y < 0.0 ||
      self->motion_x >= widget_width - LABEL_MARGIN_RIGHT ||
      self->motion_y >= widget_height - LABEL_MARGIN)
    {
      g_clear_pointer (&self->overlay, gsk_render_node_unref);
      return;
    }

  gtk_widget_get_color (GTK_WIDGET (self), &widget_color);
  style_manager = adw_style_manager_get_default ();
  accent_color  = adw_style_manager_get_accent_color_rgba (style_manager);

  n_items     = g_list_model_get_n_items (self->model);
  graph_width = widget_width - LABEL_MARGIN - LABEL_MARGIN_RIGHT;
  fraction    = (self->motion_x - LABEL_MARGIN) / graph_width;
  hovered_idx = floor ((double) n_items * fraction);
  if (hovered_idx >= n_items)
    hovered_idx = n_items - 1;

  point = g_list_model_get_item (self->model, hovered_idx);

  if (self->rounded_axis_max > 0.0)
    rounded_axis_max = self->rounded_axis_max;
  else
    {
      double max_dependent = 0.0;
      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr (BzDataPoint) p = g_list_model_get_item (self->model, i);
          double dep                = bz_data_point_get_dependent (p);
          if (i == 0 || dep > max_dependent)
            max_dependent = dep;
        }
      rounded_axis_max = calculate_axis_tick_value (max_dependent, TRUE);
    }

  snapshot = gtk_snapshot_new ();

  graph_height = widget_height - LABEL_MARGIN;

  point_x = ((double) hovered_idx / (double) (n_items - 1)) * graph_width + LABEL_MARGIN;
  point_y = (1.0 - bz_data_point_get_dependent (point) / rounded_axis_max) * graph_height;

  line_color       = widget_color;
  line_color.alpha = 0.5;

  crosshair_stroke = gsk_stroke_new (1.0);

#define APPEND_LINE(x0, y0, x1, y1, color)                                  \
  G_STMT_START                                                              \
  {                                                                         \
    g_autoptr (GskPathBuilder) builder = NULL;                              \
    g_autoptr (GskPath) path           = NULL;                              \
                                                                            \
    builder = gsk_path_builder_new ();                                      \
    gsk_path_builder_move_to (builder, (x0), (y0));                         \
    gsk_path_builder_line_to (builder, (x1), (y1));                         \
                                                                            \
    path = gsk_path_builder_to_path (builder);                              \
    gtk_snapshot_append_stroke (snapshot, path, crosshair_stroke, (color)); \
  }                                                                         \
  G_STMT_END

  APPEND_LINE (self->motion_x, 0.0, self->motion_x, widget_height - LABEL_MARGIN, &line_color);

#undef APPEND_LINE

  gsk_rounded_rect_init_from_rect (
      &rounded_rect,
      &GRAPHENE_RECT_INIT (point_x - 4.0, point_y - 4.0, 8.0, 8.0),
      4.0);
  gtk_snapshot_push_rounded_clip (snapshot, &rounded_rect);
  gtk_snapshot_append_color (snapshot, accent_color, &rounded_rect.bounds);
  gtk_snapshot_pop (snapshot);

  g_clear_pointer (&self->overlay, gsk_render_node_unref);
  self->overlay = gtk_snapshot_to_node (snapshot);

  gtk_label_set_label (GTK_LABEL (self->tooltip_label1), bz_data_point_get_label (point));
  prefix = self->tooltip_prefix != NULL ? self->tooltip_prefix : "";
  gtk_label_set_label (GTK_LABEL (self->tooltip_prefix_label), prefix);
  line2_text = g_strdup_printf ("%'.0f", bz_data_point_get_dependent (point));
  gtk_label_set_label (GTK_LABEL (self->tooltip_label2), line2_text);
}
