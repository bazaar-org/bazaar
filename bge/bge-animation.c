/* bge-animation.c
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

/* This animation implementation was originally from my other project,
   libpastry: https://github.com/kolunmi/libpastry */

/**
 * BgeAnimation:
 *
 * Manages animations for a widget. Individual value animations are tracked in a
 * hash map with string keys, allowing them to be easily restarted or replaced.
 */

#define G_LOG_DOMAIN "BGE::ANIMATION"

#define DELTA   0.001
#define EPSILON 0.00001

#include "bge.h"

#include "bge-animation-private.h"

enum
{
  PROP_0,

  PROP_WIDGET,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

struct _BgeAnimation
{
  GObject parent_instance;

  GtkWidget *widget;
  GWeakRef   wr;

  guint       tag;
  GHashTable *data;
  GPtrArray  *anonymous;
};
G_DEFINE_FINAL_TYPE (BgeAnimation, bge_animation, G_TYPE_OBJECT)

typedef struct
{
  double               from;
  double               to;
  double               damping_ratio;
  double               mass;
  double               stiffness;
  double               damping;
  gboolean             clamp;
  BgeAnimationCallback cb;
  gpointer             user_data;
  GDestroyNotify       destroy_data;
  double               est_duration;
  GTimer              *timer;
  double               velocity;
  GCancellable        *cancellable;
} SpringData;

static gboolean
tick_cb (GtkWidget     *widget,
         GdkFrameClock *frame_clock,
         GWeakRef      *wr);

static void
ensure_tick (BgeAnimation *self,
             GtkWidget    *widget);

static void
destroy_spring_data (gpointer ptr);

static void
destroy_wr (gpointer ptr);

static void
dispose (GObject *object)
{
  BgeAnimation *self           = BGE_ANIMATION (object);
  g_autoptr (GtkWidget) widget = NULL;

  widget = g_weak_ref_get (&self->wr);
  if (widget != NULL)
    {
      gtk_widget_remove_tick_callback (widget, self->tag);
      self->tag = 0;
    }
  g_clear_object (&widget);
  g_weak_ref_clear (&self->wr);

  g_clear_object (&self->widget);
  g_clear_pointer (&self->data, g_hash_table_unref);
  g_clear_pointer (&self->anonymous, g_ptr_array_unref);

  G_OBJECT_CLASS (bge_animation_parent_class)->dispose (object);
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
  BgeAnimation *self = BGE_ANIMATION (object);

  switch (prop_id)
    {
    case PROP_WIDGET:
      g_value_take_object (value, bge_animation_dup_widget (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  BgeAnimation *self = BGE_ANIMATION (object);

  switch (prop_id)
    {
    case PROP_WIDGET:
      g_clear_object (&self->widget);
      self->widget = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
constructed (GObject *object)
{
  BgeAnimation *self = BGE_ANIMATION (object);

  g_weak_ref_init (&self->wr, self->widget);
  g_clear_object (&self->widget);
}

static void
bge_animation_class_init (BgeAnimationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed  = constructed;
  object_class->set_property = set_property;
  object_class->get_property = get_property;
  object_class->dispose      = dispose;

  /**
   * BgeAnimation:widget:
   *
   * The widget on which this animation is attached.
   */
  props[PROP_WIDGET] =
      g_param_spec_object (
          "widget",
          NULL, NULL,
          GTK_TYPE_WIDGET,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bge_animation_init (BgeAnimation *self)
{
  g_weak_ref_init (&self->wr, NULL);
  self->data = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, destroy_spring_data);
  self->anonymous = g_ptr_array_new_with_free_func (
      destroy_spring_data);
}

/**
 * bge_animation_new:
 * @widget: The widget onto which to attach the tick callback
 *
 * Creates a new `BgeAnimation` object.
 *
 * Returns: The newly created `BgeAnimation` object.
 */
BgeAnimation *
bge_animation_new (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);
  return g_object_new (
      BGE_TYPE_ANIMATION,
      "widget", widget,
      NULL);
}

/**
 * bge_animation_dup_widget:
 * @self: a `BgeAnimation`
 *
 * Gets the widget on which @self is attached.
 *
 * Returns: (nullable) (transfer full): the widget for @self
 */
GtkWidget *
bge_animation_dup_widget (BgeAnimation *self)
{
  g_return_val_if_fail (BGE_IS_ANIMATION (self), NULL);
  return g_weak_ref_get (&self->wr);
}

/**
 * bge_animation_add_spring:
 * @self: a `BgeAnimation`
 * @key: (nullable): a string ID to replace, or NULL for anonymous
 * @from: the start value
 * @to: the end value
 * @damping_ratio: the damping ratio
 * @mass: the mass
 * @stiffness: the stiffness
 * @cb: a tick callback
 * @user_data: (nullable): the user data pointer to be passed to @cb
 * @destroy_data: (nullable): the destruction function for @user_data
 * @cancellable: (nullable): a cancellable to cancel the operation
 *
 * Adds a one shot spring animation to @self. If @key is already running in
 * @self, then the old animation is replaced, maintaining the current velocity.
 *
 */
void
bge_animation_add_spring (BgeAnimation        *self,
                          const char          *key,
                          double               from,
                          double               to,
                          double               damping_ratio,
                          double               mass,
                          double               stiffness,
                          BgeAnimationCallback cb,
                          gpointer             user_data,
                          GDestroyNotify       destroy_data,
                          GCancellable        *cancellable)
{
  g_autoptr (GtkWidget) widget = NULL;

  g_return_if_fail (BGE_IS_ANIMATION (self));
  g_return_if_fail (cb != NULL);

  widget = g_weak_ref_get (&self->wr);
  if (widget != NULL)
    {
      if (bge_should_animate (widget))
        {
          SpringData *data = NULL;

          if (key != NULL)
            /* reuse old data if possible */
            data = g_hash_table_lookup (self->data, key);

          if (data != NULL)
            {
              if (data->user_data != NULL &&
                  data->destroy_data != NULL)
                /* we are going to overwrite this */
                data->destroy_data (data->user_data);

              g_clear_pointer (&data->timer, g_timer_destroy);
              g_clear_object (&data->cancellable);

              /* old velocity is retained */
            }
          else
            {
              data = g_new0 (typeof (*data), 1);
              if (key != NULL)
                g_hash_table_replace (self->data, g_strdup (key), data);
              else
                g_ptr_array_add (self->anonymous, data);
            }

          data->from          = from;
          data->to            = to;
          data->damping_ratio = damping_ratio;
          data->mass          = mass;
          data->stiffness     = stiffness;
          data->cb            = cb;
          data->user_data     = user_data;
          data->destroy_data  = destroy_data;

          data->damping = damping_ratio *
                          (/* critical damping */
                           2 * sqrt (mass * stiffness));

          /* We'll fill this in on the first iteration */
          data->timer = NULL;

          if (cancellable != NULL)
            data->cancellable = g_object_ref (cancellable);

          data->est_duration = spring_calculate_duration (
              data->damping,
              data->mass,
              data->stiffness,
              data->from,
              data->to,
              data->clamp);

          cb (widget, key, from, user_data);
          ensure_tick (self, widget);
        }
      else
        /* If we shouldn't animate, just invoke the callback at the final
           value */
        {
          cb (widget, key, to, user_data);
          if (user_data != NULL &&
              destroy_data != NULL)
            destroy_data (user_data);
        }
    }
  else
    {
      if (user_data != NULL &&
          destroy_data != NULL)
        destroy_data (user_data);
    }
}

/**
 * bge_animation_has_key:
 * @self: a `BgeAnimation`
 * @key: a string ID
 *
 * Determines whether @key exists and represents an active animation on @self.
 *
 * Returns: a boolean representing whether the string ID exists
 */
gboolean
bge_animation_has_key (BgeAnimation *self,
                       const char   *key)
{
  g_return_val_if_fail (BGE_IS_ANIMATION (self), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  return g_hash_table_contains (self->data, key);
}

/**
 * bge_animation_cancel:
 * @self: a `BgeAnimation`
 * @key: a string ID to remove
 *
 * If @key exists on @self, cancel the associated animation.
 */
void
bge_animation_cancel (BgeAnimation *self,
                      const char   *key)
{
  SpringData *data             = NULL;
  g_autoptr (GtkWidget) widget = NULL;

  g_return_if_fail (BGE_IS_ANIMATION (self));
  g_return_if_fail (key != NULL);

  data = g_hash_table_lookup (self->data, key);
  if (data == NULL)
    return;

  widget = g_weak_ref_get (&self->wr);
  if (widget != NULL)
    data->cb (widget, key, data->to, data->user_data);

  g_hash_table_remove (self->data, key);
}

/**
 * bge_animation_cancel_all:
 * @self: a `BgeAnimation`
 *
 * Cancel all animations on @self.
 */
void
bge_animation_cancel_all (BgeAnimation *self)
{
  GHashTableIter iter          = { 0 };
  g_autoptr (GtkWidget) widget = NULL;

  g_return_if_fail (BGE_IS_ANIMATION (self));

  g_hash_table_iter_init (&iter, self->data);
  widget = g_weak_ref_get (&self->wr);

  for (;;)
    {
      char       *key  = NULL;
      SpringData *data = NULL;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &key,
              (gpointer *) &data))
        break;

      if (widget != NULL)
        data->cb (widget, key, data->to, data->user_data);

      g_hash_table_iter_remove (&iter);
    }
}

static gboolean
tick_cb (GtkWidget     *widget,
         GdkFrameClock *frame_clock,
         GWeakRef      *wr)
{
  g_autoptr (BgeAnimation) self = NULL;
  gboolean       cancel         = FALSE;
  GHashTableIter iter           = { 0 };

  self = g_weak_ref_get (wr);
  if (self == NULL)
    return G_SOURCE_REMOVE;

  cancel = !bge_should_animate (widget);

#define UPDATE(_data, _out_value, _out_finished)                   \
  G_STMT_START                                                     \
  {                                                                \
    if (cancel ||                                                  \
        ((_data)->cancellable != NULL &&                           \
         g_cancellable_is_cancelled ((_data)->cancellable)))       \
      (_out_finished) = TRUE;                                      \
    else                                                           \
      {                                                            \
        double elapsed = 0.0;                                      \
                                                                   \
        if ((_data)->timer == NULL)                                \
          {                                                        \
            (_data)->timer = g_timer_new ();                       \
            (_out_value)   = (_data)->from;                        \
          }                                                        \
        else                                                       \
          {                                                        \
            elapsed      = g_timer_elapsed ((_data)->timer, NULL); \
            (_out_value) = spring_oscillate (                      \
                data->damping,                                     \
                data->mass,                                        \
                data->stiffness,                                   \
                data->from,                                        \
                data->to,                                          \
                elapsed,                                           \
                &(_data)->velocity);                               \
          }                                                        \
                                                                   \
        (_out_finished) = elapsed >= (_data)->est_duration;        \
      }                                                            \
    if ((_out_finished))                                           \
      (_out_value) = (_data)->to;                                  \
  }                                                                \
  G_STMT_END

  /* Named anims */
  g_hash_table_iter_init (&iter, self->data);
  for (;;)
    {
      char       *key      = NULL;
      SpringData *data     = NULL;
      double      value    = 0.0;
      gboolean    finished = FALSE;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &key,
              (gpointer *) &data))
        break;

      UPDATE (data, value, finished);
      data->cb (widget, key, value, data->user_data);

      if (finished)
        g_hash_table_iter_remove (&iter);
    }

  /* Anonymous anims */
  for (guint i = 0; i < self->anonymous->len;)
    {
      SpringData *data     = NULL;
      double      value    = 0.0;
      gboolean    finished = FALSE;

      data = g_ptr_array_index (self->anonymous, i);

      UPDATE (data, value, finished);
      data->cb (widget, NULL, value, data->user_data);

      if (finished)
        g_ptr_array_remove_index (self->anonymous, i);
      else
        i++;
    }

#undef UPDATE

  if (g_hash_table_size (self->data) == 0 &&
      self->anonymous->len == 0)
    {
      gtk_widget_remove_tick_callback (widget, self->tag);
      self->tag = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

/* COPIED FROM LIBADWAITA */

/* Based on RBBSpringAnimation from RBBAnimation, MIT license.
 * https://github.com/robb/RBBAnimation/blob/master/RBBAnimation/RBBSpringAnimation.m
 *
 * @offset: Starting value of the spring simulation. Use -1 for regular animations,
 * as the formulas are tailored to rest at 0 and the resulting evolution between
 * -1 and 0 will be lerped to the desired range afterwards. Otherwise use 0 for in-place
 * animations which already start at equilibrium
 */
double
spring_oscillate (double  damping,
                  double  mass,
                  double  stiffness,
                  double  from,
                  double  to,
                  double  time,
                  double *velocity)
{
  double b        = damping;
  double m        = mass;
  double k        = stiffness;
  double v0       = 0.0;
  double beta     = 0.0;
  double omega0   = 0.0;
  double x0       = 0.0;
  double envelope = 0.0;

  beta     = b / (2 * m);
  omega0   = sqrt (k / m);
  x0       = from - to;
  envelope = exp (-beta * time);

  /*
   * Solutions of the form C1*e^(lambda1*x) + C2*e^(lambda2*x)
   * for the differential equation m*ẍ+b*ẋ+kx = 0
   */

  /* Critically damped */
  /* DBL_EPSILON is too small for this specific comparison, so we use
   * FLT_EPSILON even though it's doubles */
  if (G_APPROX_VALUE (beta, omega0, FLT_EPSILON))
    {
      if (velocity != NULL)
        *velocity = envelope *
                    (-beta * time * v0 -
                     beta * beta * time * x0 +
                     v0);

      return to + envelope *
                      (x0 + (beta * x0 + v0) * time);
    }

  /* Underdamped */
  if (beta < omega0)
    {
      double omega1 = 0.0;

      omega1 = sqrt ((omega0 * omega0) - (beta * beta));

      if (velocity != NULL)
        *velocity = envelope *
                    (v0 * cos (omega1 * time) -
                     (x0 * omega1 +
                      (beta * beta * x0 + beta * v0) /
                          (omega1)) *
                         sin (omega1 * time));

      return to + envelope *
                      (x0 * cos (omega1 * time) +
                       ((beta * x0 + v0) /
                        omega1) *
                           sin (omega1 * time));
    }

  /* Overdamped */
  if (beta > omega0)
    {
      double omega2 = 0.0;

      omega2 = sqrt ((beta * beta) - (omega0 * omega0));

      if (velocity != NULL)
        *velocity = envelope *
                    (v0 * coshl (omega2 * time) +
                     (omega2 * x0 -
                      (beta * beta * x0 + beta * v0) /
                          omega2) *
                         sinhl (omega2 * time));

      return to + envelope *
                      (x0 * coshl (omega2 * time) +
                       ((beta * x0 + v0) / omega2) *
                           sinhl (omega2 * time));
    }

  g_assert_not_reached ();
}

double
spring_get_first_zero (double damping,
                       double mass,
                       double stiffness,
                       double from,
                       double to)
{
  /* The first frame is not that important and we avoid finding the trivial 0
   * for in-place animations. */
  for (int i = 0; i < 20000; i++)
    {
      double y = 0.0;

      y = spring_oscillate (
          damping,
          mass,
          stiffness,
          from,
          to,
          (double) i / 1000.0,
          NULL);
      if (!((to - from > DBL_EPSILON && to - y > EPSILON) ||
            (from - to > DBL_EPSILON && y - to > EPSILON)))
        return y;
    }
  return 0.0;
}

double
spring_calculate_duration (double   damping,
                           double   mass,
                           double   stiffness,
                           double   from,
                           double   to,
                           gboolean clamp)
{
  double beta   = 0.0;
  double omega0 = 0.0;
  double x0     = 0.0;
  double y0     = 0.0;
  double x1     = 0.0;
  double y1     = 0.0;
  double m      = 0.0;

  beta = damping / (2 * mass);

  if (G_APPROX_VALUE (beta, 0, DBL_EPSILON) ||
      beta < 0)
    return G_MAXDOUBLE;

  if (clamp)
    {
      if (G_APPROX_VALUE (to, from, DBL_EPSILON))
        return 0;
      return spring_get_first_zero (damping,
                                    mass,
                                    stiffness,
                                    from,
                                    to);
    }

  omega0 = sqrt (stiffness / mass);

  /*
   * As first ansatz for the overdamped solution,
   * and general estimation for the oscillating ones
   * we take the value of the envelope when it's < epsilon
   */
  x0 = -log (EPSILON) / beta;

  /* DBL_EPSILON is too small for this specific comparison, so we use
   * FLT_EPSILON even though it's doubles */
  if (G_APPROX_VALUE (beta, omega0, FLT_EPSILON) ||
      beta < omega0)
    return x0;

  /*
   * Since the overdamped solution decays way slower than the envelope
   * we need to use the value of the oscillation itself.
   * Newton's root finding method is a good candidate in this particular case:
   * https://en.wikipedia.org/wiki/Newton%27s_method
   */
  y0 = spring_oscillate (damping,
                         mass,
                         stiffness,
                         from,
                         to,
                         x0,
                         NULL);
  m  = (spring_oscillate (
            damping,
            mass,
            stiffness,
            from,
            to,
            (x0 + DELTA),
            NULL) -
        y0) /
       DELTA;

  x1 = (to - y0 + m * x0) / m;
  y1 = spring_oscillate (
      damping,
      mass,
      stiffness,
      from,
      to,
      x1,
      NULL);

  for (int i = 0;
       ABS (to - y1) > EPSILON && i < 1000;
       i++)
    {
      x0 = x1;
      y0 = y1;

      m = (spring_oscillate (
               damping,
               mass,
               stiffness,
               from,
               to,
               x0 + DELTA,
               NULL) -
           y0) /
          DELTA;

      x1 = (to - y0 + m * x0) / m;
      y1 = spring_oscillate (
          damping,
          mass,
          stiffness,
          from,
          to,
          x1,
          NULL);
    }

  if (ABS (to - y1) <= EPSILON)
    return x1;
  else
    return 0.0;
}

/* ///COPIED FROM LIBADWAITA */

static void
ensure_tick (BgeAnimation *self,
             GtkWidget    *widget)
{
  GWeakRef *wr = NULL;

  if (self->tag > 0)
    return;

  wr = g_new0 (typeof (*wr), 1);
  g_weak_ref_init (wr, self);

  self->tag = gtk_widget_add_tick_callback (
      widget,
      (GtkTickCallback) tick_cb,
      wr, destroy_wr);
}

static void
destroy_spring_data (gpointer ptr)
{
  SpringData *data = ptr;

  if (data->destroy_data != NULL &&
      data->user_data != NULL)
    data->destroy_data (data->user_data);
  g_clear_pointer (&data->timer, g_timer_destroy);
  g_clear_object (&data->cancellable);
  g_free (ptr);
}

static void
destroy_wr (gpointer ptr)
{
  GWeakRef *wr = ptr;

  g_weak_ref_clear (wr);
  g_free (ptr);
}

gboolean
bge_should_animate (GtkWidget *widget)
{
  GtkSettings *settings          = NULL;
  gboolean     enable_animations = FALSE;

  if (!gtk_widget_get_mapped (widget))
    return FALSE;

  settings = gtk_widget_get_settings (widget);
  g_object_get (
      settings,
      "gtk-enable-animations", &enable_animations,
      NULL);

  return enable_animations;
}
