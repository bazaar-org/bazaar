/* bz-screenshot-page.c
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

#include "bz-screenshot-page.h"
#include "bz-screenshot.h"
#include "bz-zoom.h"
#include <glib/gi18n.h>

#define MOUSE_BACK_BUTTON 8

#define SPRING_DAMPING_RATIO 1.05
#define SPRING_MASS          1.0
#define SPRING_STIFFNESS     1000.0
#define FADE_DURATION_MS     350

struct _BzScreenshotPage
{
  AdwBin parent_instance;

  AdwCarousel     *carousel;
  AdwToastOverlay *toast_overlay;

  GListModel *screenshots;
  GListModel *captions;
  guint       current_index;
  guint       initial_index;

  gboolean is_zoomed;
  gboolean reduce_motion;

  GtkWidget      *source_widget;
  GdkTexture     *source_texture;
  graphene_rect_t source_bounds_at_map;
  AdwAnimation   *animation;
  double          animation_progress;
  gboolean        closing;
};

G_DEFINE_FINAL_TYPE (BzScreenshotPage, bz_screenshot_page, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_SCREENSHOTS,
  PROP_CURRENT_INDEX,
  PROP_CURRENT_CAPTION,
  PROP_IS_ZOOMED,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static GdkTexture *render_widget_to_texture (GtkWidget *widget);

static void on_animation_value (AdwAnimation     *animation,
                                GParamSpec       *pspec,
                                BzScreenshotPage *self);

static void on_fade_animation_value (AdwAnimation     *animation,
                                     GParamSpec       *pspec,
                                     BzScreenshotPage *self);

static void on_close_animation_done (AdwAnimation     *animation,
                                     BzScreenshotPage *self);

static void on_zoom_level_changed (BzZoom           *zoom,
                                   GParamSpec       *pspec,
                                   BzScreenshotPage *self);

static void update_is_zoomed (BzScreenshotPage *self);

static BzZoom *get_current_zoom (BzScreenshotPage   *self,
                                 GtkScrolledWindow **scrolled_window);

static void populate_carousel (BzScreenshotPage *self);

static gboolean on_key_pressed (GtkEventControllerKey *controller,
                                guint                  keyval,
                                guint                  keycode,
                                GdkModifierType        state,
                                BzScreenshotPage      *self);
static void     on_button_pressed (GtkGestureClick  *gesture,
                                   int               n_press,
                                   double            x,
                                   double            y,
                                   BzScreenshotPage *self);

static void on_swipe (BzScreenshotPage *self,
                      gdouble           vel_x,
                      gdouble           vel_y);

static gboolean on_scroll (BzScreenshotPage         *self,
                           gdouble                   dx,
                           gdouble                   dy,
                           GtkEventControllerScroll *controller);

static void
bz_screenshot_page_map (GtkWidget *widget)
{
  BzScreenshotPage   *self           = BZ_SCREENSHOT_PAGE (widget);
  AdwAnimationTarget *target         = NULL;
  AdwSpringParams    *params         = NULL;
  GtkSettings        *settings       = NULL;
  GtkReducedMotion    reduced_motion = GTK_REDUCED_MOTION_NO_PREFERENCE;

  GTK_WIDGET_CLASS (bz_screenshot_page_parent_class)->map (widget);

  settings = gtk_widget_get_settings (widget);
  g_object_get (settings, "gtk-interface-reduced-motion", &reduced_motion, NULL);

  self->reduce_motion = reduced_motion != GTK_REDUCED_MOTION_NO_PREFERENCE;
  self->closing       = FALSE;

  if (self->reduce_motion)
    {
      self->animation_progress = 1.0;
      g_clear_object (&self->source_texture);
      gtk_widget_set_opacity (widget, 0.0);
      target = adw_callback_animation_target_new (
          (AdwAnimationTargetFunc) gtk_widget_queue_draw, self, NULL);
      self->animation = adw_timed_animation_new (widget, 0.0, 1.0, FADE_DURATION_MS, target);
      g_signal_connect (self->animation, "notify::value",
                        G_CALLBACK (on_fade_animation_value), self);
      adw_animation_play (self->animation);
      return;
    }

  if (self->source_widget != NULL)
    {
      self->source_texture = render_widget_to_texture (self->source_widget);

      if (!gtk_widget_compute_bounds (self->source_widget, widget, &self->source_bounds_at_map))
        graphene_rect_init (&self->source_bounds_at_map, 0, 0,
                            gtk_widget_get_width (widget), gtk_widget_get_height (widget));

      gtk_widget_set_opacity (self->source_widget, 0.0);
    }

  self->animation_progress = 0.0;

  target = adw_callback_animation_target_new ((AdwAnimationTargetFunc) gtk_widget_queue_draw, self, NULL);

  params          = adw_spring_params_new (SPRING_DAMPING_RATIO, SPRING_MASS, SPRING_STIFFNESS);
  self->animation = adw_spring_animation_new (widget, 0.0, 1.0, params, target);
  adw_spring_animation_set_clamp (ADW_SPRING_ANIMATION (self->animation), TRUE);
  adw_spring_animation_set_epsilon (ADW_SPRING_ANIMATION (self->animation), 0.0001);

  adw_spring_animation_set_initial_velocity (ADW_SPRING_ANIMATION (self->animation), -1.2);
  g_signal_connect (self->animation, "notify::value", G_CALLBACK (on_animation_value), self);
  adw_animation_play (self->animation);
}

static void
bz_screenshot_page_unmap (GtkWidget *widget)
{
  BzScreenshotPage *self = BZ_SCREENSHOT_PAGE (widget);

  if (self->source_widget != NULL && !self->closing)
    gtk_widget_set_opacity (self->source_widget, 1.0);

  GTK_WIDGET_CLASS (bz_screenshot_page_parent_class)->unmap (widget);
}

static void
bz_screenshot_page_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
  BzScreenshotPage *self     = BZ_SCREENSHOT_PAGE (widget);
  double            progress = 0.0;
  int               width    = 0;
  int               height   = 0;
  float             rev      = 0.0f;
  float             x        = 0.0f;
  float             y        = 0.0f;
  float             w        = 0.0f;
  float             h        = 0.0f;
  graphene_rect_t   lerped;
  graphene_rect_t   source_bounds;

  progress = self->animation_progress;
  width    = gtk_widget_get_width (widget);
  height   = gtk_widget_get_height (widget);

  if (self->source_texture == NULL || progress >= 1.0)
    {
      GTK_WIDGET_CLASS (bz_screenshot_page_parent_class)->snapshot (widget, snapshot);
      return;
    }

  if (self->source_widget != NULL &&
      !gtk_widget_compute_bounds (self->source_widget, widget, &source_bounds))
    source_bounds = self->source_bounds_at_map;
  else if (self->source_widget == NULL)
    source_bounds = self->source_bounds_at_map;

  rev = (float) (1.0 - progress);

  x = source_bounds.origin.x * rev + 0.0f * (float) progress;
  y = source_bounds.origin.y * rev + 0.0f * (float) progress;
  w = source_bounds.size.width * rev + (float) width * (float) progress;
  h = source_bounds.size.height * rev + (float) height * (float) progress;

  graphene_rect_init (&lerped, x, y, w, h);

  gtk_snapshot_push_clip (snapshot, &lerped);
  gtk_snapshot_push_cross_fade (snapshot, progress);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, y));
  gtk_snapshot_scale (snapshot,
                      w / source_bounds.size.width,
                      h / source_bounds.size.height);
  gdk_paintable_snapshot (GDK_PAINTABLE (self->source_texture), snapshot,
                          source_bounds.size.width,
                          source_bounds.size.height);
  gtk_snapshot_restore (snapshot);

  gtk_snapshot_pop (snapshot);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, y));
  gtk_snapshot_scale (snapshot,
                      w / (float) width,
                      h / (float) height);
  GTK_WIDGET_CLASS (bz_screenshot_page_parent_class)->snapshot (widget, snapshot);
  gtk_snapshot_restore (snapshot);

  gtk_snapshot_pop (snapshot);
  gtk_snapshot_pop (snapshot);
}

static void
back_clicked (BzScreenshotPage *self)
{
  AdwAnimationTarget *target = NULL;
  AdwSpringParams    *params = NULL;
  GtkWidget          *parent = NULL;

  if (self->closing)
    return;

  if (self->source_widget != NULL)
    {
      self->closing = TRUE;

      gtk_widget_set_can_target (GTK_WIDGET (self), FALSE);

      if (self->animation != NULL)
        {
          g_signal_handlers_disconnect_by_func (self->animation, on_animation_value, self);
          g_signal_handlers_disconnect_by_func (self->animation, on_fade_animation_value, self);
          g_clear_object (&self->animation);
        }

      target = adw_callback_animation_target_new (
          (AdwAnimationTargetFunc) gtk_widget_queue_draw, self, NULL);

      if (self->reduce_motion)
        {
          self->animation = adw_timed_animation_new (
              GTK_WIDGET (self), gtk_widget_get_opacity (GTK_WIDGET (self)), 0.0,
              FADE_DURATION_MS, target);
          g_signal_connect (self->animation, "notify::value",
                            G_CALLBACK (on_fade_animation_value), self);
        }
      else
        {
          params          = adw_spring_params_new (SPRING_DAMPING_RATIO, SPRING_MASS, SPRING_STIFFNESS);
          self->animation = adw_spring_animation_new (
              GTK_WIDGET (self), self->animation_progress, 0.0, params, target);
          adw_spring_animation_set_clamp (ADW_SPRING_ANIMATION (self->animation), TRUE);
          adw_spring_animation_set_epsilon (ADW_SPRING_ANIMATION (self->animation), 0.0001);
          adw_spring_animation_set_initial_velocity (ADW_SPRING_ANIMATION (self->animation), -1.2);
          g_signal_connect (self->animation, "notify::value", G_CALLBACK (on_animation_value), self);
        }

      g_signal_connect (self->animation, "done", G_CALLBACK (on_close_animation_done), self);

      adw_animation_play (self->animation);
    }
  else
    {
      if (self->source_widget != NULL)
        gtk_widget_set_opacity (self->source_widget, 1.0);

      parent = gtk_widget_get_parent (GTK_WIDGET (self));
      if (parent != NULL)
        gtk_overlay_remove_overlay (GTK_OVERLAY (parent), GTK_WIDGET (self));
    }
}

static void
zoom_in_clicked (BzScreenshotPage *self)
{
  BzZoom *zoom = NULL;

  zoom = get_current_zoom (self, NULL);
  if (zoom != NULL)
    bz_zoom_zoom_in (zoom);
}

static void
zoom_out_clicked (BzScreenshotPage *self)
{
  BzZoom *zoom = NULL;

  zoom = get_current_zoom (self, NULL);
  if (zoom != NULL)
    bz_zoom_zoom_out (zoom);
}

static void
reset_zoom_clicked (BzScreenshotPage *self)
{
  BzZoom *zoom = NULL;

  zoom = get_current_zoom (self, NULL);
  if (zoom != NULL)
    bz_zoom_reset (zoom);
}

static void
previous_clicked (BzScreenshotPage *self)
{
  guint      n_pages = 0;
  GtkWidget *page    = NULL;

  n_pages = adw_carousel_get_n_pages (self->carousel);
  if (n_pages == 0)
    return;

  if (self->current_index > 0)
    page = adw_carousel_get_nth_page (self->carousel, self->current_index - 1);
  else
    page = adw_carousel_get_nth_page (self->carousel, n_pages - 1);

  if (page != NULL)
    adw_carousel_scroll_to (self->carousel, page, TRUE);
}

static void
next_clicked (BzScreenshotPage *self)
{
  guint      n_pages = 0;
  GtkWidget *page    = NULL;

  n_pages = adw_carousel_get_n_pages (self->carousel);
  if (n_pages == 0)
    return;

  if (self->current_index < n_pages - 1)
    page = adw_carousel_get_nth_page (self->carousel, self->current_index + 1);
  else
    page = adw_carousel_get_nth_page (self->carousel, 0);

  if (page != NULL)
    adw_carousel_scroll_to (self->carousel, page, TRUE);
}

static void
on_carousel_position_changed (AdwCarousel      *carousel,
                              GParamSpec       *pspec,
                              BzScreenshotPage *self)
{
  GtkScrolledWindow *old_scrolled_window = NULL;
  BzZoom            *old_zoom            = NULL;
  BzZoom            *new_zoom            = NULL;
  guint              new_index           = 0;
  guint              n_pages             = 0;

  new_index = (guint) round (adw_carousel_get_position (carousel));
  n_pages   = adw_carousel_get_n_pages (carousel);

  if (new_index == self->current_index || new_index >= n_pages)
    return;

  old_zoom = get_current_zoom (self, &old_scrolled_window);
  if (old_zoom != NULL)
    {
      g_signal_handlers_disconnect_by_func (old_zoom, on_zoom_level_changed, self);
      bz_zoom_reset (old_zoom);
      gtk_scrolled_window_set_policy (old_scrolled_window, GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    }

  self->current_index = new_index;

  new_zoom = get_current_zoom (self, NULL);
  g_assert (new_zoom != NULL);
  g_signal_connect (new_zoom, "notify::zoom-level",
                    G_CALLBACK (on_zoom_level_changed), self);

  update_is_zoomed (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_INDEX]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_CAPTION]);
}

static void
copy_clicked (BzScreenshotPage *self)
{
  g_autoptr (BzAsyncTexture) async_texture = NULL;
  g_autoptr (GdkTexture) texture           = NULL;
  GdkClipboard *clipboard                  = NULL;
  AdwToast     *toast                      = NULL;
  guint         n_items                    = 0;
  guint         actual_index               = 0;

  if (self->screenshots == NULL)
    return;

  n_items = g_list_model_get_n_items (self->screenshots);
  if (n_items == 0)
    return;

  actual_index = (self->initial_index + self->current_index) % n_items;

  async_texture = g_list_model_get_item (self->screenshots, actual_index);
  if (async_texture == NULL)
    return;

  texture = bz_async_texture_dup_texture (async_texture);
  if (texture == NULL)
    return;

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_texture (clipboard, texture);

  toast = adw_toast_new (_ ("Copied!"));
  adw_toast_set_timeout (toast, 1);
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
on_zoom_level_changed (BzZoom           *zoom,
                       GParamSpec       *pspec,
                       BzScreenshotPage *self)
{
  update_is_zoomed (self);
}

static gboolean
has_multiple_screenshots (GObject    *object,
                          GListModel *screenshots,
                          gpointer    user_data)
{
  if (screenshots == NULL)
    return FALSE;
  return g_list_model_get_n_items (screenshots) > 1;
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_valid_string (gpointer    object,
                 const char *value)
{
  return value != NULL && *value != '\0';
}

static void
bz_screenshot_page_dispose (GObject *object)
{
  BzScreenshotPage *self = BZ_SCREENSHOT_PAGE (object);

  g_clear_object (&self->screenshots);
  g_clear_object (&self->captions);
  g_clear_object (&self->source_texture);
  g_clear_object (&self->animation);

  G_OBJECT_CLASS (bz_screenshot_page_parent_class)->dispose (object);
}

static void
bz_screenshot_page_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzScreenshotPage *self = BZ_SCREENSHOT_PAGE (object);

  switch (prop_id)
    {
    case PROP_SCREENSHOTS:
      g_value_set_object (value, self->screenshots);
      break;
    case PROP_CURRENT_INDEX:
      g_value_set_uint (value, self->current_index);
      break;
    case PROP_CURRENT_CAPTION:
      {
        const char *caption = NULL;

        caption = bz_screenshot_page_get_current_caption (self);
        g_value_set_string (value, caption);
      }
      break;
    case PROP_IS_ZOOMED:
      g_value_set_boolean (value, self->is_zoomed);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_screenshot_page_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzScreenshotPage *self = BZ_SCREENSHOT_PAGE (object);

  switch (prop_id)
    {
    case PROP_SCREENSHOTS:
      g_set_object (&self->screenshots, g_value_get_object (value));
      break;
    case PROP_CURRENT_INDEX:
      self->initial_index = g_value_get_uint (value);
      break;
    case PROP_CURRENT_CAPTION:
    case PROP_IS_ZOOMED:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_screenshot_page_constructed (GObject *object)
{
  BzScreenshotPage *self       = BZ_SCREENSHOT_PAGE (object);
  BzZoom           *first_zoom = NULL;

  G_OBJECT_CLASS (bz_screenshot_page_parent_class)->constructed (object);

  populate_carousel (self);

  self->current_index = 0;

  first_zoom = get_current_zoom (self, NULL);
  if (first_zoom != NULL)
    g_signal_connect (first_zoom, "notify::zoom-level",
                      G_CALLBACK (on_zoom_level_changed), self);

  update_is_zoomed (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_CAPTION]);
}

static void
bz_screenshot_page_class_init (BzScreenshotPageClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_screenshot_page_dispose;
  object_class->constructed  = bz_screenshot_page_constructed;
  object_class->get_property = bz_screenshot_page_get_property;
  object_class->set_property = bz_screenshot_page_set_property;

  widget_class->map      = bz_screenshot_page_map;
  widget_class->unmap    = bz_screenshot_page_unmap;
  widget_class->snapshot = bz_screenshot_page_snapshot;

  props[PROP_SCREENSHOTS] =
      g_param_spec_object (
          "screenshots",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_CURRENT_INDEX] =
      g_param_spec_uint (
          "current-index",
          NULL, NULL,
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_CURRENT_CAPTION] =
      g_param_spec_string (
          "current-caption",
          NULL, NULL,
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_IS_ZOOMED] =
      g_param_spec_boolean (
          "is-zoomed",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_ZOOM);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-screenshot-page.ui");
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotPage, carousel);
  gtk_widget_class_bind_template_child (widget_class, BzScreenshotPage, toast_overlay);
  gtk_widget_class_bind_template_callback (widget_class, back_clicked);
  gtk_widget_class_bind_template_callback (widget_class, zoom_in_clicked);
  gtk_widget_class_bind_template_callback (widget_class, zoom_out_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_carousel_position_changed);
  gtk_widget_class_bind_template_callback (widget_class, reset_zoom_clicked);
  gtk_widget_class_bind_template_callback (widget_class, copy_clicked);
  gtk_widget_class_bind_template_callback (widget_class, previous_clicked);
  gtk_widget_class_bind_template_callback (widget_class, next_clicked);
  gtk_widget_class_bind_template_callback (widget_class, has_multiple_screenshots);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_valid_string);
}

static void
bz_screenshot_page_init (BzScreenshotPage *self)
{
  GtkEventController *key_controller = NULL;
  GtkEventController *scroll         = NULL;
  GtkGesture         *swipe          = NULL;
  GtkGesture         *click          = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  key_controller = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
  g_signal_connect (key_controller, "key-pressed",
                    G_CALLBACK (on_key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), key_controller);

  swipe = gtk_gesture_swipe_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (swipe), GTK_PHASE_CAPTURE);
  g_signal_connect_swapped (swipe, "swipe", G_CALLBACK (on_swipe), self);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (swipe), TRUE);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (swipe));

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
  g_signal_connect (click, "pressed", G_CALLBACK (on_button_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));

  scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  gtk_event_controller_set_propagation_phase (scroll, GTK_PHASE_BUBBLE);
  g_signal_connect_swapped (scroll, "scroll", G_CALLBACK (on_scroll), self);
  gtk_widget_add_controller (GTK_WIDGET (self), scroll);
}

const char *
bz_screenshot_page_get_current_caption (BzScreenshotPage *self)
{
  g_autoptr (GtkStringObject) caption_obj = NULL;
  guint n_items                           = 0;
  guint actual_index                      = 0;

  g_return_val_if_fail (BZ_IS_SCREENSHOT_PAGE (self), NULL);

  if (self->captions == NULL)
    return "";

  n_items = g_list_model_get_n_items (self->captions);
  if (n_items == 0)
    return "";

  actual_index = (self->initial_index + self->current_index) % n_items;

  caption_obj = g_list_model_get_item (self->captions, actual_index);
  if (caption_obj == NULL)
    return "";

  return gtk_string_object_get_string (caption_obj);
}

void
bz_screenshot_page_set_captions (BzScreenshotPage *self,
                                 GListModel       *captions)
{
  g_return_if_fail (BZ_IS_SCREENSHOT_PAGE (self));

  g_set_object (&self->captions, captions);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CURRENT_CAPTION]);
}

void
bz_screenshot_page_close (BzScreenshotPage *self)
{
  g_return_if_fail (BZ_IS_SCREENSHOT_PAGE (self));
  back_clicked (self);
}

gboolean
bz_screenshot_page_is_closing (BzScreenshotPage *self)
{
  return self->closing;
}

AdwBin *
bz_screenshot_page_new (GListModel *screenshots,
                        GListModel *captions,
                        guint       initial_index,
                        GtkWidget  *source_widget)
{
  BzScreenshotPage *page = NULL;

  page = g_object_new (
      BZ_TYPE_SCREENSHOT_PAGE,
      "screenshots", screenshots,
      "current-index", initial_index,
      NULL);

  page->source_widget = source_widget;

  if (captions != NULL)
    bz_screenshot_page_set_captions (page, captions);

  return ADW_BIN (page);
}

static GdkTexture *
render_widget_to_texture (GtkWidget *widget)
{
  g_autoptr (GtkWidgetPaintable) paintable = NULL;
  g_autoptr (GtkSnapshot) snapshot         = NULL;
  g_autoptr (GskRenderNode) node           = NULL;
  GtkNative  *native                       = NULL;
  GdkTexture *texture                      = NULL;

  paintable = GTK_WIDGET_PAINTABLE (gtk_widget_paintable_new (widget));
  snapshot  = gtk_snapshot_new ();

  gdk_paintable_snapshot (GDK_PAINTABLE (paintable), snapshot,
                          gdk_paintable_get_intrinsic_width (GDK_PAINTABLE (paintable)),
                          gdk_paintable_get_intrinsic_height (GDK_PAINTABLE (paintable)));

  node   = gtk_snapshot_to_node (snapshot);
  native = gtk_widget_get_native (widget);

  if (node != NULL && native != NULL)
    texture = gsk_renderer_render_texture (
        gtk_native_get_renderer (native), node, NULL);

  return texture;
}

static void
on_animation_value (AdwAnimation     *animation,
                    GParamSpec       *pspec,
                    BzScreenshotPage *self)
{
  self->animation_progress = adw_animation_get_value (animation);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_fade_animation_value (AdwAnimation     *animation,
                         GParamSpec       *pspec,
                         BzScreenshotPage *self)
{
  gtk_widget_set_opacity (GTK_WIDGET (self), adw_animation_get_value (animation));
}

static void
on_close_animation_done (AdwAnimation     *animation,
                         BzScreenshotPage *self)
{
  GtkWidget *parent = NULL;

  if (self->source_widget != NULL)
    gtk_widget_set_opacity (self->source_widget, 1.0);

  parent = gtk_widget_get_parent (GTK_WIDGET (self));
  if (parent != NULL)
    gtk_overlay_remove_overlay (GTK_OVERLAY (parent), GTK_WIDGET (self));
}

static void
populate_carousel (BzScreenshotPage *self)
{
  guint n_items = 0;
  guint i       = 0;

  if (self->screenshots == NULL)
    return;

  n_items = g_list_model_get_n_items (self->screenshots);
  if (n_items == 0)
    return;

  for (guint offset = 0; offset < n_items; offset++)
    {
      g_autoptr (BzAsyncTexture) async_texture = NULL;
      GtkWidget *scrolled_window               = NULL;
      GtkWidget *zoom_widget                   = NULL;
      GtkWidget *screenshot                    = NULL;

      i = (self->initial_index + offset) % n_items;

      async_texture = g_list_model_get_item (self->screenshots, i);
      if (async_texture == NULL)
        continue;

      screenshot = bz_screenshot_new ();
      bz_screenshot_set_paintable (BZ_SCREENSHOT (screenshot), GDK_PAINTABLE (async_texture));
      bz_screenshot_set_rounded_corners (BZ_SCREENSHOT (screenshot), FALSE);
      gtk_widget_set_margin_top (screenshot, 25);
      gtk_widget_set_margin_bottom (screenshot, 25);
      gtk_widget_set_margin_start (screenshot, 25);
      gtk_widget_set_margin_end (screenshot, 25);

      zoom_widget = bz_zoom_new ();
      bz_zoom_set_child (BZ_ZOOM (zoom_widget), screenshot);

      scrolled_window = gtk_scrolled_window_new ();
      gtk_widget_set_hexpand (scrolled_window, TRUE);
      gtk_widget_set_vexpand (scrolled_window, TRUE);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
      gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), zoom_widget);

      adw_carousel_append (self->carousel, scrolled_window);
    }
}

static void
update_is_zoomed (BzScreenshotPage *self)
{
  GtkWidget         *screenshot      = NULL;
  BzZoom            *zoom            = NULL;
  GtkScrolledWindow *scrolled_window = NULL;
  double             zoom_level      = 0.0;
  gboolean           was_zoomed      = FALSE;

  was_zoomed = self->is_zoomed;
  zoom       = get_current_zoom (self, &scrolled_window);

  if (zoom != NULL)
    {
      g_object_get (zoom, "zoom-level", &zoom_level, NULL);

      screenshot = bz_zoom_get_child (zoom);
      if (screenshot != NULL)
        bz_screenshot_set_filter (
            BZ_SCREENSHOT (screenshot),
            zoom_level <= 4.5
                ? GSK_SCALING_FILTER_TRILINEAR
                : GSK_SCALING_FILTER_NEAREST);
    }

  self->is_zoomed = zoom != NULL && bz_zoom_is_transformed (zoom);

  if (was_zoomed != self->is_zoomed)
    {
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_ZOOMED]);
      if (self->is_zoomed)
        gtk_scrolled_window_set_policy (scrolled_window, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      else
        gtk_scrolled_window_set_policy (scrolled_window, GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    }
}

static BzZoom *
get_current_zoom (BzScreenshotPage   *self,
                  GtkScrolledWindow **scrolled_window)
{
  guint      n_pages = 0;
  GtkWidget *page    = NULL;
  GtkWidget *zoom    = NULL;

  n_pages = adw_carousel_get_n_pages (self->carousel);
  if (self->current_index >= n_pages)
    return NULL;

  page = adw_carousel_get_nth_page (self->carousel, self->current_index);
  if (page == NULL)
    return NULL;

  zoom = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (page));

  if (zoom != NULL)
    {
      if (scrolled_window != NULL)
        *scrolled_window = (GtkScrolledWindow *) page;
      return BZ_ZOOM (zoom);
    }
  else
    return NULL;
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                BzScreenshotPage      *self)
{
  if (keyval == GDK_KEY_Left)
    {
      previous_clicked (self);
      return TRUE;
    }
  else if (keyval == GDK_KEY_Right)
    {
      next_clicked (self);
      return TRUE;
    }

  return FALSE;
}

static void
on_button_pressed (GtkGestureClick  *gesture,
                   int               n_press,
                   double            x,
                   double            y,
                   BzScreenshotPage *self)
{
  if (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture)) == MOUSE_BACK_BUTTON)
    back_clicked (self);
}

static void
on_swipe (BzScreenshotPage *self,
          gdouble           vel_x,
          gdouble           vel_y)
{
  if (vel_y < -500.0 || vel_y > 500.0)
    back_clicked (self);
}

static gboolean
on_scroll (BzScreenshotPage         *self,
           gdouble                   dx,
           gdouble                   dy,
           GtkEventControllerScroll *controller)
{
  GdkEvent      *event  = NULL;
  GdkDevice     *device = NULL;
  GdkInputSource source = 0;

  event  = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (controller));
  device = gdk_event_get_device (event);
  source = gdk_device_get_source (device);

  if (source == GDK_SOURCE_TOUCHPAD && (dy < -2.0 || dy > 2.0))
    back_clicked (self);

  return GDK_EVENT_PROPAGATE;
}
