/* bz-full-view.c
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

#define G_LOG_DOMAIN "BAZAAR::FULL-VIEW-WIDGET"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

#include "bz-age-rating-dialog.h"
#include "bz-app-size-dialog.h"
#include "bz-app-tile.h"
#include "bz-apps-page.h"
#include "bz-appstream-description-render.h"
#include "bz-context-tile-callbacks.h"
#include "bz-context-tile.h"
#include "bz-developer-badge.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-inspector.h"
#include "bz-error.h"
#include "bz-fading-clamp.h"
#include "bz-favorite-button.h"
#include "bz-flatpak-entry.h"
#include "bz-full-view.h"
#include "bz-gnome-extension-entry.h"
#include "bz-hardware-support-dialog.h"
#include "bz-install-controls.h"
#include "bz-license-dialog.h"
#include "bz-metainfo-preview.h"
#include "bz-releases-list.h"
#include "bz-safety-calculator.h"
#include "bz-safety-dialog.h"
#include "bz-screenshot-page.h"
#include "bz-screenshots-carousel.h"
#include "bz-section-view.h"
#include "bz-share-list.h"
#include "bz-spdx.h"
#include "bz-state-info.h"
#include "bz-stats-dialog.h"
#include "bz-tag-list.h"
#include "bz-template-callbacks.h"
#include "bz-util.h"
#include "bz-window.h"

struct _BzFullView
{
  AdwBin parent_instance;

  BzStateInfo          *state;
  BzTransactionManager *transactions;
  BzEntryGroup         *group;
  DexFuture            *ui_future;
  BzResult             *ui_entry;
  BzResult             *runtime;
  BzResult             *group_model;
  gboolean              show_sidebar;

  /* Template widgets */
  GtkScrolledWindow *main_scroll;
  AdwViewStack      *stack;
  GtkWidget         *shadow_overlay;
  GtkToggleButton   *description_toggle;
};

G_DEFINE_FINAL_TYPE (BzFullView, bz_full_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_STATE,
  PROP_ENTRY_GROUP,
  PROP_UI_ENTRY,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_UPDATE,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
bz_full_view_dispose (GObject *object)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  dex_clear (&self->ui_future);
  g_clear_object (&self->state);
  g_clear_object (&self->transactions);
  g_clear_object (&self->group);
  g_clear_object (&self->ui_entry);
  g_clear_object (&self->runtime);
  g_clear_object (&self->group_model);

  G_OBJECT_CLASS (bz_full_view_parent_class)->dispose (object);
}

static void
bz_full_view_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, self->state);
      break;
    case PROP_ENTRY_GROUP:
      g_value_set_object (value, bz_full_view_get_entry_group (self));
      break;
    case PROP_UI_ENTRY:
      g_value_set_object (value, self->ui_entry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_full_view_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzFullView *self = BZ_FULL_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_clear_object (&self->state);
      self->state = g_value_dup_object (value);
      break;
    case PROP_ENTRY_GROUP:
      bz_full_view_set_entry_group (self, g_value_get_object (value));
      break;
    case PROP_UI_ENTRY:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
entry_is_gnome_extension (gpointer object,
                          BzEntry *entry)
{
  if (entry == NULL)
    return FALSE;
  return BZ_IS_GNOME_EXTENSION_ENTRY (entry);
}

static gboolean
is_scrolled_down (gpointer object,
                  double   value)
{
  return value > 100.0;
}

static char *
format_as_link (gpointer    object,
                const char *value)
{
  if (value != NULL)
    return g_strdup_printf ("<a href=\"%s\" title=\"%s\">%s</a>",
                            value, value, value);
  else
    return g_strdup (_ ("No URL"));
}

static gboolean
has_link (gpointer    object,
          const char *license)
{
  if (license == NULL || *license == '\0')
    return FALSE;

  return bz_spdx_is_valid (license);
}

static char *
pick_license_warning (gpointer object,
                      gboolean value)
{
  return value
             ? g_strdup (_ ("This application has a FLOSS license, meaning the source code can be audited for safety."))
             : g_strdup (_ ("This application has a proprietary license, meaning the source code is developed privately and cannot be audited by an independent third party."));
}

static char *
format_other_apps_label (gpointer object, const char *developer)
{
  if (!developer || *developer == '\0')
    return g_strdup (_ ("More Apps"));
  return g_strdup_printf (_ ("More Apps by %s"), developer);
}

static char *
format_more_other_apps_label (gpointer object, const char *developer)
{
  if (!developer || *developer == '\0')
    return g_strdup (_ ("Other Apps by this Developer"));

  return g_strdup_printf (_ ("Other Apps by %s"), developer);
}

static char *
format_leftover_label (gpointer object, const char *name, guint64 size)
{
  g_autofree char *formatted_size = NULL;

  formatted_size = g_format_size (size);
  return g_strdup_printf (_ ("%s is not installed, but it still has <b>%s</b> of data present."), name, formatted_size);
}

static gpointer
filter_own_app_id (BzEntry *entry, GtkStringList *app_ids)
{
  const char *own_id;
  g_autoptr (GtkStringList) filtered = NULL;
  guint n_items                      = 0;

  if (!BZ_IS_ENTRY (entry) || !GTK_IS_STRING_LIST (app_ids))
    return NULL;

  own_id = bz_entry_get_id (entry);
  if (!own_id)
    return NULL;

  filtered = gtk_string_list_new (NULL);
  n_items  = g_list_model_get_n_items (G_LIST_MODEL (app_ids));

  for (guint i = 0; i < n_items; i++)
    {
      const char *id = NULL;

      id = gtk_string_list_get_string (app_ids, i);
      if (g_strcmp0 (id, own_id) != 0)
        gtk_string_list_append (filtered, id);
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (filtered)) > 0)
    return g_steal_pointer (&filtered);
  else
    return NULL;
}

static GListModel *
get_developer_apps_entries (gpointer object, GtkStringList *app_ids, BzEntry *entry)
{
  BzFullView *self                   = BZ_FULL_VIEW (object);
  g_autoptr (GtkStringList) filtered = filter_own_app_id (BZ_ENTRY (entry), app_ids);
  BzApplicationMapFactory *factory;

  if (!filtered)
    return NULL;

  factory = bz_state_info_get_application_factory (self->state);
  if (!factory)
    return NULL;

  return bz_application_map_factory_generate (factory, G_LIST_MODEL (filtered));
}

static int
get_dev_apps_max_children_per_line (gpointer object, GListModel *model)
{
  if (!model)
    return 3;
  return g_list_model_get_n_items (model) > 2 ? 3 : 2;
}

static void
more_apps_button_clicked_cb (BzFullView *self,
                             GtkButton  *button)
{
  g_autoptr (GListModel) model = NULL;
  guint              n_items;
  g_autofree char   *title       = NULL;
  g_autofree char   *subtitle    = NULL;
  AdwNavigationPage *apps_page   = NULL;
  GtkWidget         *nav_view    = NULL;
  g_autoptr (GListModel) app_ids = NULL;
  BzEntry    *entry              = NULL;
  const char *developer          = NULL;

  g_return_if_fail (BZ_IS_FULL_VIEW (self));
  g_return_if_fail (GTK_IS_BUTTON (button));

  entry = bz_result_get_object (self->ui_entry);
  if (entry == NULL)
    return;

  g_object_get (entry, "developer-apps", &app_ids, NULL);

  model = bz_application_map_factory_generate (
      bz_state_info_get_application_factory (self->state),
      app_ids);

  n_items = g_list_model_get_n_items (model);

  developer = bz_entry_get_developer (entry);
  if (developer != NULL && *developer != '\0')
    title = g_strdup_printf (_ ("Other Apps by %s"), developer);
  else
    title = g_strdup (_ ("Other Apps"));

  subtitle = g_strdup_printf (ngettext ("%d Application", "%d Applications", n_items), n_items);

  apps_page = bz_apps_page_new (title, model);
  bz_apps_page_set_subtitle (BZ_APPS_PAGE (apps_page), subtitle);

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  if (nav_view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), apps_page);
}

static void
app_tile_clicked_cb (BzFullView *self,
                     BzAppTile  *tile)
{
  BzEntryGroup *group = bz_app_tile_get_group (tile);
  bz_full_view_set_entry_group (self, group);
}

static void
bind_app_tile_cb (BzFullView        *self,
                  BzAppTile         *tile,
                  BzEntryGroup      *group,
                  BzDynamicListView *view)
{
  g_signal_connect_swapped (tile, "clicked",
                            G_CALLBACK (app_tile_clicked_cb),
                            self);
}

static void
unbind_app_tile_cb (BzFullView        *self,
                    BzAppTile         *tile,
                    BzEntryGroup      *group,
                    BzDynamicListView *view)
{
  g_signal_handlers_disconnect_by_func (tile, G_CALLBACK (app_tile_clicked_cb), self);
}

static void
open_url_cb (BzFullView   *self,
             AdwActionRow *row)
{
  BzEntry    *entry = NULL;
  const char *url   = NULL;

  entry = BZ_ENTRY (bz_result_get_object (self->ui_entry));
  url   = bz_entry_get_url (entry);

  if (url != NULL && *url != '\0')
    g_app_info_launch_default_for_uri (url, NULL, NULL);
  else
    g_warning ("Invalid or empty URL provided for Flathub URL CB");
}

static void
license_cb (BzFullView *self,
            GtkButton  *button)
{
  AdwDialog *dialog   = NULL;
  BzEntry   *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  if (ui_entry == NULL)
    return;

  dialog = bz_license_dialog_new (ui_entry);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
age_rating_cb (BzFullView *self,
               GtkButton  *button)
{
  BzAgeRatingDialog *dialog   = NULL;
  BzEntry           *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  if (ui_entry == NULL)
    return;

  dialog = bz_age_rating_dialog_new (ui_entry);
  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
dl_stats_cb (BzFullView *self,
             GtkButton  *button)
{
  AdwDialog        *dialog   = NULL;
  AdwBreakpointBin *bin      = NULL;
  BzEntry          *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);

  bin    = bz_stats_dialog_new (NULL, NULL, 0);
  dialog = adw_dialog_new ();
  adw_dialog_set_content_width (dialog, 1250);
  adw_dialog_set_content_height (dialog, 750);
  adw_dialog_set_child (dialog, GTK_WIDGET (bin));

  g_object_bind_property (ui_entry, "download-stats", bin, "model", G_BINDING_SYNC_CREATE);
  g_object_bind_property (ui_entry, "download-stats-per-country", bin, "country-model", G_BINDING_SYNC_CREATE);
  g_object_bind_property (ui_entry, "total-downloads", bin, "total-downloads", G_BINDING_SYNC_CREATE);

  adw_dialog_present (dialog, GTK_WIDGET (self));
  bz_stats_dialog_animate_open (BZ_STATS_DIALOG (bin));
}

static void
screenshot_clicked_cb (BzFullView            *self,
                       guint                  index,
                       BzScreenshotsCarousel *carousel)
{
  GListModel        *screenshots = NULL;
  GListModel        *captions    = NULL;
  AdwNavigationPage *page        = NULL;
  GtkWidget         *nav_view    = NULL;
  BzEntry           *entry       = NULL;

  screenshots = bz_screenshots_carousel_get_model (carousel);
  if (screenshots == NULL)
    return;

  if (self->ui_entry != NULL)
    {
      entry = bz_result_get_object (self->ui_entry);
      if (entry != NULL)
        g_object_get (entry, "screenshot-captions", &captions, NULL);
    }

  page = bz_screenshot_page_new (screenshots, captions, index);

  g_clear_object (&captions);

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  if (nav_view != NULL)
    adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), page);
}

static void
size_cb (BzFullView *self,
         GtkButton  *button)
{
  AdwDialog *size_dialog = NULL;

  if (self->group == NULL)
    return;

  size_dialog = bz_app_size_dialog_new (self->group);
  adw_dialog_present (size_dialog, GTK_WIDGET (self));
}

static void
formfactor_cb (BzFullView *self,
               GtkButton  *button)
{
  AdwDialog *dialog   = NULL;
  BzEntry   *ui_entry = NULL;

  if (self->group == NULL)
    return;

  ui_entry = bz_result_get_object (self->ui_entry);
  dialog   = ADW_DIALOG (bz_hardware_support_dialog_new (ui_entry));

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
safety_cb (BzFullView *self,
           GtkButton  *button)
{
  AdwDialog *dialog = NULL;

  if (self->group == NULL)
    return;

  dialog = ADW_DIALOG (bz_safety_dialog_new (self->group));

  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static void
update_cb (BzFullView        *self,
           GListModel        *entries,
           BzInstallControls *controls)
{
  g_signal_emit (self, signals[SIGNAL_UPDATE], 0, entries);
}

static void
delete_user_data_cb (BzFullView *self,
                     GtkButton  *button)
{
  g_return_if_fail (BZ_IS_FULL_VIEW (self));

  if (self->group == NULL)
    return;

  bz_entry_group_reap_user_data (self->group);
}

static void
support_cb (BzFullView *self,
            GtkButton  *button)
{
  BzEntry *entry = NULL;

  entry = bz_result_get_object (self->ui_entry);
  if (entry != NULL)
    {
      const char *url = NULL;

      url = bz_entry_get_donation_url (entry);
      g_app_info_launch_default_for_uri (url, NULL, NULL);
    }
}

static void
install_addons_cb (BzFullView *self,
                   GtkButton  *button)
{
  if (self->group == NULL)
    return;

  gtk_widget_activate_action (GTK_WIDGET (self), "window.addons-group", "s",
                              bz_entry_group_get_id (self->group));
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

static gboolean
metainfo_banner_visible (gpointer    object,
                         const char *remote_name)
{
  return g_strcmp0 (remote_name, "local-preview") == 0;
}

static void
preview_other_metainfo_cb (BzFullView *self,
                           AdwBanner  *banner)
{
  GtkWidget *window = NULL;

  window = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (self)));
  bz_window_push_page (BZ_WINDOW (window),
                       create_entry_group_preview_page (self->group));
}

static void
copy_id_cb (BzFullView *self,
            GtkButton  *button)
{
  const char   *id        = NULL;
  GdkClipboard *clipboard = NULL;

  if (self->group == NULL)
    return;
  id = bz_entry_group_get_id (self->group);

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());
  gdk_clipboard_set_text (clipboard, id);
}

static void
debug_id_inspect_cb (BzFullView *self,
                     GtkButton  *button)
{
  g_autofree char *unique_id         = NULL;
  g_autoptr (GtkStringObject) string = NULL;
  g_autoptr (BzResult) result        = NULL;

  if (self->group == NULL)
    return;
  unique_id = bz_entry_group_dup_ui_entry_id (self->group);

  result = bz_application_map_factory_convert_one (
      bz_state_info_get_entry_factory (self->state),
      gtk_string_object_new (unique_id));
  if (result != NULL)
    {
      BzEntryInspector *inspector = NULL;

      inspector = bz_entry_inspector_new ();
      bz_entry_inspector_set_result (inspector, result);

      gtk_window_present (GTK_WINDOW (inspector));
    }
}

static void
bz_full_view_class_init (BzFullViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_full_view_dispose;
  object_class->get_property = bz_full_view_get_property;
  object_class->set_property = bz_full_view_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_ENTRY_GROUP] =
      g_param_spec_object (
          "entry-group",
          NULL, NULL,
          BZ_TYPE_ENTRY_GROUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_UI_ENTRY] =
      g_param_spec_object (
          "ui-entry",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_UPDATE] =
      g_signal_new (
          "update",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE, 1,
          G_TYPE_LIST_MODEL);
  g_signal_set_va_marshaller (
      signals[SIGNAL_UPDATE],
      G_TYPE_FROM_CLASS (klass),
      g_cclosure_marshal_VOID__OBJECTv);

  g_type_ensure (BZ_TYPE_APPSTREAM_DESCRIPTION_RENDER);
  g_type_ensure (BZ_TYPE_DEVELOPER_BADGE);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_ENTRY);
  g_type_ensure (BZ_TYPE_ENTRY_GROUP);
  g_type_ensure (BZ_TYPE_FADING_CLAMP);
  g_type_ensure (BZ_TYPE_FAVORITE_BUTTON);
  g_type_ensure (BZ_TYPE_FLATPAK_ENTRY);
  g_type_ensure (BZ_TYPE_GNOME_EXTENSION_ENTRY);
  g_type_ensure (BZ_TYPE_HARDWARE_SUPPORT_DIALOG);
  g_type_ensure (BZ_TYPE_INSTALL_CONTROLS);
  g_type_ensure (BZ_TYPE_SECTION_VIEW);
  g_type_ensure (BZ_TYPE_RELEASES_LIST);
  g_type_ensure (BZ_TYPE_SCREENSHOTS_CAROUSEL);
  g_type_ensure (BZ_TYPE_SHARE_LIST);
  g_type_ensure (BZ_TYPE_TAG_LIST);
  g_type_ensure (BZ_TYPE_CONTEXT_TILE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-full-view.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  bz_widget_class_bind_all_context_tile_callbacks (widget_class);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, stack);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, main_scroll);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, shadow_overlay);
  gtk_widget_class_bind_template_child (widget_class, BzFullView, description_toggle);
  gtk_widget_class_bind_template_callback (widget_class, entry_is_gnome_extension);
  gtk_widget_class_bind_template_callback (widget_class, is_scrolled_down);
  gtk_widget_class_bind_template_callback (widget_class, age_rating_cb);
  gtk_widget_class_bind_template_callback (widget_class, format_as_link);
  gtk_widget_class_bind_template_callback (widget_class, has_link);
  gtk_widget_class_bind_template_callback (widget_class, format_leftover_label);
  gtk_widget_class_bind_template_callback (widget_class, format_other_apps_label);
  gtk_widget_class_bind_template_callback (widget_class, format_more_other_apps_label);
  gtk_widget_class_bind_template_callback (widget_class, get_developer_apps_entries);
  gtk_widget_class_bind_template_callback (widget_class, get_dev_apps_max_children_per_line);
  gtk_widget_class_bind_template_callback (widget_class, more_apps_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, open_url_cb);
  gtk_widget_class_bind_template_callback (widget_class, license_cb);
  gtk_widget_class_bind_template_callback (widget_class, dl_stats_cb);
  gtk_widget_class_bind_template_callback (widget_class, screenshot_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, size_cb);
  gtk_widget_class_bind_template_callback (widget_class, formfactor_cb);
  gtk_widget_class_bind_template_callback (widget_class, safety_cb);
  gtk_widget_class_bind_template_callback (widget_class, update_cb);
  gtk_widget_class_bind_template_callback (widget_class, delete_user_data_cb);
  gtk_widget_class_bind_template_callback (widget_class, support_cb);
  gtk_widget_class_bind_template_callback (widget_class, pick_license_warning);
  gtk_widget_class_bind_template_callback (widget_class, install_addons_cb);
  gtk_widget_class_bind_template_callback (widget_class, bind_app_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, unbind_app_tile_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_description_max_height);
  gtk_widget_class_bind_template_callback (widget_class, get_description_toggle_text);
  gtk_widget_class_bind_template_callback (widget_class, metainfo_banner_visible);
  gtk_widget_class_bind_template_callback (widget_class, preview_other_metainfo_cb);
  gtk_widget_class_bind_template_callback (widget_class, copy_id_cb);
  gtk_widget_class_bind_template_callback (widget_class, debug_id_inspect_cb);
}

static void
bz_full_view_init (BzFullView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_full_view_new (void)
{
  return g_object_new (BZ_TYPE_FULL_VIEW, NULL);
}

static DexFuture *
on_ui_entry_resolved (DexFuture *future,
                      GWeakRef  *wr)
{
  g_autoptr (BzFullView) self         = NULL;
  BzEntry *ui_entry                   = NULL;
  g_autoptr (BzResult) runtime_result = NULL;
  const GValue *value                 = NULL;

  bz_weak_get_or_return_reject (self, wr);

  value = dex_future_get_value (future, NULL);
  if (value != NULL && G_VALUE_HOLDS_OBJECT (value))
    {
      ui_entry = g_value_get_object (value);

      if (BZ_IS_FLATPAK_ENTRY (ui_entry))
        self->runtime = bz_flatpak_entry_dup_runtime_result (BZ_FLATPAK_ENTRY (ui_entry));
    }

  return dex_future_new_for_boolean (TRUE);
}

void
bz_full_view_set_entry_group (BzFullView   *self,
                              BzEntryGroup *group)
{
  g_return_if_fail (BZ_IS_FULL_VIEW (self));
  g_return_if_fail (group == NULL ||
                    BZ_IS_ENTRY_GROUP (group));

  if (group == self->group)
    return;

  dex_clear (&self->ui_future);
  g_clear_object (&self->group);
  g_clear_object (&self->ui_entry);
  g_clear_object (&self->runtime);
  g_clear_object (&self->group_model);
  gtk_toggle_button_set_active (self->description_toggle, FALSE);

  if (group != NULL)
    {
      self->group    = g_object_ref (group);
      self->ui_entry = bz_entry_group_dup_ui_entry (group);

      if (self->ui_entry != NULL && bz_result_get_resolved (self->ui_entry))
        {
          BzEntry *entry                      = NULL;
          g_autoptr (GListStore) store        = NULL;
          g_autoptr (DexFuture) future        = NULL;
          g_autoptr (DexFuture) object_future = NULL;
          GWeakRef *wr                        = NULL;

          entry = bz_result_get_object (self->ui_entry);
          store = g_list_store_new (BZ_TYPE_ENTRY);
          g_list_store_append (store, entry);

          future            = dex_future_new_for_object (store);
          self->group_model = bz_result_new (future);

          object_future = dex_future_new_for_object (entry);
          wr            = bz_track_weak (self);
          dex_unref (on_ui_entry_resolved (object_future, wr));
          bz_weak_release (wr);
        }
      else
        {
          g_autoptr (DexFuture) future = NULL;

          future            = bz_entry_group_dup_all_into_store (group);
          self->group_model = bz_result_new (future);

          if (self->ui_entry != NULL)
            {
              g_autoptr (DexFuture) ui_future = NULL;

              ui_future = bz_result_dup_future (self->ui_entry);
              ui_future = dex_future_then (
                  ui_future,
                  (DexFutureCallback) on_ui_entry_resolved,
                  bz_track_weak (self),
                  bz_weak_release);
              self->ui_future = g_steal_pointer (&ui_future);
            }
        }

      adw_view_stack_set_visible_child_name (self->stack, "content");
    }
  else
    adw_view_stack_set_visible_child_name (self->stack, "empty");

  gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (self->main_scroll)), 0.0);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY_GROUP]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_UI_ENTRY]);
}

BzEntryGroup *
bz_full_view_get_entry_group (BzFullView *self)
{
  g_return_val_if_fail (BZ_IS_FULL_VIEW (self), NULL);
  return self->group;
}
