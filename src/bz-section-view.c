/* bz-section-view.c
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

#include <bge.h>

#include "bz-application.h"
#include "bz-async-texture.h"
#include "bz-curated-app-tile.h"
#include "bz-curated-section.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "bz-rich-app-tile.h"
#include "bz-section-view.h"
#include "bz-window.h"

struct _BzSectionView
{
  AdwBin parent_instance;

  BzCuratedSection *section;
  GListModel       *classes;

  AdwStyleManager *style_manager;
  GListModel      *applied_classes;

  /* Template widgets */
  BgeMarkdownRender *markdown;
};

G_DEFINE_FINAL_TYPE (BzSectionView, bz_section_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_SECTION,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_GROUP_ACTIVATED,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
tile_clicked (BzEntryGroup *group,
              GtkButton    *button);

static void
dark_changed (BzSectionView   *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr);

static void
refresh_dark_light_classes (BzSectionView   *self,
                            AdwStyleManager *mgr);

static BzAsyncTexture *
choose_image (const char *default_variant_uri,
              const char *light_variant_uri,
              const char *dark_variant_uri);

static void
bz_section_view_dispose (GObject *object)
{
  BzSectionView *self = BZ_SECTION_VIEW (object);

  g_signal_handlers_disconnect_by_func (
      self->style_manager, dark_changed, self);

  g_clear_object (&self->section);
  g_clear_object (&self->classes);
  g_clear_object (&self->style_manager);
  g_clear_object (&self->applied_classes);

  G_OBJECT_CLASS (bz_section_view_parent_class)->dispose (object);
}

static void
bz_section_view_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzSectionView *self = BZ_SECTION_VIEW (object);

  switch (prop_id)
    {
    case PROP_SECTION:
      g_value_set_object (value, bz_section_view_get_section (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_section_view_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzSectionView *self = BZ_SECTION_VIEW (object);

  switch (prop_id)
    {
    case PROP_SECTION:
      bz_section_view_set_section (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

// static BzAsyncTexture *
// get_banner (gpointer               object,
//             BzCuratedCategoryInfo *info)
// {
//   const char *banner       = NULL;
//   const char *light_banner = NULL;
//   const char *dark_banner  = NULL;
//
//   if (!BZ_IS_CURATED_CATEGORY_INFO (info))
//     return NULL;
//
//   banner       = bz_curated_category_info_get_banner (info);
//   light_banner = bz_curated_category_info_get_light_banner (info);
//   dark_banner  = bz_curated_category_info_get_dark_banner (info);
//
//   return choose_image (banner, light_banner, dark_banner);
// }

static BzAsyncTexture *
get_image (gpointer            object,
           BzCuratedImageInfo *info)
{
  const char *image       = NULL;
  const char *light_image = NULL;
  const char *dark_image  = NULL;

  if (!BZ_IS_CURATED_IMAGE_INFO (info))
    return NULL;

  image       = bz_curated_image_info_get_uri (info);
  light_image = bz_curated_image_info_get_light_uri (info);
  dark_image  = bz_curated_image_info_get_dark_uri (info);

  return choose_image (image, light_image, dark_image);
}

static int
clamp_banner_height (gpointer object,
                     int      value)
{
  if (value == 0)
    return 300;
  return CLAMP (value, 100, 1000);
}

static int
clamp_image_dimension (gpointer object,
                       int      value)
{
  if (value == 0)
    return 200;
  return CLAMP (value, 100, 1000);
}

static GListModel *
convert_to_groups (gpointer    object,
                   GListModel *value)
{
  BzStateInfo             *info    = NULL;
  BzApplicationMapFactory *factory = NULL;

  info    = bz_state_info_get_default ();
  factory = bz_state_info_get_application_factory (info);

  return bz_application_map_factory_generate (factory, value);
}

static void
bind_widget_cb (BzSectionView     *self,
                BzCuratedAppTile  *tile,
                BzEntryGroup      *group,
                BzDynamicListView *view)
{
  g_signal_connect_swapped (tile, "clicked", G_CALLBACK (tile_clicked), group);
}

static void
unbind_widget_cb (BzSectionView     *self,
                  BzCuratedAppTile  *tile,
                  BzEntryGroup      *group,
                  BzDynamicListView *view)
{
  g_signal_handlers_disconnect_by_func (tile, G_CALLBACK (tile_clicked), group);
}

static void
install_all_clicked (BzSectionView *self,
                     GtkButton     *button)
{
  GtkWidget               *window   = NULL;
  BzCuratedAppidsInfo     *appids   = NULL;
  GListModel              *list     = NULL;
  guint                    n_appids = 0;
  BzStateInfo             *info     = NULL;
  BzApplicationMapFactory *factory  = NULL;
  g_autoptr (GListModel) groups     = NULL;

  window = gtk_widget_get_ancestor (GTK_WIDGET (self), BZ_TYPE_WINDOW);
  if (window == NULL)
    return;

  /* If the button is visible and the user clicked it, this must be non-null */
  appids = bz_curated_section_get_appids (self->section);
  list   = bz_curated_appids_info_get_list (appids);
  if (list == NULL)
    return;
  n_appids = g_list_model_get_n_items (list);
  if (n_appids == 0)
    return;

  /* TODO: bind state via object properties */
  info    = bz_state_info_get_default ();
  factory = bz_state_info_get_application_factory (info);

  groups = bz_application_map_factory_generate (factory, list);
  /* TODO: use signals to chain up the blueprints; it is cleaner, but more
     work... :( */
  bz_window_bulk_install (BZ_WINDOW (window), groups);
}

static GtkWidget *
markdown_bind_inline_uri (BzSectionView     *self,
                          const char        *title,
                          const char        *src,
                          BgeMarkdownRender *markdown)
{
  if (src == NULL)
    return NULL;

  if (g_str_has_prefix (src, "appstream://"))
    {
      BzStateInfo             *info    = NULL;
      BzApplicationMapFactory *factory = NULL;
      g_autoptr (BzEntryGroup) group   = NULL;

      info    = bz_state_info_get_default ();
      factory = bz_state_info_get_application_factory (info);

      group = bz_application_map_factory_convert_one (
          factory,
          gtk_string_object_new (src + strlen ("appstream://")));
      if (group != NULL)
        {
          GtkWidget *tile = NULL;

          tile = bz_rich_app_tile_new ();
          bz_rich_app_tile_set_group (BZ_RICH_APP_TILE (tile), group);

          return tile;
        }
    }
  else
    {
      g_autoptr (GFile) file = NULL;

      file = g_file_new_for_uri (src);
      if (file != NULL)
        {
          g_autoptr (BzAsyncTexture) texture = NULL;
          GtkWidget *picture                 = NULL;

          texture = bz_async_texture_new_lazy (file, NULL);
          picture = gtk_picture_new ();
          gtk_picture_set_paintable (GTK_PICTURE (picture), GDK_PAINTABLE (texture));

          gtk_widget_set_hexpand (picture, TRUE);
          gtk_widget_set_size_request (picture, -1, 100);

          return picture;
        }
    }

  return NULL;
}

static void
bz_section_view_class_init (BzSectionViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_section_view_dispose;
  object_class->get_property = bz_section_view_get_property;
  object_class->set_property = bz_section_view_set_property;

  props[PROP_SECTION] =
      g_param_spec_object (
          "section",
          NULL, NULL,
          BZ_TYPE_CURATED_SECTION,
          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_GROUP_ACTIVATED] =
      g_signal_new (
          "group-activated",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__OBJECT,
          G_TYPE_NONE, 1,
          BZ_TYPE_ENTRY_GROUP);
  g_signal_set_va_marshaller (
      signals[SIGNAL_GROUP_ACTIVATED],
      G_TYPE_FROM_CLASS (klass),
      g_cclosure_marshal_VOID__OBJECTv);

  g_type_ensure (BZ_TYPE_CURATED_APP_TILE);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_ASYNC_TEXTURE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-section-view.ui");
  gtk_widget_class_bind_template_child (widget_class, BzSectionView, markdown);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  // gtk_widget_class_bind_template_callback (widget_class, get_banner);
  gtk_widget_class_bind_template_callback (widget_class, get_image);
  gtk_widget_class_bind_template_callback (widget_class, clamp_banner_height);
  gtk_widget_class_bind_template_callback (widget_class, clamp_image_dimension);
  gtk_widget_class_bind_template_callback (widget_class, convert_to_groups);
  gtk_widget_class_bind_template_callback (widget_class, bind_widget_cb);
  gtk_widget_class_bind_template_callback (widget_class, unbind_widget_cb);
  gtk_widget_class_bind_template_callback (widget_class, install_all_clicked);
  gtk_widget_class_bind_template_callback (widget_class, markdown_bind_inline_uri);
}

static void
dark_changed (BzSectionView   *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr)
{
  refresh_dark_light_classes (self, mgr);

  if (self->section != NULL)
    {
      g_object_notify (G_OBJECT (self->section), "category");
      g_object_notify (G_OBJECT (self->section), "markdown");
      g_object_notify (G_OBJECT (self->section), "image");
    }
}

static void
bz_section_view_init (BzSectionView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->style_manager = g_object_ref (
      adw_style_manager_get_default ());
  g_signal_connect_swapped (
      self->style_manager,
      "notify::dark",
      G_CALLBACK (dark_changed),
      self);
  g_object_bind_property (
      self->style_manager, "dark",
      self->markdown, "dark",
      G_BINDING_SYNC_CREATE);
}

GtkWidget *
bz_section_view_new (BzCuratedSection *section)
{
  return g_object_new (
      BZ_TYPE_SECTION_VIEW,
      "section", section,
      NULL);
}

void
bz_section_view_set_section (BzSectionView    *self,
                             BzCuratedSection *section)
{
  g_return_if_fail (BZ_IS_SECTION_VIEW (self));
  g_return_if_fail (section == NULL || BZ_IS_CURATED_SECTION (section));

  g_clear_object (&self->section);

  if (self->classes != NULL)
    {
      guint n_classes = 0;

      n_classes = g_list_model_get_n_items (self->classes);
      for (guint i = 0; i < n_classes; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;
          const char *class                  = NULL;

          string = g_list_model_get_item (self->classes, i);
          class  = gtk_string_object_get_string (string);

          gtk_widget_remove_css_class (GTK_WIDGET (self), class);
        }
    }
  g_clear_object (&self->classes);

  if (section != NULL)
    {
      self->section = g_object_ref (section);
      g_object_get (section, "classes", &self->classes, NULL);

      if (self->classes != NULL)
        {
          guint n_classes = 0;

          n_classes = g_list_model_get_n_items (self->classes);
          for (guint i = 0; i < n_classes; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;
              const char *class                  = NULL;

              string = g_list_model_get_item (self->classes, i);
              class  = gtk_string_object_get_string (string);

              gtk_widget_add_css_class (GTK_WIDGET (self), class);
            }
        }

      refresh_dark_light_classes (self, NULL);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SECTION]);
}

BzCuratedSection *
bz_section_view_get_section (BzSectionView *self)
{
  g_return_val_if_fail (BZ_IS_SECTION_VIEW (self), NULL);
  return self->section;
}

static void
tile_clicked (BzEntryGroup *group,
              GtkButton    *button)
{
  GtkWidget *self = NULL;

  self = gtk_widget_get_ancestor (GTK_WIDGET (button), BZ_TYPE_SECTION_VIEW);
  g_signal_emit (self, signals[SIGNAL_GROUP_ACTIVATED], 0, group);
}

static void
refresh_dark_light_classes (BzSectionView   *self,
                            AdwStyleManager *mgr)
{
  if (self->applied_classes != NULL)
    {
      guint n_applied_classes = 0;

      n_applied_classes = g_list_model_get_n_items (self->applied_classes);
      for (guint i = 0; i < n_applied_classes; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;
          const char *class                  = NULL;

          string = g_list_model_get_item (self->applied_classes, i);
          class  = gtk_string_object_get_string (string);

          gtk_widget_remove_css_class (GTK_WIDGET (self), class);
        }
    }
  g_clear_object (&self->applied_classes);

  if (self->section == NULL)
    return;

  if (mgr == NULL)
    mgr = adw_style_manager_get_default ();

  if (adw_style_manager_get_dark (mgr))
    g_object_get (self->section, "dark-classes", &self->applied_classes, NULL);
  else
    g_object_get (self->section, "light-classes", &self->applied_classes, NULL);

  if (self->applied_classes != NULL)
    {
      guint n_classes = 0;

      n_classes = g_list_model_get_n_items (self->applied_classes);
      for (guint i = 0; i < n_classes; i++)
        {
          g_autoptr (GtkStringObject) string = NULL;
          const char *class                  = NULL;

          string = g_list_model_get_item (self->applied_classes, i);
          class  = gtk_string_object_get_string (string);

          gtk_widget_add_css_class (GTK_WIDGET (self), class);
        }
    }
}

static BzAsyncTexture *
choose_image (const char *default_variant_uri,
              const char *light_variant_uri,
              const char *dark_variant_uri)
{
  gboolean    is_dark                = FALSE;
  const char *uri                    = NULL;
  g_autoptr (GFile) source           = NULL;
  g_autoptr (GdkPaintable) paintable = NULL;

  is_dark = adw_style_manager_get_dark (adw_style_manager_get_default ());
  if (is_dark)
    uri = dark_variant_uri;
  else
    uri = light_variant_uri;
  if (uri == NULL)
    uri = default_variant_uri;
  if (uri == NULL)
    return NULL;

  source = g_file_new_for_uri (uri);
  return bz_async_texture_new_lazy (source, NULL);
}
