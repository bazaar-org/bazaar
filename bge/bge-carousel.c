/* bge-carousel.c
 *
 * Copyright 2026 Eva M
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

/**
 * BgeCarousel:
 *
 * Arranges widgets into a horizontal carousel
 */

#define G_LOG_DOMAIN "BGE::CAROUSEL"

#include "bge.h"

#include "bge-marshalers.h"
#include "util.h"

#define RAISE_FACTOR 0.025

/* `ratio < 1.0` means it overshoots */
#define ANIMATION_DAMPING_RATIO 1.15

struct _BgeCarousel
{
  GtkWidget parent_instance;

  GtkEventController *motion;
  double              motion_x;
  double              motion_y;
  GtkEventController *scroll;
  gboolean            scrolling;
  int                 hscroll_start;
  int                 hscroll_current;
  GtkGesture         *drag;
  gboolean            dragging;
  double              drag_x;
  double              drag_y;
  BgeAnimation       *animation;

  gboolean            allow_mouse_drag;
  gboolean            allow_overshoot;
  gboolean            allow_scroll_wheel;
  gboolean            allow_raise;
  GtkSingleSelection *model;

  GPtrArray *mirror;
  GPtrArray *widgets;
};

G_DEFINE_FINAL_TYPE (BgeCarousel, bge_carousel, GTK_TYPE_WIDGET);

enum
{
  PROP_0,

  PROP_ALLOW_MOUSE_DRAG,
  PROP_ALLOW_OVERSHOOT,
  PROP_ALLOW_SCROLL_WHEEL,
  PROP_ALLOW_RAISE,
  PROP_MODEL,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_CREATE_WIDGET,
  SIGNAL_REMOVE_WIDGET,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

BGE_DEFINE_DATA (
    carousel_widget,
    CarouselWidget,
    {
      GtkWidget *widget;
      int        width;

      graphene_rect_t rect;
      graphene_rect_t target;

      gboolean raised;

      GCancellable *cancellable;
    },
    BGE_RELEASE_DATA (widget, gtk_widget_unparent);
    BGE_RELEASE_DATA (cancellable, g_object_unref))

static void
items_changed (BgeCarousel *self,
               guint        position,
               guint        removed,
               guint        added,
               GListModel  *model);

static void
model_selected_changed (BgeCarousel        *self,
                        GParamSpec         *pspec,
                        GtkSingleSelection *selection);

static void
move_to_idx (BgeCarousel *self,
             guint        idx,
             /* damping_ratio <= 0.0 means no animation */
             double damping_ratio);

static void
animate (BgeCarousel        *self,
         const char         *key,
         double              value,
         CarouselWidgetData *data);

static void
ensure_viewport (BgeCarousel        *self,
                 GtkSingleSelection *model,
                 gboolean            animate);

static void
motion_enter (BgeCarousel              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller);

static void
motion_event (BgeCarousel              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller);

static void
motion_leave (BgeCarousel              *self,
              GtkEventControllerMotion *controller);

static void
update_motion (BgeCarousel *self,
               gdouble      x,
               gdouble      y);

static void
scroll_begin (BgeCarousel              *self,
              GtkEventControllerScroll *controller);

static void
scroll_end (BgeCarousel              *self,
            GtkEventControllerScroll *controller);

static gboolean
scroll (BgeCarousel              *self,
        gdouble                   dx,
        gdouble                   dy,
        GtkEventControllerScroll *controller);

static void
drag_begin (BgeCarousel    *self,
            gdouble         start_x,
            gdouble         start_y,
            GtkGestureDrag *gesture);

static void
drag_end (BgeCarousel    *self,
          gdouble         offset_x,
          gdouble         offset_y,
          GtkGestureDrag *gesture);

static void
drag_update (BgeCarousel    *self,
             gdouble         offset_x,
             gdouble         offset_y,
             GtkGestureDrag *gesture);

static void
finish_horizontal_gesture (BgeCarousel *self,
                           int          offset_x,
                           int          offset_y);

static void
bge_carousel_dispose (GObject *object)
{
  BgeCarousel *self = BGE_CAROUSEL (object);

  if (self->model != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->model, items_changed, self);
      g_signal_handlers_disconnect_by_func (self->model, model_selected_changed, self);
    }

  g_clear_pointer (&self->animation, g_object_unref);
  g_clear_pointer (&self->model, g_object_unref);

  g_clear_pointer (&self->mirror, g_ptr_array_unref);
  g_clear_pointer (&self->widgets, g_ptr_array_unref);

  G_OBJECT_CLASS (bge_carousel_parent_class)->dispose (object);
}

static void
bge_carousel_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BgeCarousel *self = BGE_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_ALLOW_MOUSE_DRAG:
      g_value_set_boolean (value, bge_carousel_get_allow_mouse_drag (self));
      break;
    case PROP_ALLOW_OVERSHOOT:
      g_value_set_boolean (value, bge_carousel_get_allow_overshoot (self));
      break;
    case PROP_ALLOW_SCROLL_WHEEL:
      g_value_set_boolean (value, bge_carousel_get_allow_scroll_wheel (self));
      break;
    case PROP_ALLOW_RAISE:
      g_value_set_boolean (value, bge_carousel_get_allow_raise (self));
      break;
    case PROP_MODEL:
      g_value_set_object (value, bge_carousel_get_model (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_carousel_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BgeCarousel *self = BGE_CAROUSEL (object);

  switch (prop_id)
    {
    case PROP_ALLOW_MOUSE_DRAG:
      bge_carousel_set_allow_mouse_drag (self, g_value_get_boolean (value));
      break;
    case PROP_ALLOW_OVERSHOOT:
      bge_carousel_set_allow_overshoot (self, g_value_get_boolean (value));
      break;
    case PROP_ALLOW_SCROLL_WHEEL:
      bge_carousel_set_allow_scroll_wheel (self, g_value_get_boolean (value));
      break;
    case PROP_ALLOW_RAISE:
      bge_carousel_set_allow_raise (self, g_value_get_boolean (value));
      break;
    case PROP_MODEL:
      bge_carousel_set_model (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_carousel_measure (GtkWidget     *widget,
                      GtkOrientation orientation,
                      int            for_size,
                      int           *minimum,
                      int           *natural,
                      int           *minimum_baseline,
                      int           *natural_baseline)
{
  BgeCarousel *self = BGE_CAROUSEL (widget);

  for (guint i = 0; i < self->widgets->len; i++)
    {
      CarouselWidgetData *child                = NULL;
      int                 tmp_minimum          = 0;
      int                 tmp_natural          = 0;
      int                 tmp_minimum_baseline = 0;
      int                 tmp_natural_baseline = 0;

      child = g_ptr_array_index (self->widgets, i);

      gtk_widget_measure (
          child->widget,
          orientation,
          for_size,
          &tmp_minimum,
          &tmp_natural,
          &tmp_minimum_baseline,
          &tmp_natural_baseline);

      if (tmp_minimum > 0 && tmp_minimum > *minimum)
        *minimum = tmp_minimum;
      if (tmp_natural > 0 && tmp_natural > *natural)
        *natural = tmp_natural;
      if (tmp_minimum_baseline > 0 && tmp_minimum_baseline > *minimum_baseline)
        *minimum_baseline = tmp_minimum_baseline;
      if (tmp_natural_baseline > 0 && tmp_natural_baseline > *natural_baseline)
        *natural_baseline = tmp_natural_baseline;
    }
}

static void
bge_carousel_size_allocate (GtkWidget *widget,
                            int        width,
                            int        height,
                            int        baseline)
{
  BgeCarousel *self = BGE_CAROUSEL (widget);

  ensure_viewport (self, self->model, FALSE);

  for (guint i = 0; i < self->widgets->len; i++)
    {
      CarouselWidgetData *child          = NULL;
      g_autoptr (GskTransform) transform = NULL;

      child     = g_ptr_array_index (self->widgets, i);
      transform = gsk_transform_translate (
          gsk_transform_new (),
          &child->rect.origin);

      gtk_widget_allocate (
          child->widget,
          child->rect.size.width,
          child->rect.size.height,
          baseline,
          g_steal_pointer (&transform));
    }
}

static void
bge_carousel_class_init (BgeCarouselClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bge_carousel_set_property;
  object_class->get_property = bge_carousel_get_property;
  object_class->dispose      = bge_carousel_dispose;

  /**
   * BgeCarousel:allow-mouse-drag:
   *
   * Whether to allow dragging with the mouse.
   */
  props[PROP_ALLOW_MOUSE_DRAG] =
      g_param_spec_boolean (
          "allow-mouse-drag",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeCarousel:allow-overshoot:
   *
   * Whether to allow overshooting the ends of the carousel with drag/touchpad
   * input. Once the event completes, the carousel offset value will go back to
   * that of the start/end widget. Setting this value to FALSE will prevent this
   * widget from capturing input events which would result in an overshoot.
   */
  props[PROP_ALLOW_OVERSHOOT] =
      g_param_spec_boolean (
          "allow-overshoot",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeCarousel:allow-scroll-wheel:
   *
   * Whether to allow moving the carousel contents with the horizontal scroll
   * wheel.
   */
  props[PROP_ALLOW_SCROLL_WHEEL] =
      g_param_spec_boolean (
          "allow-scroll-wheel",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeCarousel:allow-raise:
   *
   * Whether to allow raise animations when motion input events hover over them
   * carousel widgets.
   */
  props[PROP_ALLOW_RAISE] =
      g_param_spec_boolean (
          "allow-raise",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * BgeCarousel:model:
   *
   * The selection model for the carousel contents.
   */
  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          GTK_TYPE_SINGLE_SELECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * BgeCarousel::create-widget:
   * @carousel: the object that received the signal
   * @object: a list item object from [property@Bge.Carousel:model]
   *
   * Emitted when an object is being bound to the carousel
   *
   * Return: a newly allocated widget to add to the carousel
   */
  signals[SIGNAL_CREATE_WIDGET] =
      g_signal_new (
          "create-widget",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          bge_marshal_OBJECT__OBJECT,
          GTK_TYPE_WIDGET,
          1,
          G_TYPE_OBJECT);
  g_signal_set_va_marshaller (
      signals[SIGNAL_CREATE_WIDGET],
      G_TYPE_FROM_CLASS (klass),
      bge_marshal_OBJECT__OBJECTv);

  /**
   * BgeCarousel::remove-widget:
   * @carousel: the object that received the signal
   * @widget: the widget which was created by [signal@Bge.Carousel::create-widget]
   * @object: a list item object from [property@Bge.Carousel:model]
   *
   * Emitted when an object is being unbound from the carousel
   */
  signals[SIGNAL_REMOVE_WIDGET] =
      g_signal_new (
          "remove-widget",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          bge_marshal_VOID__OBJECT_OBJECT,
          G_TYPE_NONE,
          2,
          GTK_TYPE_WIDGET, G_TYPE_OBJECT);
  g_signal_set_va_marshaller (
      signals[SIGNAL_REMOVE_WIDGET],
      G_TYPE_FROM_CLASS (klass),
      bge_marshal_VOID__OBJECT_OBJECTv);

  widget_class->measure       = bge_carousel_measure;
  widget_class->size_allocate = bge_carousel_size_allocate;
}

static void
bge_carousel_init (BgeCarousel *self)
{
  self->animation = bge_animation_new (GTK_WIDGET (self));

  self->mirror = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_object_unref);
  self->widgets = g_ptr_array_new_with_free_func (
      carousel_widget_data_unref);

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);

  self->motion = gtk_event_controller_motion_new ();
  g_signal_connect_swapped (self->motion, "enter", G_CALLBACK (motion_enter), self);
  g_signal_connect_swapped (self->motion, "motion", G_CALLBACK (motion_event), self);
  g_signal_connect_swapped (self->motion, "leave", G_CALLBACK (motion_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->motion);

  self->scroll = gtk_event_controller_scroll_new (
      GTK_EVENT_CONTROLLER_SCROLL_HORIZONTAL |
      GTK_EVENT_CONTROLLER_SCROLL_KINETIC);
  g_signal_connect_swapped (self->scroll, "scroll-begin", G_CALLBACK (scroll_begin), self);
  g_signal_connect_swapped (self->scroll, "scroll-end", G_CALLBACK (scroll_end), self);
  g_signal_connect_swapped (self->scroll, "scroll", G_CALLBACK (scroll), self);
  gtk_widget_add_controller (GTK_WIDGET (self), self->scroll);

  self->drag = gtk_gesture_drag_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (self->drag), GTK_PHASE_CAPTURE);
  g_signal_connect_swapped (self->drag, "drag-begin", G_CALLBACK (drag_begin), self);
  g_signal_connect_swapped (self->drag, "drag-end", G_CALLBACK (drag_end), self);
  g_signal_connect_swapped (self->drag, "drag-update", G_CALLBACK (drag_update), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (self->drag));
}

/**
 * bge_carousel_new:
 *
 * Creates a new `BgeCarousel` object.
 *
 * Returns: The newly created `BgeCarousel` object.
 */
GtkWidget *
bge_carousel_new (void)
{
  return g_object_new (BGE_TYPE_CAROUSEL, NULL);
}

/**
 * bge_carousel_get_allow_mouse_drag:
 * @self: a `BgeCarousel`
 *
 * Gets [property@Bge.Carousel:allow-mouse-drag].
 *
 * Returns: the value of the property
 */
gboolean
bge_carousel_get_allow_mouse_drag (BgeCarousel *self)
{
  g_return_val_if_fail (BGE_IS_CAROUSEL (self), FALSE);
  return self->allow_mouse_drag;
}

/**
 * bge_carousel_get_allow_overshoot:
 * @self: a `BgeCarousel`
 *
 * Gets [property@Bge.Carousel:allow-overshoot].
 *
 * Returns: the value of the property
 */
gboolean
bge_carousel_get_allow_overshoot (BgeCarousel *self)
{
  g_return_val_if_fail (BGE_IS_CAROUSEL (self), FALSE);
  return self->allow_overshoot;
}

/**
 * bge_carousel_get_allow_scroll_wheel:
 * @self: a `BgeCarousel`
 *
 * Gets [property@Bge.Carousel:allow-scroll-wheel].
 *
 * Returns: the value of the property
 */
gboolean
bge_carousel_get_allow_scroll_wheel (BgeCarousel *self)
{
  g_return_val_if_fail (BGE_IS_CAROUSEL (self), FALSE);
  return self->allow_scroll_wheel;
}

/**
 * bge_carousel_get_allow_raise:
 * @self: a `BgeCarousel`
 *
 * Gets [property@Bge.Carousel:allow-raise].
 *
 * Returns: the value of the property
 */
gboolean
bge_carousel_get_allow_raise (BgeCarousel *self)
{
  g_return_val_if_fail (BGE_IS_CAROUSEL (self), FALSE);
  return self->allow_raise;
}

/**
 * bge_carousel_get_model:
 * @self: a `BgeCarousel`
 *
 * Gets [property@Bge.Carousel:model].
 *
 * Returns: (nullable): the value of the property
 */
GtkSingleSelection *
bge_carousel_get_model (BgeCarousel *self)
{
  g_return_val_if_fail (BGE_IS_CAROUSEL (self), NULL);
  return self->model;
}

/**
 * bge_carousel_get_nth_page:
 * @self: a `BgeCarousel`
 * @index: Index of the page.
 *
 * Returns: (nullable) (transfer none): the page at @index, or NULL if out of bounds
 */
GtkWidget *
bge_carousel_get_nth_page (BgeCarousel *self,
                           guint        index)
{
  CarouselWidgetData *data = NULL;

  g_return_val_if_fail (BGE_IS_CAROUSEL (self), NULL);

  if (index >= self->widgets->len)
    return NULL;

  data = g_ptr_array_index (self->widgets, index);
  return data->widget;
}

/**
 * bge_carousel_set_allow_mouse_drag:
 * @self: a `BgeCarousel`
 * @allow_mouse_drag: a boolean
 *
 * Sets [property@Bge.Carousel:allow-mouse-drag].
 */
void
bge_carousel_set_allow_mouse_drag (BgeCarousel *self,
                                   gboolean     allow_mouse_drag)
{
  g_return_if_fail (BGE_IS_CAROUSEL (self));

  if (!!allow_mouse_drag == !!self->allow_mouse_drag)
    return;

  self->allow_mouse_drag = allow_mouse_drag;
  if (!allow_mouse_drag && self->dragging)
    {
      self->dragging = FALSE;
      finish_horizontal_gesture (self, self->drag_x, self->drag_y);
      self->drag_x = 0.0;
      self->drag_y = 0.0;
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_MOUSE_DRAG]);
}

/**
 * bge_carousel_set_allow_overshoot:
 * @self: a `BgeCarousel`
 * @allow_overshoot: a boolean
 *
 * Sets [property@Bge.Carousel:allow-overshoot].
 */
void
bge_carousel_set_allow_overshoot (BgeCarousel *self,
                                  gboolean     allow_overshoot)
{
  g_return_if_fail (BGE_IS_CAROUSEL (self));

  if (!!allow_overshoot == !!self->allow_overshoot)
    return;

  self->allow_overshoot = allow_overshoot;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_OVERSHOOT]);
}

/**
 * bge_carousel_set_allow_scroll_wheel:
 * @self: a `BgeCarousel`
 * @allow_scroll_wheel: a boolean
 *
 * Sets [property@Bge.Carousel:allow-scroll-wheel].
 */
void
bge_carousel_set_allow_scroll_wheel (BgeCarousel *self,
                                     gboolean     allow_scroll_wheel)
{
  g_return_if_fail (BGE_IS_CAROUSEL (self));

  if (!!allow_scroll_wheel == !!self->allow_scroll_wheel)
    return;

  self->allow_scroll_wheel = allow_scroll_wheel;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_SCROLL_WHEEL]);
}

/**
 * bge_carousel_set_allow_raise:
 * @self: a `BgeCarousel`
 * @allow_raise: a boolean
 *
 * Sets [property@Bge.Carousel:allow-raise].
 */
void
bge_carousel_set_allow_raise (BgeCarousel *self,
                              gboolean     allow_raise)
{
  g_return_if_fail (BGE_IS_CAROUSEL (self));

  if (!!allow_raise == !!self->allow_raise)
    return;

  self->allow_raise = allow_raise;
  if (self->model != NULL)
    ensure_viewport (self, self->model, TRUE);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_RAISE]);
}

/**
 * bge_carousel_set_model:
 * @self: a `BgeCarousel`
 * @model: a `GtkSingleSelection` object
 *
 * Sets [property@Bge.Carousel:model].
 */
void
bge_carousel_set_model (BgeCarousel        *self,
                        GtkSingleSelection *model)
{
  g_return_if_fail (BGE_IS_CAROUSEL (self));
  g_return_if_fail (model == NULL || GTK_IS_SINGLE_SELECTION (model));

  if (model == self->model)
    return;

  if (self->model != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->model, items_changed, self);
      g_signal_handlers_disconnect_by_func (self->model, model_selected_changed, self);
      items_changed (
          self,
          0,
          g_list_model_get_n_items (G_LIST_MODEL (self->model)),
          0,
          G_LIST_MODEL (self->model));
    }
  g_clear_pointer (&self->model, g_object_unref);

  if (model != NULL)
    {
      self->model = g_object_ref (model);
      items_changed (
          self,
          0,
          0,
          g_list_model_get_n_items (G_LIST_MODEL (model)),
          G_LIST_MODEL (model));

      g_signal_connect_swapped (
          model,
          "items-changed",
          G_CALLBACK (items_changed),
          self);
      g_signal_connect_swapped (
          model,
          "notify::selected",
          G_CALLBACK (model_selected_changed),
          self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
}

static void
items_changed (BgeCarousel *self,
               guint        position,
               guint        removed,
               guint        added,
               GListModel  *model)
{
  for (guint i = 0; i < removed; i++)
    {
      GObject            *object = NULL;
      CarouselWidgetData *child  = NULL;

      object = g_ptr_array_index (self->mirror, position + i);
      child  = g_ptr_array_index (self->widgets, position + i);

      if (child->cancellable != NULL)
        g_cancellable_cancel (child->cancellable);
      g_signal_emit (self, signals[SIGNAL_REMOVE_WIDGET], 0, child->widget, object);
    }
  if (removed > 0)
    {
      g_ptr_array_remove_range (self->mirror, position, removed);
      g_ptr_array_remove_range (self->widgets, position, removed);
    }

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (GObject) object = NULL;
      GtkWidget          *child  = NULL;
      CarouselWidgetData *data   = NULL;

      object = g_list_model_get_item (model, position + i);

      g_signal_emit (self, signals[SIGNAL_CREATE_WIDGET], 0, object, &child);
      if (child == NULL)
        {
          g_critical ("Failed to populate child for carousel widget");
          child = gtk_fixed_new ();
        }

      if (position + i == 0)
        gtk_widget_set_parent (child, GTK_WIDGET (self));
      else
        {
          CarouselWidgetData *prev = NULL;

          prev = g_ptr_array_index (self->widgets, position + i - 1);
          gtk_widget_insert_after (child, GTK_WIDGET (self), prev->widget);
        }

      data         = carousel_widget_data_new ();
      data->widget = child;

      g_ptr_array_insert (self->mirror, position + i, g_object_ref (object));
      g_ptr_array_insert (self->widgets, position + i, data);
    }

  ensure_viewport (self, GTK_SINGLE_SELECTION (model), FALSE);
}

static void
model_selected_changed (BgeCarousel        *self,
                        GParamSpec         *pspec,
                        GtkSingleSelection *selection)
{
  guint idx = 0;

  idx = gtk_single_selection_get_selected (selection);
  if (idx != G_MAXUINT)
    move_to_idx (self, idx, ANIMATION_DAMPING_RATIO);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
move_to_idx (BgeCarousel *self,
             guint        idx,
             /* damping_ratio <= 0.0 means no animation */
             double damping_ratio)
{
  int width  = 0;
  int height = 0;
  int offset = 0;

  width  = gtk_widget_get_width (GTK_WIDGET (self));
  height = gtk_widget_get_height (GTK_WIDGET (self));
  if (width == 0 ||
      height == 0)
    {
      gtk_widget_queue_allocate (GTK_WIDGET (self));
      return;
    }

  offset = width / 2;
  if (self->scrolling)
    offset += self->hscroll_start - self->hscroll_current;
  if (self->dragging)
    offset += self->drag_x;

  for (guint i = 0; i <= idx; i++)
    {
      CarouselWidgetData *child       = NULL;
      int                 hminimum    = 0;
      int                 hnatural    = 0;
      int                 unused      = 0;
      int                 child_width = 0;

      child = g_ptr_array_index (self->widgets, i);

      gtk_widget_measure (
          child->widget,
          GTK_ORIENTATION_HORIZONTAL,
          height,
          &hminimum,
          &hnatural,
          &unused,
          &unused);
      if (gtk_widget_get_hexpand (child->widget))
        child_width = MAX (hminimum, width);
      else
        child_width = CLAMP (hnatural, hminimum, width);

      if (i == idx)
        offset -= child_width / 2;
      else
        offset -= child_width;
    }

  for (guint i = 0; i < self->widgets->len; i++)
    {
      CarouselWidgetData *child           = NULL;
      int                 hminimum        = 0;
      int                 hnatural        = 0;
      int                 unused          = 0;
      int                 rect_width      = 0;
      int                 child_width     = 0;
      int                 child_height    = 0;
      int                 child_x         = 0;
      int                 child_y         = 0;
      graphene_rect_t     target          = { 0 };
      gboolean            avoid_animation = FALSE;

      child = g_ptr_array_index (self->widgets, i);

      gtk_widget_measure (
          child->widget,
          GTK_ORIENTATION_HORIZONTAL,
          height,
          &hminimum,
          &hnatural,
          &unused,
          &unused);
      if (gtk_widget_get_hexpand (child->widget))
        rect_width = MAX (hminimum, width);
      else
        rect_width = CLAMP (hnatural, hminimum, width);

      if (child->raised || !self->allow_raise)
        {
          child_width  = rect_width;
          child_height = height;
          child_x      = offset;
          child_y      = 0;
        }
      else
        {
          child_height = round ((double) height * (1.0 - RAISE_FACTOR));

          gtk_widget_measure (
              child->widget,
              GTK_ORIENTATION_HORIZONTAL,
              child_height,
              &hminimum,
              &hnatural,
              &unused,
              &unused);
          child_width = CLAMP (hnatural, hminimum, width);

          child_x = offset + round ((double) (rect_width - child_width) * 0.5);
          child_y = round ((double) height * (0.5 * RAISE_FACTOR));
        }

      target          = GRAPHENE_RECT_INIT (child_x, child_y, child_width, child_height);
      avoid_animation = graphene_rect_equal (&target, &child->target);
      if (child->width != rect_width ||
          (damping_ratio < 0.0 && !avoid_animation))
        {
          if (child->cancellable != NULL)
            g_cancellable_cancel (child->cancellable);

          child->width  = rect_width;
          child->rect   = target;
          child->target = target;
        }
      else if (avoid_animation)
        child->target = target;
      else
        {
          char buf[64] = { 0 };

          g_clear_object (&child->cancellable);
          child->cancellable = g_cancellable_new ();

#define MASS      1.0
#define STIFFNESS 800.0

          /* pointer is to ensure a unique identifier so as not to overwrite any
             other child's key */
          g_snprintf (buf, sizeof (buf), "x%p", child);
          bge_animation_add_spring (
              self->animation, buf,
              child->rect.origin.x, target.origin.x,
              damping_ratio, MASS, STIFFNESS,
              (BgeAnimationCallback) animate,
              carousel_widget_data_ref (child),
              carousel_widget_data_unref,
              child->cancellable);

          g_snprintf (buf, sizeof (buf), "y%p", child);
          bge_animation_add_spring (
              self->animation, buf,
              child->rect.origin.y, target.origin.y,
              damping_ratio, MASS, STIFFNESS,
              (BgeAnimationCallback) animate,
              carousel_widget_data_ref (child),
              carousel_widget_data_unref,
              child->cancellable);

          g_snprintf (buf, sizeof (buf), "w%p", child);
          bge_animation_add_spring (
              self->animation, buf,
              child->rect.size.width, target.size.width,
              damping_ratio, MASS, STIFFNESS,
              (BgeAnimationCallback) animate,
              carousel_widget_data_ref (child),
              carousel_widget_data_unref,
              child->cancellable);

          g_snprintf (buf, sizeof (buf), "h%p", child);
          bge_animation_add_spring (
              self->animation, buf,
              child->rect.size.height, target.size.height,
              damping_ratio, MASS, STIFFNESS,
              (BgeAnimationCallback) animate,
              carousel_widget_data_ref (child),
              carousel_widget_data_unref,
              child->cancellable);

#undef STIFFNESS
#undef MASS

          child->target = target;
        }

      offset += rect_width;
    }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
animate (BgeCarousel        *self,
         const char         *key,
         double              value,
         CarouselWidgetData *data)
{
  switch (*key)
    {
    case 'x':
      data->rect.origin.x = value;
      break;
    case 'y':
      data->rect.origin.y = value;
      break;
    case 'w':
      data->rect.size.width = value;
      break;
    case 'h':
      data->rect.size.height = value;
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
ensure_viewport (BgeCarousel        *self,
                 GtkSingleSelection *model,
                 gboolean            animate)
{
  guint n_items = 0;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (model));
  if (n_items > 0)
    {
      guint selected = 0;

      selected = gtk_single_selection_get_selected (model);
      if (selected == G_MAXUINT)
        {
          gtk_single_selection_set_selected (model, 0);
          move_to_idx (self, 0, animate ? ANIMATION_DAMPING_RATIO : -1.0);
        }
      else
        move_to_idx (self, selected, animate ? ANIMATION_DAMPING_RATIO : -1.0);
    }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
motion_enter (BgeCarousel              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller)
{
  self->motion_x = x;
  self->motion_y = y;
  update_motion (self, x, y);
}

static void
motion_event (BgeCarousel              *self,
              gdouble                   x,
              gdouble                   y,
              GtkEventControllerMotion *controller)
{
  self->motion_x = x;
  self->motion_y = y;
  update_motion (self, x, y);
}

static void
motion_leave (BgeCarousel              *self,
              GtkEventControllerMotion *controller)
{
  if (self->dragging)
    return;
  self->motion_x = -1.0;
  self->motion_y = -1.0;
  update_motion (self, -1.0, -1.0);
}

static void
update_motion (BgeCarousel *self,
               gdouble      x,
               gdouble      y)
{
  graphene_point_t point  = { 0 };
  gboolean         ensure = FALSE;

  point = GRAPHENE_POINT_INIT (x, y);

  if (self->scrolling)
    return;

  for (guint i = 0; i < self->widgets->len; i++)
    {
      CarouselWidgetData *child     = NULL;
      gboolean            contained = FALSE;

      child     = g_ptr_array_index (self->widgets, i);
      contained = graphene_rect_contains_point (&child->target, &point);
      if (!!contained != !!child->raised)
        {
          child->raised = contained;
          ensure        = TRUE;
        }
    }

  if (self->dragging)
    return;
  if (ensure)
    ensure_viewport (self, self->model, TRUE);
}

static void
scroll_begin (BgeCarousel              *self,
              GtkEventControllerScroll *controller)
{
  self->scrolling       = TRUE;
  self->hscroll_start   = self->motion_x;
  self->hscroll_current = self->motion_x;
}

static void
scroll_end (BgeCarousel              *self,
            GtkEventControllerScroll *controller)
{
  self->scrolling = FALSE;
  finish_horizontal_gesture (
      self,
      self->hscroll_start - self->hscroll_current,
      0);
  self->hscroll_start   = -1;
  self->hscroll_current = -1;

  update_motion (self, self->motion_x, self->motion_y);
}

static gboolean
scroll (BgeCarousel              *self,
        gdouble                   dx,
        gdouble                   dy,
        GtkEventControllerScroll *controller)
{
  guint          n_items = 0;
  GdkDevice     *device  = NULL;
  GdkInputSource source  = 0;

  if (self->model == NULL)
    {
      self->scrolling = FALSE;
      return FALSE;
    }

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->model));
  if (n_items == 0)
    {
      self->scrolling = FALSE;
      return FALSE;
    }

  device = gtk_event_controller_get_current_event_device (
      GTK_EVENT_CONTROLLER (controller));
  source = gdk_device_get_source (device);

  switch (source)
    {
    case GDK_SOURCE_TOUCHPAD:
    case GDK_SOURCE_TRACKPOINT:
      {
        if (self->widgets->len > 0 &&
            !self->allow_overshoot)
          {
            guint selection = 0;

            selection = gtk_single_selection_get_selected (self->model);
            if ((selection == 0 && self->hscroll_current + dx < 0) ||
                (selection == self->widgets->len - 1 && self->hscroll_current + dx > 0))
              {
                self->scrolling = FALSE;
                return FALSE;
              }
          }

        self->hscroll_current += dx;
        ensure_viewport (self, self->model, FALSE);
      }
      break;
    case GDK_SOURCE_MOUSE:
    case GDK_SOURCE_PEN:
    case GDK_SOURCE_KEYBOARD:
    case GDK_SOURCE_TOUCHSCREEN:
    case GDK_SOURCE_TABLET_PAD:
    default:
      {
        guint selected     = 0;
        guint new_selected = 0;

        if (!self->allow_scroll_wheel)
          break;

        selected = gtk_single_selection_get_selected (self->model);
        if (dx > 0)
          new_selected = MIN (selected + 1, n_items - 1);
        else
          {
            if (selected == 0)
              new_selected = 0;
            else
              new_selected = selected - 1;
          }
        gtk_single_selection_set_selected (self->model, new_selected);
      }
      break;
    }

  return TRUE;
}

static void
drag_begin (BgeCarousel    *self,
            gdouble         start_x,
            gdouble         start_y,
            GtkGestureDrag *gesture)
{
  if (!self->allow_mouse_drag)
    return;

  self->dragging = TRUE;
  if (self->model == NULL)
    return;

  ensure_viewport (self, self->model, TRUE);
}

static void
drag_end (BgeCarousel    *self,
          gdouble         offset_x,
          gdouble         offset_y,
          GtkGestureDrag *gesture)
{
  if (!self->dragging)
    /* This situation will happen if the `allow-mouse-drag` prop is set to FALSE
       while a drag is taking place */
    return;

  self->dragging = FALSE;
  finish_horizontal_gesture (self, self->drag_x, self->drag_y);
  self->drag_x = 0.0;
  self->drag_y = 0.0;

  if (offset_x < -3 ||
      offset_x > 3 ||
      offset_y < -3 ||
      offset_y > 3)
    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
drag_update (BgeCarousel    *self,
             gdouble         offset_x,
             gdouble         offset_y,
             GtkGestureDrag *gesture)
{
  if (!self->dragging)
    return;

  self->drag_x = offset_x;
  self->drag_y = offset_y;

  if (self->model == NULL)
    return;

  if (self->widgets->len > 0 &&
      !self->allow_overshoot)
    {
      guint selected = 0;

      selected = gtk_single_selection_get_selected (self->model);
      if (selected == 0)
        self->drag_x = MIN (self->drag_x, 0.0);
      if (selected == self->widgets->len - 1)
        self->drag_x = MAX (self->drag_x, 0.0);
    }

  ensure_viewport (self, self->model, FALSE);
}

static void
finish_horizontal_gesture (BgeCarousel *self,
                           int          offset_x,
                           int          offset_y)
{
  guint  selected     = 0;
  double width        = 0.0;
  guint  new_selected = G_MAXUINT;
  int    min_distance = G_MAXINT;

  if (self->model == NULL ||
      self->widgets->len == 0)
    return;

  if (offset_x == 0)
    return;

  selected = gtk_single_selection_get_selected (self->model);

  width = gtk_widget_get_width (GTK_WIDGET (self));
  for (guint i = 0; i < self->widgets->len; i++)
    {
      CarouselWidgetData *child                = NULL;
      int                 left_distance        = 0;
      int                 right_distance       = 0;
      int                 distance_from_center = 0;

      child = g_ptr_array_index (self->widgets, i);

      if (child->rect.origin.x > width / 2.0)
        left_distance = child->rect.origin.x - width / 2.0;
      else
        left_distance = width / 2.0 - child->rect.origin.x;

      if ((child->rect.origin.x + child->rect.size.width) > width / 2.0)
        right_distance = (child->rect.origin.x + child->rect.size.width) - width / 2.0;
      else
        right_distance = width / 2.0 - (child->rect.origin.x + child->rect.size.width);

      distance_from_center = MIN (left_distance, right_distance);
      if (distance_from_center < min_distance)
        {
          new_selected = i;
          min_distance = distance_from_center;
        }
    }

  if (new_selected == selected)
    {
      /* Ensure dragging is not too stiff; meaning if we drag the content at
         least 15 pixels in either direction, it will automatically snap to the
         next widget */
      if (offset_x > 15 && selected > 0)
        new_selected--;
      else if (offset_x < -15 && selected < self->widgets->len - 1)
        new_selected++;
    }

  if (new_selected == G_MAXUINT ||
      new_selected == selected)
    ensure_viewport (self, self->model, TRUE);
  else
    gtk_single_selection_set_selected (self->model, new_selected);
}

/* End of bge-carousel.c */
