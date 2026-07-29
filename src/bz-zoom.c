/* bz-zoom.c
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

#include <adwaita.h>
#include <math.h>

#include "bz-zoom.h"

#define OVERPAN_FACTOR 0.5

struct _BzZoom
{
  GtkWidget parent_instance;

  GtkWidget *child;

  double zoom_level;
  double min_zoom;
  double max_zoom;
  double pan_x;
  double pan_y;
  double mouse_x;
  double mouse_y;

  double gesture_start_zoom;
  double drag_start_x;
  double drag_start_y;

  gboolean is_dragging;

  AdwAnimation *zoom_animation;
  double        target_zoom;
  double        target_pan_x;
  double        target_pan_y;
  double        start_zoom;
  double        start_pan_x;
  double        start_pan_y;

  GtkGesture         *zoom_gesture;
  GtkGesture         *drag_gesture;
  GtkEventController *scroll_controller;
  GtkEventController *motion_controller;

  GtkAdjustment      *hadjustment;
  GtkAdjustment      *vadjustment;
  GtkScrollablePolicy hscroll_policy;
  GtkScrollablePolicy vscroll_policy;
};

static void
scrollable_iface_init (GtkScrollableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzZoom,
    bz_zoom,
    GTK_TYPE_WIDGET,
    G_IMPLEMENT_INTERFACE (GTK_TYPE_SCROLLABLE, scrollable_iface_init));

enum
{
  PROP_0,
  PROP_CHILD,
  PROP_ZOOM_LEVEL,
  PROP_MIN_ZOOM,
  PROP_MAX_ZOOM,
  LAST_PROP,

  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
  PROP_HSCROLL_POLICY,
  PROP_VSCROLL_POLICY,
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void bz_zoom_constrain_pan (BzZoom *self);
static void bz_zoom_zoom_at_point (BzZoom *self,
                                   double  zoom_factor,
                                   double  center_x,
                                   double  center_y);

static void
on_adjustment_value_changed (BzZoom        *self,
                             GParamSpec    *pspec,
                             GtkAdjustment *adjustment);

static void
bz_zoom_dispose (GObject *object)
{
  BzZoom *self;

  self = BZ_ZOOM (object);

  if (self->hadjustment != NULL)
    g_signal_handlers_disconnect_by_func (
        self->hadjustment, on_adjustment_value_changed, self);
  g_clear_object (&self->hadjustment);

  if (self->vadjustment != NULL)
    g_signal_handlers_disconnect_by_func (
        self->vadjustment, on_adjustment_value_changed, self);
  g_clear_object (&self->vadjustment);

  g_clear_pointer (&self->child, gtk_widget_unparent);
  g_clear_object (&self->zoom_animation);

  G_OBJECT_CLASS (bz_zoom_parent_class)->dispose (object);
}

static void
bz_zoom_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  BzZoom *self;

  self = BZ_ZOOM (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, bz_zoom_get_child (self));
      break;
    case PROP_ZOOM_LEVEL:
      g_value_set_double (value, bz_zoom_get_zoom_level (self));
      break;
    case PROP_MIN_ZOOM:
      g_value_set_double (value, bz_zoom_get_min_zoom (self));
      break;
    case PROP_MAX_ZOOM:
      g_value_set_double (value, bz_zoom_get_max_zoom (self));
      break;
    case PROP_HADJUSTMENT:
      g_value_set_object (value, self->hadjustment);
      break;
    case PROP_VADJUSTMENT:
      g_value_set_object (value, self->vadjustment);
      break;
    case PROP_HSCROLL_POLICY:
      g_value_set_enum (value, self->hscroll_policy);
      break;
    case PROP_VSCROLL_POLICY:
      g_value_set_enum (value, self->vscroll_policy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_zoom_set_property (GObject      *object,
                      guint         prop_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  BzZoom *self;

  self = BZ_ZOOM (object);

  switch (prop_id)
    {
    case PROP_CHILD:
      bz_zoom_set_child (self, g_value_get_object (value));
      break;
    case PROP_ZOOM_LEVEL:
      bz_zoom_set_zoom_level (self, g_value_get_double (value));
      break;
    case PROP_MIN_ZOOM:
      bz_zoom_set_min_zoom (self, g_value_get_double (value));
      break;
    case PROP_MAX_ZOOM:
      bz_zoom_set_max_zoom (self, g_value_get_double (value));
      break;
    case PROP_HADJUSTMENT:
      if (self->hadjustment != NULL)
        g_signal_handlers_disconnect_by_func (
            self->hadjustment, on_adjustment_value_changed, self);
      g_clear_object (&self->hadjustment);
      self->hadjustment = g_value_dup_object (value);
      if (self->hadjustment != NULL)
        {
          g_signal_connect_swapped (
              self->hadjustment, "notify::value",
              G_CALLBACK (on_adjustment_value_changed), self);
          self->pan_x = -gtk_adjustment_get_value (self->hadjustment);
        }
      bz_zoom_constrain_pan (self);
      gtk_widget_queue_draw (GTK_WIDGET (self));
      break;
    case PROP_VADJUSTMENT:
      if (self->vadjustment != NULL)
        g_signal_handlers_disconnect_by_func (
            self->vadjustment, on_adjustment_value_changed, self);
      g_clear_object (&self->vadjustment);
      self->vadjustment = g_value_dup_object (value);
      if (self->vadjustment != NULL)
        {
          g_signal_connect_swapped (
              self->vadjustment, "notify::value",
              G_CALLBACK (on_adjustment_value_changed), self);
          self->pan_y = -gtk_adjustment_get_value (self->vadjustment);
        }
      bz_zoom_constrain_pan (self);
      gtk_widget_queue_draw (GTK_WIDGET (self));
      break;
    case PROP_HSCROLL_POLICY:
      self->hscroll_policy = g_value_get_enum (value);
      break;
    case PROP_VSCROLL_POLICY:
      self->vscroll_policy = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
on_motion (BzZoom                   *self,
           double                    x,
           double                    y,
           GtkEventControllerMotion *controller)
{
  self->mouse_x = x;
  self->mouse_y = y;
}

static void
on_zoom_begin (BzZoom           *self,
               GdkEventSequence *sequence,
               GtkGesture       *gesture)
{
  self->gesture_start_zoom = self->zoom_level;
}

static void
on_zoom_changed (BzZoom     *self,
                 double      scale,
                 GtkGesture *gesture)
{
  double target_zoom;
  double zoom_factor;
  double center_x;
  double center_y;

  target_zoom = self->gesture_start_zoom * scale;

  if (self->zoom_level > 0)
    zoom_factor = target_zoom / self->zoom_level;
  else
    zoom_factor = 1.0;

  gtk_gesture_get_bounding_box_center (gesture, &center_x, &center_y);
  bz_zoom_zoom_at_point (self, zoom_factor, center_x, center_y);
}

static gboolean
on_scroll (BzZoom                   *self,
           double                    dx,
           double                    dy,
           GtkEventControllerScroll *controller)
{
  GdkEvent      *event;
  GdkDevice     *device;
  GdkInputSource source;
  double         zoom_factor;

  event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));
  if (event == NULL)
    return GDK_EVENT_PROPAGATE;

  device = gdk_event_get_device (event);
  if (device == NULL)
    return GDK_EVENT_PROPAGATE;

  source = gdk_device_get_source (device);

  if (source != GDK_SOURCE_MOUSE)
    return GDK_EVENT_PROPAGATE;

  zoom_factor = dy < 0 ? 1.1 : 0.9;
  bz_zoom_zoom_at_point (self, zoom_factor, self->mouse_x, self->mouse_y);
  return GDK_EVENT_STOP;
}

static void
on_drag_begin (BzZoom     *self,
               double      start_x,
               double      start_y,
               GtkGesture *gesture)
{
  if (!bz_zoom_is_transformed (self))
    {
      gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  self->drag_start_x = self->pan_x;
  self->drag_start_y = self->pan_y;
  self->is_dragging  = TRUE;
}

static void
on_drag_update (BzZoom     *self,
                double      offset_x,
                double      offset_y,
                GtkGesture *gesture)
{
  if (!self->is_dragging)
    return;

  self->pan_x = self->drag_start_x + offset_x;
  self->pan_y = self->drag_start_y + offset_y;
  bz_zoom_constrain_pan (self);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_drag_end (BzZoom     *self,
             double      offset_x,
             double      offset_y,
             GtkGesture *gesture)
{
  self->is_dragging = FALSE;
}

static void
bz_zoom_snapshot (GtkWidget   *widget,
                  GtkSnapshot *snapshot)
{
  BzZoom          *self;
  int              width;
  int              height;
  GskTransform    *transform;
  graphene_point_t point;

  self = BZ_ZOOM (widget);

  width  = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (width <= 0 || height <= 0)
    return;

  transform = NULL;

  graphene_point_init (&point, width / 2.0 + self->pan_x, height / 2.0 + self->pan_y);
  transform = gsk_transform_translate (transform, &point);

  /* See bz_zoom_size_allocate */
  // transform = gsk_transform_scale (transform, self->zoom_level, self->zoom_level);

  graphene_point_init (
      &point,
      -(width * self->zoom_level) / 2.0,
      -(height * self->zoom_level) / 2.0);
  transform = gsk_transform_translate (transform, &point);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_transform (snapshot, transform);

  if (self->child)
    gtk_widget_snapshot_child (widget, self->child, snapshot);

  gtk_snapshot_restore (snapshot);

  gsk_transform_unref (transform);
}

static void
bz_zoom_measure (GtkWidget     *widget,
                 GtkOrientation orientation,
                 int            for_size,
                 int           *minimum,
                 int           *natural,
                 int           *minimum_baseline,
                 int           *natural_baseline)
{
  BzZoom *self;

  self = BZ_ZOOM (widget);

  if (self->child)
    gtk_widget_measure (self->child, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline);
  else
    {
      *minimum = 0;
      *natural = 0;
    }
}

static void
bz_zoom_size_allocate (GtkWidget *widget,
                       int        width,
                       int        height,
                       int        baseline)
{
  BzZoom *self;

  self = BZ_ZOOM (widget);

  if (self->child)
    /* TODO: maybe add a property to control whether the child is artificially
       scaled? */
    gtk_widget_allocate (
        self->child,
        self->zoom_level * width,
        self->zoom_level * height,
        baseline,
        NULL);

  bz_zoom_constrain_pan (self);
}

static void
bz_zoom_class_init (BzZoomClass *klass)
{
  GObjectClass   *object_class;
  GtkWidgetClass *widget_class;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_zoom_dispose;
  object_class->get_property = bz_zoom_get_property;
  object_class->set_property = bz_zoom_set_property;

  widget_class->snapshot      = bz_zoom_snapshot;
  widget_class->measure       = bz_zoom_measure;
  widget_class->size_allocate = bz_zoom_size_allocate;

  props[PROP_CHILD] =
      g_param_spec_object (
          "child",
          NULL, NULL,
          GTK_TYPE_WIDGET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_ZOOM_LEVEL] =
      g_param_spec_double (
          "zoom-level",
          NULL, NULL,
          0.1, 10.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MIN_ZOOM] =
      g_param_spec_double (
          "min-zoom",
          NULL, NULL,
          0.1, 1.0, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_MAX_ZOOM] =
      g_param_spec_double (
          "max-zoom",
          NULL, NULL,
          1.0, 10.0, 5.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_object_class_override_property (object_class, PROP_HADJUSTMENT, "hadjustment");
  g_object_class_override_property (object_class, PROP_VADJUSTMENT, "vadjustment");
  g_object_class_override_property (object_class, PROP_HSCROLL_POLICY, "hscroll-policy");
  g_object_class_override_property (object_class, PROP_VSCROLL_POLICY, "vscroll-policy");
}

static void
bz_zoom_init (BzZoom *self)
{
  self->zoom_level         = 1.0;
  self->min_zoom           = 1.0;
  self->max_zoom           = 5.0;
  self->pan_x              = 0.0;
  self->pan_y              = 0.0;
  self->mouse_x            = 0.0;
  self->mouse_y            = 0.0;
  self->gesture_start_zoom = 1.0;
  self->is_dragging        = FALSE;

  self->zoom_gesture = gtk_gesture_zoom_new ();
  g_signal_connect_swapped (self->zoom_gesture, "begin", G_CALLBACK (on_zoom_begin), self);
  g_signal_connect_swapped (self->zoom_gesture, "scale-changed", G_CALLBACK (on_zoom_changed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->zoom_gesture));

  self->drag_gesture = gtk_gesture_drag_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->drag_gesture), GDK_BUTTON_PRIMARY);
  g_signal_connect_swapped (self->drag_gesture, "drag-begin", G_CALLBACK (on_drag_begin), self);
  g_signal_connect_swapped (self->drag_gesture, "drag-update", G_CALLBACK (on_drag_update), self);
  g_signal_connect_swapped (self->drag_gesture, "drag-end", G_CALLBACK (on_drag_end), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->drag_gesture));

  self->scroll_controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL);
  g_signal_connect_swapped (self->scroll_controller, "scroll", G_CALLBACK (on_scroll), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->scroll_controller);

  self->motion_controller = gtk_event_controller_motion_new ();
  g_signal_connect_swapped (self->motion_controller, "motion", G_CALLBACK (on_motion), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->motion_controller);

  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
}

static gboolean
scrollable_get_border (GtkScrollable *scrollable,
                       GtkBorder     *border)
{
  return FALSE;
}

static void
scrollable_iface_init (GtkScrollableInterface *iface)
{
  iface->get_border = scrollable_get_border;
}

GtkWidget *
bz_zoom_new (void)
{
  return g_object_new (BZ_TYPE_ZOOM, NULL);
}

GtkWidget *
bz_zoom_get_child (BzZoom *self)
{
  g_return_val_if_fail (BZ_IS_ZOOM (self), NULL);
  return self->child;
}

void
bz_zoom_set_child (BzZoom    *self,
                   GtkWidget *child)
{
  g_return_if_fail (BZ_IS_ZOOM (self));
  g_return_if_fail (child == NULL || GTK_IS_WIDGET (child));

  if (self->child == child)
    return;

  g_clear_pointer (&self->child, gtk_widget_unparent);

  self->child = child;

  if (self->child)
    gtk_widget_set_parent (self->child, GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CHILD]);
}

double
bz_zoom_get_zoom_level (BzZoom *self)
{
  g_return_val_if_fail (BZ_IS_ZOOM (self), 1.0);
  return self->zoom_level;
}

void
bz_zoom_set_zoom_level (BzZoom *self,
                        double  zoom_level)
{
  double new_zoom;

  g_return_if_fail (BZ_IS_ZOOM (self));

  new_zoom = CLAMP (zoom_level, self->min_zoom, self->max_zoom);

  if (fabs (new_zoom - self->zoom_level) < 0.001)
    return;

  self->zoom_level = new_zoom;
  bz_zoom_constrain_pan (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ZOOM_LEVEL]);
}

gboolean
bz_zoom_is_transformed (BzZoom *self)
{
  g_return_val_if_fail (BZ_IS_ZOOM (self), FALSE);
  return fabs (self->zoom_level - 1.0) >= 0.001 ||
         fabs (self->pan_x) >= 0.5 ||
         fabs (self->pan_y) >= 0.5;
}

double
bz_zoom_get_min_zoom (BzZoom *self)
{
  g_return_val_if_fail (BZ_IS_ZOOM (self), 1);
  return self->min_zoom;
}

void
bz_zoom_set_min_zoom (BzZoom *self,
                      double  min_zoom)
{
  g_return_if_fail (BZ_IS_ZOOM (self));

  self->min_zoom = min_zoom;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MIN_ZOOM]);
}

double
bz_zoom_get_max_zoom (BzZoom *self)
{
  g_return_val_if_fail (BZ_IS_ZOOM (self), 5.0);
  return self->max_zoom;
}

void
bz_zoom_set_max_zoom (BzZoom *self,
                      double  max_zoom)
{
  g_return_if_fail (BZ_IS_ZOOM (self));

  self->max_zoom = max_zoom;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MAX_ZOOM]);
}

static void
bz_zoom_constrain_pan (BzZoom *self)
{
  int    widget_width;
  int    widget_height;
  double overpan_x;
  double overpan_y;
  double range_x;
  double range_y;
  double max_pan_x;
  double max_pan_y;

  widget_width  = gtk_widget_get_width (GTK_WIDGET (self));
  widget_height = gtk_widget_get_height (GTK_WIDGET (self));

  if (widget_width <= 0 || widget_height <= 0)
    return;

  overpan_x = widget_width * OVERPAN_FACTOR;
  overpan_y = widget_height * OVERPAN_FACTOR;

  range_x = MAX (0, (widget_width * self->zoom_level - widget_width) / 2.0);
  range_y = MAX (0, (widget_height * self->zoom_level - widget_height) / 2.0);

  max_pan_x = range_x + overpan_x;
  max_pan_y = range_y + overpan_y;

  self->pan_x = CLAMP (self->pan_x, -max_pan_x, max_pan_x);
  self->pan_y = CLAMP (self->pan_y, -max_pan_y, max_pan_y);

  if (self->hadjustment != NULL)
    g_signal_handlers_block_by_func (
        self->hadjustment,
        on_adjustment_value_changed,
        self);
  if (self->vadjustment != NULL)
    g_signal_handlers_block_by_func (
        self->vadjustment,
        on_adjustment_value_changed,
        self);

  if (self->hadjustment != NULL)
    g_object_set (
        self->hadjustment,
        "lower", -max_pan_x,
        "value", -self->pan_x,
        "upper", max_pan_x + overpan_x,
        "page-size", overpan_x,
        "page-increment", max_pan_x / 4.0,
        NULL);
  if (self->vadjustment != NULL)
    g_object_set (
        self->vadjustment,
        "lower", -max_pan_y,
        "value", -self->pan_y,
        "upper", max_pan_y + overpan_y,
        "page-size", overpan_y,
        "page-increment", max_pan_y / 4.0,
        NULL);

  if (self->hadjustment != NULL)
    g_signal_handlers_unblock_by_func (
        self->hadjustment,
        on_adjustment_value_changed,
        self);
  if (self->vadjustment != NULL)
    g_signal_handlers_unblock_by_func (
        self->vadjustment,
        on_adjustment_value_changed,
        self);
}

static void
bz_zoom_zoom_at_point (BzZoom *self,
                       double  zoom_factor,
                       double  center_x,
                       double  center_y)
{
  double old_zoom;
  double new_zoom;
  int    widget_width;
  int    widget_height;
  double widget_center_x;
  double widget_center_y;
  double old_content_x;
  double old_content_y;
  double zoom_ratio;
  double new_content_x;
  double new_content_y;

  old_zoom = self->zoom_level;
  new_zoom = old_zoom * zoom_factor;
  new_zoom = CLAMP (new_zoom, self->min_zoom, self->max_zoom);

  if (fabs (new_zoom - old_zoom) < 0.001)
    return;

  widget_width  = gtk_widget_get_width (GTK_WIDGET (self));
  widget_height = gtk_widget_get_height (GTK_WIDGET (self));

  widget_center_x = widget_width / 2.0;
  widget_center_y = widget_height / 2.0;

  old_content_x = center_x - widget_center_x - self->pan_x;
  old_content_y = center_y - widget_center_y - self->pan_y;

  zoom_ratio    = new_zoom / old_zoom;
  new_content_x = old_content_x * zoom_ratio;
  new_content_y = old_content_y * zoom_ratio;

  self->zoom_level = new_zoom;

  self->pan_x = center_x - widget_center_x - new_content_x;
  self->pan_y = center_y - widget_center_y - new_content_y;

  bz_zoom_constrain_pan (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ZOOM_LEVEL]);
}

static void
on_animation_value (double  value,
                    BzZoom *self)
{
  self->zoom_level = self->start_zoom + (self->target_zoom - self->start_zoom) * value;
  self->pan_x      = self->start_pan_x + (self->target_pan_x - self->start_pan_x) * value;
  self->pan_y      = self->start_pan_y + (self->target_pan_y - self->start_pan_y) * value;

  bz_zoom_constrain_pan (self);
  gtk_widget_queue_resize (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ZOOM_LEVEL]);
}

static void
on_adjustment_value_changed (BzZoom        *self,
                             GParamSpec    *pspec,
                             GtkAdjustment *adjustment)
{
  if (self->zoom_animation != NULL)
    adw_animation_skip (self->zoom_animation);

  if (self->hadjustment != NULL)
    self->pan_x = -gtk_adjustment_get_value (self->hadjustment);

  if (self->vadjustment != NULL)
    self->pan_y = -gtk_adjustment_get_value (self->vadjustment);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
bz_zoom_animate_to (BzZoom *self,
                    double  target_zoom,
                    double  target_pan_x,
                    double  target_pan_y)
{
  AdwAnimationTarget *target;

  if (self->zoom_animation)
    adw_animation_skip (self->zoom_animation);

  self->start_zoom   = self->zoom_level;
  self->start_pan_x  = self->pan_x;
  self->start_pan_y  = self->pan_y;
  self->target_zoom  = target_zoom;
  self->target_pan_x = target_pan_x;
  self->target_pan_y = target_pan_y;

  target               = adw_callback_animation_target_new ((AdwAnimationTargetFunc) on_animation_value, self, NULL);
  self->zoom_animation = adw_timed_animation_new (GTK_WIDGET (self), 0, 1, 150, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->zoom_animation), ADW_EASE_OUT_CUBIC);
  adw_animation_play (self->zoom_animation);
}

void
bz_zoom_zoom_in (BzZoom *self)
{
  int    width;
  int    height;
  double old_zoom;
  double new_zoom;
  double widget_center_x;
  double widget_center_y;
  double center_x;
  double center_y;
  double old_content_x;
  double old_content_y;
  double zoom_ratio;
  double new_content_x;
  double new_content_y;
  double new_pan_x;
  double new_pan_y;

  g_return_if_fail (BZ_IS_ZOOM (self));

  width  = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  center_x = width / 2.0;
  center_y = height / 2.0;

  old_zoom = self->zoom_level;
  new_zoom = old_zoom * 1.2;
  new_zoom = CLAMP (new_zoom, self->min_zoom, self->max_zoom);

  if (fabs (new_zoom - old_zoom) < 0.001)
    return;

  widget_center_x = width / 2.0;
  widget_center_y = height / 2.0;

  old_content_x = center_x - widget_center_x - self->pan_x;
  old_content_y = center_y - widget_center_y - self->pan_y;

  zoom_ratio    = new_zoom / old_zoom;
  new_content_x = old_content_x * zoom_ratio;
  new_content_y = old_content_y * zoom_ratio;

  new_pan_x = center_x - widget_center_x - new_content_x;
  new_pan_y = center_y - widget_center_y - new_content_y;

  bz_zoom_animate_to (self, new_zoom, new_pan_x, new_pan_y);
}

void
bz_zoom_zoom_out (BzZoom *self)
{
  int    width;
  int    height;
  double old_zoom;
  double new_zoom;
  double widget_center_x;
  double widget_center_y;
  double center_x;
  double center_y;
  double old_content_x;
  double old_content_y;
  double zoom_ratio;
  double new_content_x;
  double new_content_y;
  double new_pan_x;
  double new_pan_y;

  g_return_if_fail (BZ_IS_ZOOM (self));

  width  = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));

  center_x = width / 2.0;
  center_y = height / 2.0;

  old_zoom = self->zoom_level;
  new_zoom = old_zoom * 0.8;
  new_zoom = CLAMP (new_zoom, self->min_zoom, self->max_zoom);

  if (fabs (new_zoom - old_zoom) < 0.001)
    return;

  widget_center_x = width / 2.0;
  widget_center_y = height / 2.0;

  old_content_x = center_x - widget_center_x - self->pan_x;
  old_content_y = center_y - widget_center_y - self->pan_y;

  zoom_ratio    = new_zoom / old_zoom;
  new_content_x = old_content_x * zoom_ratio;
  new_content_y = old_content_y * zoom_ratio;

  new_pan_x = center_x - widget_center_x - new_content_x;
  new_pan_y = center_y - widget_center_y - new_content_y;

  bz_zoom_animate_to (self, new_zoom, new_pan_x, new_pan_y);
}

void
bz_zoom_reset (BzZoom *self)
{
  g_return_if_fail (BZ_IS_ZOOM (self));

  bz_zoom_animate_to (self, 1.0, 0.0, 0.0);
}

void
bz_zoom_fit_to_window (BzZoom *self)
{
  g_return_if_fail (BZ_IS_ZOOM (self));

  bz_zoom_animate_to (self, 1.0, 0.0, 0.0);
}

/* End of bz-zoom.c */
