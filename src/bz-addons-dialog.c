/* bz-addons-dialog.c
 *
 * Copyright 2025 Adam Masciola, Alexander Vanhee
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

#include "bz-addon-tile.h"
#include "bz-addons-dialog.h"
#include "bz-app-size-dialog.h"
#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-appstream-description-render.h"
#include "bz-context-tile-callbacks.h"
#include "bz-context-tile.h"
#include "bz-entry-group.h"
#include "bz-fading-clamp.h"
#include "bz-flatpak-entry.h"
#include "bz-install-controls.h"
#include "bz-license-dialog.h"
#include "bz-result.h"
#include "bz-share-list.h"
#include "bz-state-info.h"
#include "bz-stats-dialog.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"

struct _BzAddonsDialog
{
  AdwDialog parent_instance;

  GListModel   *addon_groups;
  BzEntryGroup *selected_group;
  BzResult     *selected_ui_entry;
  DexFuture    *selected_ui_future;
  BzResult     *parent_ui_entry;
  DexFuture    *parent_ui_future;
  BzStateInfo  *state;

  AdwAnimation *width_animation;
  AdwAnimation *height_animation;

  /* Template widgets */
  AdwNavigationView *navigation_view;
  GtkToggleButton   *description_toggle;
  AdwClamp          *full_view_clamp;
  AdwClamp          *list_clamp;
  GtkCustomSorter   *sorter;
};

G_DEFINE_FINAL_TYPE (BzAddonsDialog, bz_addons_dialog, ADW_TYPE_DIALOG)

enum
{
  PROP_0,

  PROP_ADDON_GROUPS,
  PROP_SELECTED_GROUP,
  PROP_SELECTED_UI_ENTRY,
  PROP_PARENT_UI_ENTRY,
  PROP_STATE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static char      *format_parent_title (gpointer object, const char *title);
static int        get_description_max_height (gpointer object, gboolean active);
static char      *get_description_toggle_text (gpointer object, gboolean active);
static void       size_cb (BzAddonsDialog *self, GtkButton *button);
static void       license_cb (BzAddonsDialog *self, GtkButton *button);
static void       dl_stats_cb (BzAddonsDialog *self, GtkButton *button);
static void       animate_to_size (BzAddonsDialog *self);
static void       on_visible_page_tag_changed (AdwNavigationView *nav_view, GParamSpec *pspec, BzAddonsDialog *self);
static DexFuture *on_parent_ui_entry_resolved (DexFuture *future, GWeakRef *wr);
static DexFuture *on_selected_ui_entry_resolved (DexFuture *future, GWeakRef *wr);
static void       set_selected_group (BzAddonsDialog *self, BzEntryGroup *group);
static void       tile_activated_cb (BzAddonTile *tile);
static int        sort_func (BzEntryGroup *a, BzEntryGroup *b, BzAddonsDialog *self);

static void
bz_addons_dialog_dispose (GObject *object)
{
  BzAddonsDialog *self = BZ_ADDONS_DIALOG (object);

  dex_clear (&self->selected_ui_future);
  dex_clear (&self->parent_ui_future);
  g_clear_object (&self->addon_groups);
  g_clear_object (&self->selected_group);
  g_clear_object (&self->selected_ui_entry);
  g_clear_object (&self->parent_ui_entry);
  g_clear_object (&self->state);
  g_clear_object (&self->width_animation);
  g_clear_object (&self->height_animation);

  G_OBJECT_CLASS (bz_addons_dialog_parent_class)->dispose (object);
}

static void
bz_addons_dialog_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzAddonsDialog *self = BZ_ADDONS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ADDON_GROUPS:
      g_value_set_object (value, self->addon_groups);
      break;
    case PROP_SELECTED_GROUP:
      g_value_set_object (value, self->selected_group);
      break;
    case PROP_SELECTED_UI_ENTRY:
      g_value_set_object (value, self->selected_ui_entry);
      break;
    case PROP_PARENT_UI_ENTRY:
      g_value_set_object (value, self->parent_ui_entry);
      break;
    case PROP_STATE:
      g_value_set_object (value, bz_state_info_get_default ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_addons_dialog_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzAddonsDialog *self = BZ_ADDONS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ADDON_GROUPS:
      g_clear_object (&self->addon_groups);
      self->addon_groups = g_value_dup_object (value);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ADDON_GROUPS]);
      break;
    case PROP_SELECTED_GROUP:
      g_clear_object (&self->selected_group);
      self->selected_group = g_value_dup_object (value);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_GROUP]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_addons_dialog_class_init (BzAddonsDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_addons_dialog_dispose;
  object_class->get_property = bz_addons_dialog_get_property;
  object_class->set_property = bz_addons_dialog_set_property;

  props[PROP_ADDON_GROUPS] =
      g_param_spec_object (
          "addon-groups",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_SELECTED_GROUP] =
      g_param_spec_object (
          "selected-group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_SELECTED_UI_ENTRY] =
      g_param_spec_object (
          "selected-ui-entry",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PARENT_UI_ENTRY] =
      g_param_spec_object (
          "parent-ui-entry",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_ADDON_TILE);
  g_type_ensure (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER);
  g_type_ensure (BZ_TYPE_CONTEXT_TILE);
  g_type_ensure (BZ_TYPE_ENTRY);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_FADING_CLAMP);
  g_type_ensure (BZ_TYPE_FLATPAK_ENTRY);
  g_type_ensure (BZ_TYPE_INSTALL_CONTROLS);
  g_type_ensure (BZ_TYPE_RESULT);
  g_type_ensure (BZ_TYPE_SHARE_LIST);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-addons-dialog.ui");

  bz_widget_class_bind_all_util_callbacks (widget_class);
  bz_widget_class_bind_all_context_tile_callbacks (widget_class);

  gtk_widget_class_bind_template_child (widget_class, BzAddonsDialog, navigation_view);
  gtk_widget_class_bind_template_child (widget_class, BzAddonsDialog, description_toggle);
  gtk_widget_class_bind_template_child (widget_class, BzAddonsDialog, full_view_clamp);
  gtk_widget_class_bind_template_child (widget_class, BzAddonsDialog, list_clamp);
  gtk_widget_class_bind_template_child (widget_class, BzAddonsDialog, sorter);

  gtk_widget_class_bind_template_callback (widget_class, tile_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_visible_page_tag_changed);
  gtk_widget_class_bind_template_callback (widget_class, format_parent_title);
  gtk_widget_class_bind_template_callback (widget_class, get_description_max_height);
  gtk_widget_class_bind_template_callback (widget_class, get_description_toggle_text);

  gtk_widget_class_bind_template_callback (widget_class, license_cb);
  gtk_widget_class_bind_template_callback (widget_class, size_cb);
  gtk_widget_class_bind_template_callback (widget_class, dl_stats_cb);
}

static void
bz_addons_dialog_init (BzAddonsDialog *self)
{
  AdwAnimationTarget *width_target  = NULL;
  AdwAnimationTarget *height_target = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  width_target           = adw_property_animation_target_new (G_OBJECT (self), "content-width");
  self->width_animation  = adw_timed_animation_new (GTK_WIDGET (self), 0, 0, 300, width_target);
  height_target          = adw_property_animation_target_new (G_OBJECT (self), "content-height");
  self->height_animation = adw_timed_animation_new (GTK_WIDGET (self), 0, 0, 300, height_target);

  g_signal_connect_swapped (self, "map", G_CALLBACK (animate_to_size), self);

  gtk_custom_sorter_set_sort_func (self->sorter, (GCompareDataFunc) sort_func, self, NULL);
}

AdwDialog *
bz_addons_dialog_new (BzEntryGroup *group)
{
  GListModel              *ids     = NULL;
  GListModel              *groups  = NULL;
  BzApplicationMapFactory *factory = NULL;
  BzAddonsDialog          *self    = NULL;

  ids = bz_entry_group_get_addon_group_ids (group);
  if (ids != NULL)
    {
      factory = bz_state_info_get_application_factory (bz_state_info_get_default ());
      if (factory != NULL)
        groups = bz_application_map_factory_generate (factory, ids);
    }

  self = g_object_new (
      BZ_TYPE_ADDONS_DIALOG,
      "addon-groups", groups,
      NULL);

  self->parent_ui_entry = bz_entry_group_dup_ui_entry (group);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARENT_UI_ENTRY]);

  if (groups == NULL || g_list_model_get_n_items (groups) == 0)
    adw_navigation_view_replace (self->navigation_view,
                                 (AdwNavigationPage *[]) { adw_navigation_view_find_page (self->navigation_view, "empty") },
                                 1);
  else if (g_list_model_get_n_items (groups) == 1)
    {
      g_autoptr (BzEntryGroup) single   = g_list_model_get_item (groups, 0);
      AdwNavigationPage *full_view_page = NULL;

      set_selected_group (self, single);
      full_view_page = adw_navigation_view_find_page (self->navigation_view, "full-view");
      adw_navigation_view_replace (self->navigation_view, &full_view_page, 1);
    }

  return ADW_DIALOG (self);
}

AdwDialog *
bz_addons_dialog_new_single (BzEntryGroup *group)
{
  BzAddonsDialog    *self      = NULL;
  AdwNavigationPage *full_view = NULL;

  self = g_object_new (BZ_TYPE_ADDONS_DIALOG, NULL);

  set_selected_group (self, group);

  full_view = adw_navigation_view_find_page (self->navigation_view, "full-view");
  adw_navigation_view_replace (self->navigation_view, &full_view, 1);

  return ADW_DIALOG (self);
}

static char *
format_parent_title (gpointer    object,
                     const char *title)
{
  if (title == NULL || *title == '\0')
    return g_strdup ("");

  return g_strdup_printf (_ ("Add-on for %s"), title);
}

static int
get_description_max_height (gpointer object,
                            gboolean active)
{
  return active ? 10000 : 170;
}

static char *
get_description_toggle_text (gpointer object,
                             gboolean active)
{
  return g_strdup (active ? _ ("Show Less") : _ ("Show More"));
}

static void
size_cb (BzAddonsDialog *self,
         GtkButton      *button)
{
  AdwNavigationPage *page = NULL;

  if (self->selected_group == NULL)
    return;

  page = bz_app_size_page_new (self->selected_group);
  adw_navigation_view_push (self->navigation_view, page);
}

static void
license_cb (BzAddonsDialog *self,
            GtkButton      *button)
{
  AdwNavigationPage *page     = NULL;
  BzEntry           *ui_entry = NULL;

  if (self->selected_ui_entry == NULL)
    return;

  ui_entry = bz_result_get_object (self->selected_ui_entry);
  if (ui_entry == NULL)
    return;

  page = bz_license_page_new (ui_entry);
  adw_navigation_view_push (self->navigation_view, page);
}

static void
dl_stats_cb (BzAddonsDialog *self,
             GtkButton      *button)
{
  BzStatsDialog     *bin      = NULL;
  AdwNavigationPage *page     = NULL;
  BzEntry           *ui_entry = NULL;

  if (self->selected_ui_entry == NULL)
    return;

  ui_entry = bz_result_get_object (self->selected_ui_entry);
  if (ui_entry == NULL)
    return;

  bin  = BZ_STATS_DIALOG (bz_stats_dialog_new (NULL, NULL, 0));
  page = adw_navigation_page_new (GTK_WIDGET (bin), _ ("Download Stats"));
  adw_navigation_page_set_tag (page, "stats");

  g_object_bind_property (ui_entry, "download-stats", bin, "model", G_BINDING_SYNC_CREATE);
  g_object_bind_property (ui_entry, "total-downloads", bin, "total-downloads", G_BINDING_SYNC_CREATE);

  adw_navigation_view_push (self->navigation_view, page);
  bz_stats_dialog_animate_open (bin);
}

static void
animate_to_size (BzAddonsDialog *self)
{
  const char *tag           = NULL;
  int         target_width  = 0;
  int         target_height = 0;
  int         nat           = 0;
  int         cur_width     = 0;
  int         measure_for   = 0;
  int         cur_w         = 0;
  int         cur_h         = 0;
  int         delta_w       = 0;
  int         delta_h       = 0;
  int         delta         = 0;
  guint       duration      = 0;

  tag = adw_navigation_view_get_visible_page_tag (self->navigation_view);

  if (g_strcmp0 (tag, "list") == 0)
    {
      cur_width    = gtk_widget_get_width (GTK_WIDGET (self));
      target_width = 500;
      measure_for  = MAX (-1, MIN (target_width, cur_width) - 48);
      gtk_widget_measure (GTK_WIDGET (self->list_clamp), GTK_ORIENTATION_VERTICAL, measure_for, NULL, &nat, NULL, NULL);
      target_height = CLAMP (nat + 50, 150, 600);
    }
  else if (g_strcmp0 (tag, "full-view") == 0)
    {
      cur_width    = gtk_widget_get_width (GTK_WIDGET (self));
      target_width = 500;
      measure_for  = MAX (-1, MIN (target_width, cur_width) - 48);
      gtk_widget_measure (GTK_WIDGET (self->full_view_clamp), GTK_ORIENTATION_VERTICAL, measure_for, NULL, &nat, NULL, NULL);
      target_height = CLAMP (nat + 50, 300, 700);
    }
  else if (g_strcmp0 (tag, "app-size") == 0)
    {
      target_width  = 500;
      target_height = 300;
    }
  else if (g_strcmp0 (tag, "license") == 0)
    {
      cur_width    = gtk_widget_get_width (GTK_WIDGET (self));
      target_width = 400;
      measure_for  = target_width - 48;
      gtk_widget_measure (GTK_WIDGET (self->navigation_view), GTK_ORIENTATION_VERTICAL, measure_for, NULL, &nat, NULL, NULL);
      target_height = CLAMP (nat, 300, 700);
    }
  else if (g_strcmp0 (tag, "stats") == 0)
    {
      target_width  = 1250;
      target_height = 750;
    }
  else if (g_strcmp0 (tag, "empty") == 0)
    {
      target_width  = 500;
      target_height = 500;
    }
  else
    return;

  cur_w   = adw_dialog_get_content_width (ADW_DIALOG (self));
  cur_h   = adw_dialog_get_content_height (ADW_DIALOG (self));
  delta_w = ABS (target_width - cur_w);
  delta_h = ABS (target_height - cur_h);
  delta   = MAX (delta_w, delta_h);

  duration = (guint) CLAMP (delta * 0.6, 200, (target_width < cur_w || target_height < cur_h) ? 300 : 600);

  adw_timed_animation_set_duration (ADW_TIMED_ANIMATION (self->width_animation), duration);
  adw_timed_animation_set_duration (ADW_TIMED_ANIMATION (self->height_animation), duration);

  adw_timed_animation_set_value_from (ADW_TIMED_ANIMATION (self->width_animation), cur_w);
  adw_timed_animation_set_value_to (ADW_TIMED_ANIMATION (self->width_animation), target_width);
  adw_timed_animation_set_value_from (ADW_TIMED_ANIMATION (self->height_animation), cur_h);
  adw_timed_animation_set_value_to (ADW_TIMED_ANIMATION (self->height_animation), target_height);
  adw_animation_play (self->width_animation);
  adw_animation_play (self->height_animation);
}

static void
on_visible_page_tag_changed (AdwNavigationView *nav_view,
                             GParamSpec        *pspec,
                             BzAddonsDialog    *self)
{
  g_idle_add_once ((GSourceOnceFunc) animate_to_size, self);
}

static DexFuture *
on_parent_ui_entry_resolved (DexFuture *future,
                             GWeakRef  *wr)
{
  g_autoptr (BzAddonsDialog) self = NULL;

  bz_weak_get_or_return_reject (self, wr);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARENT_UI_ENTRY]);

  return dex_future_new_for_boolean (TRUE);
}

static DexFuture *
on_selected_ui_entry_resolved (DexFuture *future,
                               GWeakRef  *wr)
{
  g_autoptr (BzAddonsDialog) self       = NULL;
  const GValue *value                   = NULL;
  g_autoptr (BzEntry) ui_entry          = NULL;
  const char *ref                       = NULL;
  g_auto (GStrv) parts                  = NULL;
  BzApplicationMapFactory *factory      = NULL;
  GtkStringObject         *item         = NULL;
  BzEntryGroup            *parent_group = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, NULL);
  if (value == NULL || !G_VALUE_HOLDS_OBJECT (value))
    return dex_future_new_for_boolean (TRUE);

  ui_entry = g_value_dup_object (value);
  if (ui_entry == NULL || !BZ_IS_FLATPAK_ENTRY (ui_entry))
    return dex_future_new_for_boolean (TRUE);

  ref = bz_flatpak_entry_get_addon_extension_of_ref (BZ_FLATPAK_ENTRY (ui_entry));
  if (ref == NULL)
    return dex_future_new_for_boolean (TRUE);

  parts = g_strsplit (ref, "/", -1);
  if (parts[0] == NULL || parts[1] == NULL)
    return dex_future_new_for_boolean (TRUE);

  factory      = bz_state_info_get_application_factory (bz_state_info_get_default ());
  item         = gtk_string_object_new (parts[1]);
  parent_group = bz_application_map_factory_convert_one (factory, item);

  if (parent_group == NULL)
    return dex_future_new_for_boolean (TRUE);

  g_clear_object (&self->parent_ui_entry);
  self->parent_ui_entry = bz_entry_group_dup_ui_entry (parent_group);

  if (self->parent_ui_entry == NULL)
    return dex_future_new_for_boolean (TRUE);

  if (bz_result_get_resolved (self->parent_ui_entry))
    {
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARENT_UI_ENTRY]);
    }
  else
    {
      g_autoptr (DexFuture) parent_future = NULL;
      GWeakRef *parent_wr                 = NULL;

      parent_future = bz_result_dup_future (self->parent_ui_entry);
      parent_wr     = bz_track_weak (self);
      parent_future = dex_future_then (
          parent_future,
          (DexFutureCallback) on_parent_ui_entry_resolved,
          parent_wr,
          bz_weak_release);
      dex_clear (&self->parent_ui_future);
      self->parent_ui_future = g_steal_pointer (&parent_future);
    }

  return dex_future_new_for_boolean (TRUE);
}

static void
set_selected_group (BzAddonsDialog *self,
                    BzEntryGroup   *group)
{
  dex_clear (&self->selected_ui_future);
  dex_clear (&self->parent_ui_future);
  g_clear_object (&self->selected_group);
  g_clear_object (&self->selected_ui_entry);
  g_clear_object (&self->parent_ui_entry);

  gtk_toggle_button_set_active (self->description_toggle, FALSE);

  if (group == NULL)
    return;

  self->selected_group    = g_object_ref (group);
  self->selected_ui_entry = bz_entry_group_dup_ui_entry (group);

  if (self->selected_ui_entry == NULL)
    goto notify;

  if (bz_result_get_resolved (self->selected_ui_entry))
    {
      g_autoptr (BzEntry) entry           = NULL;
      g_autoptr (DexFuture) object_future = NULL;
      GWeakRef *wr                        = NULL;

      entry         = g_object_ref (bz_result_get_object (self->selected_ui_entry));
      object_future = dex_future_new_for_object (entry);
      wr            = bz_track_weak (self);
      dex_unref (on_selected_ui_entry_resolved (object_future, wr));
      bz_weak_release (wr);
    }
  else
    {
      g_autoptr (DexFuture) ui_future = NULL;
      GWeakRef *wr                    = NULL;

      ui_future = bz_result_dup_future (self->selected_ui_entry);
      wr        = bz_track_weak (self);
      ui_future = dex_future_then (
          ui_future,
          (DexFutureCallback) on_selected_ui_entry_resolved,
          wr,
          bz_weak_release);
      self->selected_ui_future = g_steal_pointer (&ui_future);
    }

notify:
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_GROUP]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_UI_ENTRY]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PARENT_UI_ENTRY]);
}

static void
tile_activated_cb (BzAddonTile *tile)
{
  BzAddonsDialog *self  = NULL;
  BzEntryGroup   *group = NULL;

  self = BZ_ADDONS_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (tile), BZ_TYPE_ADDONS_DIALOG));
  if (self == NULL)
    return;

  group = bz_addon_tile_get_group (tile);
  if (group == NULL)
    return;

  set_selected_group (self, group);

  adw_navigation_view_push_by_tag (self->navigation_view, "full-view");
}

static int
sort_func (BzEntryGroup   *a,
           BzEntryGroup   *b,
           BzAddonsDialog *self)
{
  const char *desc_a = NULL;
  const char *desc_b = NULL;
  gboolean    has_a  = FALSE;
  gboolean    has_b  = FALSE;
  int         result = 0;

  desc_a = bz_entry_group_get_description (a);
  desc_b = bz_entry_group_get_description (b);
  has_a  = desc_a != NULL && *desc_a != '\0';
  has_b  = desc_b != NULL && *desc_b != '\0';
  if (has_a != has_b)
    result = has_b - has_a;
  else
    result = g_utf8_collate (bz_entry_group_get_title (a),
                             bz_entry_group_get_title (b));

  return result;
}
