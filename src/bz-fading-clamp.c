/* bz-fading-clamp.c
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

#include <bge.h>

#include "bz-fading-clamp.h"
#include "util.h"

#define FADE_HEIGHT  75
#define CLAMP_LEEWAY 100

struct _BzFadingClamp
{
  GtkWidget parent_instance;

  BgeAnimation *animation;

  GtkWidget *child;

  int      max_height;
  int      min_max_height;
  int      current_height;
  int      allocated_width;
  gboolean will_change;
};

enum
{
  PROP_0,

  PROP_CHILD,
  PROP_MAX_HEIGHT,
  PROP_MIN_MAX_HEIGHT,
  PROP_WILL_CHANGE,

  N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (BzFadingClamp, bz_fading_clamp, GTK_TYPE_WIDGET)

static void
animate (BzFadingClamp *self,
         const char    *key,
         double         value,
         gpointer       user_data)
{
  self->current_height = round (value);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
bz_fading_clamp_update_will_change (BzFadingClamp *self)
{
  gboolean new_value = FALSE;
  int      natural_height;
  int      width;

  if (self->child)
    {
      if (self->allocated_width > 0)
        width = self->allocated_width;
      else
        gtk_widget_measure (self->child, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &width, NULL, NULL);

      gtk_widget_measure (self->child, GTK_ORIENTATION_VERTICAL, width, NULL, &natural_height, NULL, NULL);
      new_value = natural_height > self->min_max_height + CLAMP_LEEWAY;
    }

  if (self->will_change != new_value)
    {
      self->will_change = new_value;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WILL_CHANGE]);
    }
}

static void
bz_fading_clamp_dispose (GObject *object)
{
  BzFadingClamp *self = BZ_FADING_CLAMP (object);

  g_clear_object (&self->animation);
  g_clear_pointer (&self->child, gtk_widget_unparent);

  G_OBJECT_CLASS (bz_fading_clamp_parent_class)->dispose (object);
}

static void
bz_fading_clamp_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  BzFadingClamp *self = BZ_FADING_CLAMP (object);
  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, self->child);
      break;
    case PROP_MAX_HEIGHT:
      g_value_set_int (value, self->max_height);
      break;
    case PROP_MIN_MAX_HEIGHT:
      g_value_set_int (value, self->min_max_height);
      break;
    case PROP_WILL_CHANGE:
      g_value_set_boolean (value, self->will_change);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_fading_clamp_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  BzFadingClamp *self = BZ_FADING_CLAMP (object);
  switch (prop_id)
    {
    case PROP_CHILD:
      bz_fading_clamp_set_child (self, g_value_get_object (value));
      break;
    case PROP_MAX_HEIGHT:
      bz_fading_clamp_set_max_height (self, g_value_get_int (value));
      break;
    case PROP_MIN_MAX_HEIGHT:
      self->min_max_height = g_value_get_int (value);
      bz_fading_clamp_update_will_change (self);
      gtk_widget_queue_resize (GTK_WIDGET (self));
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_MAX_HEIGHT]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GtkSizeRequestMode
bz_fading_clamp_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
bz_fading_clamp_measure (GtkWidget     *widget,
                         GtkOrientation orientation,
                         int            for_size,
                         int           *minimum,
                         int           *natural,
                         int           *minimum_baseline,
                         int           *natural_baseline)
{
  int            target_height = 0;
  BzFadingClamp *self          = BZ_FADING_CLAMP (widget);

  if (!self->child)
    {
      *minimum = 0;
      *natural = 0;
      return;
    }

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_widget_measure (self->child, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline);
  else
    {
      gtk_widget_measure (self->child, GTK_ORIENTATION_VERTICAL, for_size, minimum, natural, minimum_baseline, natural_baseline);

      if (*natural <= self->max_height + CLAMP_LEEWAY)
        target_height = *natural;
      else
        target_height = self->max_height;

      bz_fading_clamp_update_will_change (self);

      if (!bge_animation_has_key (self->animation, "height"))
        self->current_height = target_height;

      *minimum = self->current_height;
      *natural = self->current_height;
    }
}

static void
bz_fading_clamp_size_allocate (GtkWidget *widget,
                               int        width,
                               int        height,
                               int        baseline)
{
  BzFadingClamp *self = BZ_FADING_CLAMP (widget);

  if (self->allocated_width != width)
    {
      self->allocated_width = width;
      bz_fading_clamp_update_will_change (self);
    }

  if (self->child)
    {
      int child_height   = 0;
      int natural_height = 0;

      gtk_widget_measure (self->child, GTK_ORIENTATION_VERTICAL, width, NULL, &natural_height, NULL, NULL);

      if (natural_height > height)
        child_height = natural_height;
      else
        child_height = height;
      gtk_widget_allocate (self->child, width, child_height, baseline, NULL);
    }
}

static void
bz_fading_clamp_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  BzFadingClamp   *self = BZ_FADING_CLAMP (widget);
  int              width, height, natural_height;
  int              gradient_start;
  graphene_rect_t  gradient_rect;
  GskColorStop     stops[2];
  graphene_point_t start_point, end_point;
  int              effective_fade_height;
  float            stop_offset;

  if (!self->child || !gtk_widget_get_visible (self->child))
    return;

  width  = gtk_widget_get_width (widget);
  height = gtk_widget_get_height (widget);

  if (height <= 0)
    return;

  gtk_widget_measure (self->child, GTK_ORIENTATION_VERTICAL, width, NULL, &natural_height, NULL, NULL);

  if (natural_height <= height)
    {
      gtk_widget_snapshot_child (widget, self->child, snapshot);
      return;
    }

  effective_fade_height = MIN (natural_height - height, FADE_HEIGHT);

  gtk_snapshot_push_mask (snapshot, GSK_MASK_MODE_ALPHA);

  gradient_start = height - effective_fade_height;
  graphene_rect_init (&gradient_rect, 0, 0, width, height);

  stop_offset = CLAMP ((float) gradient_start / height, 0.0f, 1.0f);

  stops[0] = (GskColorStop){
    stop_offset, { 1, 1, 1, 1 }
  };
  stops[1] = (GskColorStop){
    1.0, { 1, 1, 1, 0 }
  };

  graphene_point_init (&start_point, 0, 0);
  graphene_point_init (&end_point, 0, height);
  gtk_snapshot_append_linear_gradient (snapshot, &gradient_rect, &start_point, &end_point, stops, 2);

  gtk_snapshot_pop (snapshot);

  gtk_widget_snapshot_child (widget, self->child, snapshot);

  gtk_snapshot_pop (snapshot);
}

static void
bz_fading_clamp_class_init (BzFadingClampClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose          = bz_fading_clamp_dispose;
  object_class->get_property     = bz_fading_clamp_get_property;
  object_class->set_property     = bz_fading_clamp_set_property;
  widget_class->get_request_mode = bz_fading_clamp_get_request_mode;
  widget_class->measure          = bz_fading_clamp_measure;
  widget_class->size_allocate    = bz_fading_clamp_size_allocate;
  widget_class->snapshot         = bz_fading_clamp_snapshot;

  properties[PROP_CHILD] =
      g_param_spec_object ("child", NULL, NULL, GTK_TYPE_WIDGET,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  properties[PROP_MAX_HEIGHT] =
      g_param_spec_int ("max-height", NULL, NULL, 0, G_MAXINT, 300,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  properties[PROP_MIN_MAX_HEIGHT] =
      g_param_spec_int ("min-max-height", NULL, NULL, 0, G_MAXINT, 150,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  properties[PROP_WILL_CHANGE] =
      g_param_spec_boolean ("will-change", NULL, NULL, FALSE,
                            G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
bz_fading_clamp_init (BzFadingClamp *self)
{
  self->animation = bge_animation_new (GTK_WIDGET (self));

  self->max_height      = 300;
  self->min_max_height  = 150;
  self->current_height  = CLAMP_LEEWAY;
  self->allocated_width = 0;
  self->will_change     = FALSE;
}

GtkWidget *
bz_fading_clamp_new (void)
{
  return g_object_new (BZ_TYPE_FADING_CLAMP, NULL);
}

void
bz_fading_clamp_set_child (BzFadingClamp *self, GtkWidget *child)
{
  g_return_if_fail (BZ_IS_FADING_CLAMP (self));
  g_return_if_fail (child == NULL || GTK_IS_WIDGET (child));

  if (self->child == child)
    return;

  g_clear_pointer (&self->child, gtk_widget_unparent);

  if (child)
    {
      self->child = child;
      gtk_widget_set_parent (child, GTK_WIDGET (self));
      self->current_height = 0;
    }

  bz_fading_clamp_update_will_change (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CHILD]);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

GtkWidget *
bz_fading_clamp_get_child (BzFadingClamp *self)
{
  g_return_val_if_fail (BZ_IS_FADING_CLAMP (self), NULL);
  return self->child;
}

void
bz_fading_clamp_set_max_height (BzFadingClamp *self, int max_height)
{
  int natural_height = 0;
  int target_height  = 0;
  int width          = 0;

  g_return_if_fail (BZ_IS_FADING_CLAMP (self));

  if (self->max_height == max_height)
    return;

  self->max_height = max_height;

  if (self->child)
    {
      if (self->allocated_width > 0)
        width = self->allocated_width;
      else
        gtk_widget_measure (self->child, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &width, NULL, NULL);

      gtk_widget_measure (self->child, GTK_ORIENTATION_VERTICAL, width, NULL, &natural_height, NULL, NULL);

      if (natural_height <= max_height + CLAMP_LEEWAY)
        target_height = natural_height;
      else
        target_height = max_height;
    }
  else
    target_height = max_height;

  bge_animation_add_spring (
      self->animation, "height",
      self->current_height, target_height,
      1.25, 1.0, 800.0,
      (BgeAnimationCallback) animate,
      NULL, NULL, NULL);

  bz_fading_clamp_update_will_change (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HEIGHT]);
}

int
bz_fading_clamp_get_max_height (BzFadingClamp *self)
{
  g_return_val_if_fail (BZ_IS_FADING_CLAMP (self), 0);
  return self->max_height;
}
