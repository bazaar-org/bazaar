/* bz-featured-tile.c
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

#include "bz-featured-tile.h"
#include "bz-entry.h"
#include "bz-group-tile-css-watcher.h"
#include "bz-screenshot.h"
#include "util.h"

#define BZ_TYPE_FEATURED_TILE_LAYOUT (bz_featured_tile_layout_get_type ())
G_DECLARE_FINAL_TYPE (BzFeaturedTileLayout, bz_featured_tile_layout, BZ, FEATURED_TILE_LAYOUT, GtkLayoutManager)

struct _BzFeaturedTileLayout
{
  GtkLayoutManager parent_instance;

  gboolean   narrow_mode;
  GtkWidget *content_box;
  int        last_width;
};

G_DEFINE_FINAL_TYPE (BzFeaturedTileLayout, bz_featured_tile_layout, GTK_TYPE_LAYOUT_MANAGER)

enum
{
  LAYOUT_SIGNAL_NARROW_MODE_CHANGED,
  LAYOUT_SIGNAL_LAST
};

static guint layout_signals[LAYOUT_SIGNAL_LAST] = { 0 };

static void
bz_featured_tile_layout_measure (GtkLayoutManager *layout_manager,
                                 GtkWidget        *widget,
                                 GtkOrientation    orientation,
                                 int               for_size,
                                 int              *minimum,
                                 int              *natural,
                                 int              *minimum_baseline,
                                 int              *natural_baseline)
{
  GtkWidget *child;

  *minimum          = 0;
  *natural          = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      int child_min;
      int child_nat;

      if (!gtk_widget_should_layout (child))
        continue;

      gtk_widget_measure (child, orientation, for_size,
                          &child_min, &child_nat, NULL, NULL);

      *minimum = MAX (*minimum, child_min);
      *natural = MAX (*natural, child_nat);
    }
}

static void
bz_featured_tile_layout_allocate (GtkLayoutManager *layout_manager,
                                  GtkWidget        *widget,
                                  gint              width,
                                  gint              height,
                                  gint              baseline)
{
  BzFeaturedTileLayout *self;
  GtkWidget            *child;
  gboolean              narrow_mode;
  int                   spacing;
  const int             NARROW_THRESHOLD = 950;
  const int             MIN_SPACING      = 15;
  const int             MAX_SPACING      = 128;
  const int             MAX_WIDTH        = 1300;

  self = BZ_FEATURED_TILE_LAYOUT (layout_manager);

  narrow_mode = (width < NARROW_THRESHOLD);

  if (self->content_box != NULL && self->last_width != width)
    {
      self->last_width = width;

      if (narrow_mode)
        {
          spacing = 100;
        }
      else
        {
          if (width < NARROW_THRESHOLD)
            spacing = MIN_SPACING;
          else if (width >= MAX_WIDTH)
            spacing = MAX_SPACING;
          else
            spacing = MIN_SPACING + ((width - NARROW_THRESHOLD) * (MAX_SPACING - MIN_SPACING)) / (MAX_WIDTH - NARROW_THRESHOLD);
        }

      gtk_box_set_spacing (GTK_BOX (self->content_box), spacing);
    }

  for (child = gtk_widget_get_first_child (widget);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (gtk_widget_should_layout (child))
        gtk_widget_allocate (child, width, height, -1, NULL);
    }

  if (self->narrow_mode != narrow_mode)
    {
      self->narrow_mode = narrow_mode;
      g_signal_emit (self, layout_signals[LAYOUT_SIGNAL_NARROW_MODE_CHANGED], 0, self->narrow_mode);
    }
}

static void
bz_featured_tile_layout_class_init (BzFeaturedTileLayoutClass *klass)
{
  GtkLayoutManagerClass *layout_manager_class;

  layout_manager_class = GTK_LAYOUT_MANAGER_CLASS (klass);

  layout_manager_class->measure  = bz_featured_tile_layout_measure;
  layout_manager_class->allocate = bz_featured_tile_layout_allocate;

  layout_signals[LAYOUT_SIGNAL_NARROW_MODE_CHANGED] =
      g_signal_new ("narrow-mode-changed",
                    G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                    0, NULL, NULL, NULL,
                    G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
bz_featured_tile_layout_init (BzFeaturedTileLayout *self)
{
  self->last_width = -1;
}

struct _BzFeaturedTile
{
  GtkButton parent_instance;

  BzEntryGroup *group;
  gboolean      narrow_mode;
  guint         refresh_id;

  BzGroupTileCssWatcher *css;

  GtkWidget *stack;
  GtkWidget *image;
  GtkWidget *title;
  GtkWidget *description;
  GtkWidget *screenshot;
  GtkWidget *content_box;

  GdkPaintable *first_screenshot;
  gboolean      has_screenshot;
  DexFuture    *ui_entry_resolve;
};

G_DEFINE_FINAL_TYPE (BzFeaturedTile, bz_featured_tile, GTK_TYPE_BUTTON)

enum
{
  PROP_0,
  PROP_GROUP,
  PROP_FIRST_SCREENSHOT,
  PROP_HAS_SCREENSHOT,
  PROP_NARROW,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = {
  NULL,
};

static void bz_featured_tile_refresh (BzFeaturedTile *self);
static void update_screenshot (BzFeaturedTile *self);

static gboolean
bz_featured_tile_refresh_idle_cb (GWeakRef *wr)
{
  g_autoptr (BzFeaturedTile) self = NULL;

  self = g_weak_ref_get (wr);
  if (self == NULL)
    goto done;

  self->refresh_id = 0;
  bz_featured_tile_refresh (self);

done:
  return G_SOURCE_REMOVE;
}

static void
schedule_refresh (BzFeaturedTile *self)
{
  if (self->refresh_id != 0)
    return;

  self->refresh_id = g_idle_add_full (
      G_PRIORITY_DEFAULT,
      (GSourceFunc) bz_featured_tile_refresh_idle_cb,
      bz_track_weak (self),
      bz_weak_release);
}

static void
bz_featured_tile_layout_narrow_mode_changed_cb (GtkLayoutManager *layout_manager,
                                                gboolean          narrow_mode,
                                                gpointer          user_data)
{
  BzFeaturedTile *self;

  self = BZ_FEATURED_TILE (user_data);

  if (self->narrow_mode != narrow_mode)
    {
      self->narrow_mode = narrow_mode;
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NARROW]);
      schedule_refresh (self);
    }
}

static inline void
notify_properties (BzFeaturedTile *self, gboolean has_screenshot)
{
  if (self->has_screenshot != has_screenshot)
    {
      self->has_screenshot = has_screenshot;
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_SCREENSHOT]);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FIRST_SCREENSHOT]);
}

static DexFuture *
ui_entry_resolved_finally (DexFuture *future,
                           GWeakRef  *wr)
{
  g_autoptr (BzFeaturedTile) self = NULL;
  const GValue *value             = NULL;
  gboolean      has_screenshot    = FALSE;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, NULL);
  if (value != NULL)
    {
      BzEntry *ui_entry                  = NULL;
      g_autoptr (GListModel) screenshots = NULL;

      ui_entry = g_value_get_object (value);

      g_object_get (ui_entry, "screenshot-paintables", &screenshots, NULL);
      if (screenshots != NULL &&
          g_list_model_get_n_items (screenshots) > 0)
        {
          self->first_screenshot = g_list_model_get_item (screenshots, 0);
          has_screenshot         = TRUE;
        }
    }

  dex_clear (&self->ui_entry_resolve);
  notify_properties (self, has_screenshot);

  return NULL;
}

/* FIXME: duplicate code from rich-app-tile */
static void
update_screenshot (BzFeaturedTile *self)
{
  g_autoptr (BzResult) ui_entry_result = NULL;
  g_autoptr (GListModel) screenshots   = NULL;

  dex_clear (&self->ui_entry_resolve);
  g_clear_object (&self->first_screenshot);

  if (self->group == NULL)
    {
      notify_properties (self, FALSE);
      return;
    }

  ui_entry_result        = bz_entry_group_dup_ui_entry (self->group);
  self->ui_entry_resolve = dex_future_finally (
      bz_result_dup_future (ui_entry_result),
      (DexFutureCallback) ui_entry_resolved_finally,
      bz_track_weak (self),
      bz_weak_release);
}

static void
bz_featured_tile_refresh (BzFeaturedTile *self)
{
  gtk_label_set_wrap (GTK_LABEL (self->description), self->narrow_mode);
  gtk_label_set_lines (GTK_LABEL (self->description), self->narrow_mode ? 2 : 1);

  update_screenshot (self);
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static int
get_start_margin (gpointer object, gboolean narrow)
{
  return narrow ? 20 : 50;
}

static void
bz_featured_tile_dispose (GObject *object)
{
  BzFeaturedTile *self;

  self = BZ_FEATURED_TILE (object);

  g_clear_handle_id (&self->refresh_id, g_source_remove);
  if (self->group != NULL)
    g_signal_handlers_disconnect_by_func (self->group, schedule_refresh, self);

  g_clear_object (&self->group);
  g_clear_object (&self->css);
  g_clear_object (&self->first_screenshot);

  G_OBJECT_CLASS (bz_featured_tile_parent_class)->dispose (object);
}

static void
bz_featured_tile_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzFeaturedTile *self;

  self = BZ_FEATURED_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      g_value_set_object (value, bz_featured_tile_get_group (self));
      break;
    case PROP_FIRST_SCREENSHOT:
      g_value_set_object (value, self->first_screenshot);
      break;
    case PROP_HAS_SCREENSHOT:
      g_value_set_boolean (value, self->has_screenshot);
      break;
    case PROP_NARROW:
      g_value_set_boolean (value, self->narrow_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
bz_featured_tile_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzFeaturedTile *self;

  self = BZ_FEATURED_TILE (object);

  switch (prop_id)
    {
    case PROP_GROUP:
      bz_featured_tile_set_group (self, g_value_get_object (value));
      break;
    case PROP_FIRST_SCREENSHOT:
    case PROP_HAS_SCREENSHOT:
    case PROP_NARROW:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
bz_featured_tile_class_init (BzFeaturedTileClass *klass)
{
  GObjectClass   *object_class;
  GtkWidgetClass *widget_class;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_featured_tile_dispose;
  object_class->get_property = bz_featured_tile_get_property;
  object_class->set_property = bz_featured_tile_set_property;

  props[PROP_GROUP] =
      g_param_spec_object ("group", NULL, NULL,
                           BZ_TYPE_ENTRY_GROUP,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_FIRST_SCREENSHOT] =
      g_param_spec_object ("first-screenshot", NULL, NULL,
                           GDK_TYPE_PAINTABLE,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_HAS_SCREENSHOT] =
      g_param_spec_boolean ("has-screenshot", NULL, NULL,
                            FALSE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_NARROW] =
      g_param_spec_boolean ("narrow", NULL, NULL,
                            FALSE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_SCREENSHOT);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-featured-tile.ui");
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, get_start_margin);

  gtk_widget_class_bind_template_child (widget_class, BzFeaturedTile, image);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedTile, title);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedTile, description);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedTile, screenshot);
  gtk_widget_class_bind_template_child (widget_class, BzFeaturedTile, content_box);

  gtk_widget_class_set_css_name (widget_class, "featured-tile");
  gtk_widget_class_set_layout_manager_type (widget_class, BZ_TYPE_FEATURED_TILE_LAYOUT);
}

static void
bz_featured_tile_init (BzFeaturedTile *self)
{
  GtkLayoutManager     *layout_manager;
  BzFeaturedTileLayout *tile_layout;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->css = bz_group_tile_css_watcher_new ();
  bz_group_tile_css_watcher_set_widget (self->css, GTK_WIDGET (self));

  layout_manager = gtk_widget_get_layout_manager (GTK_WIDGET (self));
  g_warn_if_fail (layout_manager != NULL);

  tile_layout              = BZ_FEATURED_TILE_LAYOUT (layout_manager);
  tile_layout->content_box = self->content_box;

  g_signal_connect_object (layout_manager, "narrow-mode-changed",
                           G_CALLBACK (bz_featured_tile_layout_narrow_mode_changed_cb), self, 0);
}

BzFeaturedTile *
bz_featured_tile_new (BzEntryGroup *group)
{
  return g_object_new (BZ_TYPE_FEATURED_TILE,
                       "group", group,
                       NULL);
}

BzEntryGroup *
bz_featured_tile_get_group (BzFeaturedTile *self)
{
  g_return_val_if_fail (BZ_IS_FEATURED_TILE (self), NULL);
  return self->group;
}

void
bz_featured_tile_set_group (BzFeaturedTile *self,
                            BzEntryGroup   *group)
{
  g_return_if_fail (BZ_IS_FEATURED_TILE (self));
  g_return_if_fail (group == NULL || BZ_IS_ENTRY_GROUP (group));

  g_clear_handle_id (&self->refresh_id, g_source_remove);
  if (self->group != NULL)
    g_signal_handlers_disconnect_by_func (self->group, schedule_refresh, self);

  g_clear_object (&self->group);
  if (group != NULL)
    {
      self->group = g_object_ref (group);

      g_signal_connect_swapped (group, "notify",
                                G_CALLBACK (schedule_refresh), self);
      schedule_refresh (self);
    }

  bz_group_tile_css_watcher_set_group (self->css, group);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_GROUP]);
}
