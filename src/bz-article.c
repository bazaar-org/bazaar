/* bz-article.c
 *
 * Copyright 2026 Alexander Vanhee
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

#include <bge.h>
#include <libdex.h>

#include "bz-app-tile.h"
#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-article.h"
#include "bz-aspect-picture.h"
#include "bz-async-texture.h"
#include "bz-dynamic-list-view.h"
#include "env.h"
#include "global-net.h"
#include "io.h"
#include "bz-rich-app-tile.h"
#include "bz-screenshot-page.h"
#include "bz-state-info.h"
#include "util.h"
#include "bz-window.h"

struct _BzArticle
{
  AdwNavigationPage parent_instance;

  BzCuratedArticle *article;
  GCancellable     *cancellable;

  AdwStyleManager *style_manager;

  GListStore *screenshot_textures;
  GListStore *screenshot_captions;

  /* Template widgets */
  AdwViewStack      *stack;
  BgeMarkdownRender *render;
};

G_DEFINE_FINAL_TYPE (BzArticle, bz_article, ADW_TYPE_NAVIGATION_PAGE)

enum
{
  PROP_0,

  PROP_ARTICLE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
dark_changed (BzArticle       *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr);

static void
load_markdown (BzArticle *self);

static void
load_markdown_then (DexFuture *future,
                    BzArticle *self);

static GtkWidget *
markdown_bind_inline_uri (BzArticle         *self,
                          const char        *title,
                          const char        *src,
                          BgeMarkdownRender *markdown);

static gboolean
is_scrolled_down (gpointer object,
                  double   value);

static GtkWidget *
build_appstream_wrap_box (const char *remainder);

static void
screenshot_clicked (GtkButton *button,
                    gpointer   user_data);

static GtkWidget *
build_screenshot_box (BzArticle  *self,
                      const char *title,
                      GFile      *file);

static void
hook_tile_bind_widget (BzDynamicListView *list_view,
                       GtkWidget         *widget,
                       GObject           *object,
                       gpointer           user_data);

static BzEntryGroup *
parse_hook_group (const char *item);

static void
bz_article_dispose (GObject *object)
{
  BzArticle *self = BZ_ARTICLE (object);

  g_signal_handlers_disconnect_by_func (
      self->style_manager, dark_changed, self);

  if (self->cancellable != NULL)
    {
      g_cancellable_cancel (self->cancellable);
      g_clear_object (&self->cancellable);
    }

  g_clear_object (&self->article);
  g_clear_object (&self->style_manager);
  g_clear_object (&self->screenshot_textures);
  g_clear_object (&self->screenshot_captions);

  G_OBJECT_CLASS (bz_article_parent_class)->dispose (object);
}

static void
bz_article_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BzArticle *self = BZ_ARTICLE (object);

  switch (prop_id)
    {
    case PROP_ARTICLE:
      g_value_set_object (value, self->article);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_article_class_init (BzArticleClass *klass)
{
  GObjectClass   *object_class = NULL;
  GtkWidgetClass *widget_class = NULL;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_article_dispose;
  object_class->get_property = bz_article_get_property;

  props[PROP_ARTICLE] =
      g_param_spec_object (
          "article",
          NULL, NULL,
          BZ_TYPE_CURATED_ARTICLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-article.ui");
  gtk_widget_class_bind_template_child (widget_class, BzArticle, stack);
  gtk_widget_class_bind_template_child (widget_class, BzArticle, render);
  gtk_widget_class_bind_template_callback (widget_class, markdown_bind_inline_uri);
  gtk_widget_class_bind_template_callback (widget_class, is_scrolled_down);
}

static void
bz_article_init (BzArticle *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->style_manager = g_object_ref (
      adw_style_manager_get_default ());
  g_signal_connect_swapped (
      self->style_manager,
      "notify::dark",
      G_CALLBACK (dark_changed),
      self);

  bge_markdown_render_set_dark (
      self->render,
      adw_style_manager_get_dark (self->style_manager));

  self->screenshot_textures = g_list_store_new (BZ_TYPE_ASYNC_TEXTURE);
  self->screenshot_captions = g_list_store_new (GTK_TYPE_STRING_OBJECT);
}

AdwNavigationPage *
bz_article_new (BzCuratedArticle *article)
{
  BzArticle  *self  = NULL;
  const char *title = NULL;

  g_return_val_if_fail (article == NULL || BZ_IS_CURATED_ARTICLE (article), NULL);

  self = g_object_new (BZ_TYPE_ARTICLE, NULL);

  if (article != NULL)
    {
      self->article = g_object_ref (article);

      title = bz_curated_article_get_title (article);
      adw_navigation_page_set_title (ADW_NAVIGATION_PAGE (self), title != NULL ? title : "");

      load_markdown (self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ARTICLE]);
  return ADW_NAVIGATION_PAGE (self);
}

static void
dark_changed (BzArticle       *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr)
{
  bge_markdown_render_set_dark (self->render, adw_style_manager_get_dark (mgr));
}

static void
load_markdown (BzArticle *self)
{
  const char *uri              = NULL;
  g_autoptr (DexFuture) future = NULL;

  if (self->article == NULL)
    return;

  uri = bz_curated_article_get_uri (self->article);
  if (uri == NULL)
    {
      adw_view_stack_set_visible_child_name (self->stack, "error");
      return;
    }

  adw_view_stack_set_visible_child_name (self->stack, "loading");

  self->cancellable = g_cancellable_new ();

  future = bz_fetch_uri_contents (uri);
  future = dex_future_then (
      future,
      (DexFutureCallback) load_markdown_then,
      g_object_ref (self), g_object_unref);
  dex_future_disown (g_steal_pointer (&future));
}

static void
load_markdown_then (DexFuture *future,
                    BzArticle *self)
{
  g_autoptr (GError) local_error = NULL;
  const GValue *value            = NULL;
  g_autoptr (GBytes) bytes       = NULL;
  gconstpointer data             = NULL;
  gsize         size             = 0;

  if (g_cancellable_is_cancelled (self->cancellable))
    return;

  value = dex_future_get_value (future, &local_error);
  if (value == NULL)
    {
      g_warning ("Failed to load article markdown: %s",
                 local_error != NULL ? local_error->message : "unknown error");
      adw_view_stack_set_visible_child_name (self->stack, "error");
      return;
    }

  bytes = g_value_dup_boxed (value);
  data  = g_bytes_get_data (bytes, &size);

  bge_markdown_render_set_markdown (self->render, (const char *) data);
  adw_view_stack_set_visible_child_name (self->stack, "content");
}

static gboolean
is_scrolled_down (gpointer object,
                  double   value)
{
  return value > 50.0;
}

static void
hook_tile_bind_widget (BzDynamicListView *list_view,
                       GtkWidget         *widget,
                       GObject           *object,
                       gpointer           user_data)
{
  BzEntryGroup *group    = BZ_ENTRY_GROUP (object);
  const char   *icon_uri = NULL;

  icon_uri = g_object_get_data (object, "bz-hook-icon-uri");

  if (g_object_get_data (object, "bz-hook-app") != NULL)
    {
      gtk_actionable_set_action_name (GTK_ACTIONABLE (widget), "window.activate-hook-app");
      gtk_actionable_set_action_target (GTK_ACTIONABLE (widget), "s", bz_entry_group_get_id (group));

      if (icon_uri != NULL)
        {
          g_autoptr (GFile) icon_file        = NULL;
          g_autoptr (BzAsyncTexture) texture = NULL;

          icon_file = g_file_new_for_uri (icon_uri);
          texture   = bz_async_texture_new_lazy (icon_file, NULL);

          bz_app_tile_set_icon_override (BZ_APP_TILE (widget), GDK_PAINTABLE (texture));
        }
    }
}

static BzEntryGroup *
parse_hook_group (const char *item)
{
  g_autoptr (GUri) uri           = NULL;
  g_autoptr (GHashTable) params  = NULL;
  g_autoptr (GError) local_error = NULL;
  const char   *id               = NULL;
  const char   *title            = NULL;
  const char   *subtitle         = NULL;
  const char   *icon_uri         = NULL;
  BzEntryGroup *group            = NULL;

  uri = g_uri_parse (item, G_URI_FLAGS_NONE, &local_error);
  if (uri == NULL)
    return NULL;

  params = g_uri_parse_params (
      g_uri_get_query (uri), -1, "&", G_URI_PARAMS_NONE, &local_error);
  if (params == NULL)
    return NULL;

  id       = g_hash_table_lookup (params, "id");
  title    = g_hash_table_lookup (params, "title");
  subtitle = g_hash_table_lookup (params, "subtitle");
  icon_uri = g_hash_table_lookup (params, "icon");

  group = bz_entry_group_new_manual (id, title, subtitle);

  if (group != NULL)
    {
      g_object_set_data (G_OBJECT (group), "bz-hook-app", GINT_TO_POINTER (TRUE));

      if (icon_uri != NULL)
        g_object_set_data_full (G_OBJECT (group), "bz-hook-icon-uri", g_strdup (icon_uri), g_free);
    }

  return group;
}

static GtkWidget *
build_appstream_wrap_box (const char *remainder)
{
  g_auto (GStrv) items          = NULL;
  guint n_items                 = 0;
  g_autoptr (GListStore) groups = NULL;
  GtkWidget *list_view          = NULL;

  items   = g_strsplit (remainder, ",", -1);
  n_items = g_strv_length (items);
  groups  = g_list_store_new (BZ_TYPE_ENTRY_GROUP);

  for (guint i = 0; i < n_items; i++)
    {
      const char *item = items[i];

      if (g_str_has_prefix (item, "appstream://"))
        {
          g_autoptr (GtkStringObject) string = NULL;
          g_autoptr (BzEntryGroup) group     = NULL;

          string = gtk_string_object_new (item + strlen ("appstream://"));
          group  = bz_application_map_factory_convert_one (
              bz_state_info_get_application_factory (bz_state_info_get_default ()),
              g_steal_pointer (&string));
          if (group != NULL)
            g_list_store_append (groups, group);
        }
      else if (g_str_has_prefix (item, "bazaar-hook://"))
        {
          g_autoptr (BzEntryGroup) group = NULL;
          group                          = parse_hook_group (item);

          if (group != NULL)
            g_list_store_append (groups, group);
        }
    }

  list_view = GTK_WIDGET (bz_dynamic_list_view_new ());
  gtk_widget_set_hexpand (list_view, TRUE);

  g_signal_connect (list_view, "bind-widget", G_CALLBACK (hook_tile_bind_widget), NULL);

  g_object_set (
      list_view,
      "scroll", FALSE,
      "noscroll-kind", BZ_DYNAMIC_LIST_VIEW_KIND_FLOW_BOX,
      "child-type", "BzAppTile",
      "child-prop", "group",
      "column-spacing", 12,
      "row-spacing", 12,
      "model", groups,
      NULL);

  if (g_list_model_get_n_items (G_LIST_MODEL (groups)) == 1)
    {
      GtkWidget *clamp = NULL;

      clamp = GTK_WIDGET (adw_clamp_new ());
      g_object_set (
          clamp,
          "maximum-size", 375,
          "hexpand", TRUE,
          "margin-top", 12,
          "margin-bottom", 12,
          "child", list_view,
          NULL);

      return clamp;
    }

  gtk_widget_set_margin_top (list_view, 12);
  gtk_widget_set_margin_bottom (list_view, 12);

  return list_view;
}

static void
screenshot_clicked (GtkButton *button,
                    gpointer   user_data)
{
  BzArticle        *self  = user_data;
  guint             index = 0;
  GtkWidget        *root  = NULL;
  BzScreenshotPage *page  = NULL;

  index = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "bz-screenshot-index"));

  root = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (button)));
  if (!BZ_IS_WINDOW (root))
    return;

  page = BZ_SCREENSHOT_PAGE (bz_screenshot_page_new (
      G_LIST_MODEL (self->screenshot_textures),
      G_LIST_MODEL (self->screenshot_captions),
      index,
      GTK_WIDGET (button)));

  bz_window_open_screenshot_page (BZ_WINDOW (root), page);
}

static GtkWidget *
build_screenshot_box (BzArticle  *self,
                      const char *title,
                      GFile      *file)
{
  g_autoptr (BzAsyncTexture) texture = NULL;
  GtkWidget *picture                 = NULL;
  GtkWidget *box                     = NULL;
  GtkWidget *button                  = NULL;
  guint      index                   = 0;

  texture = bz_async_texture_new_lazy (file, NULL);
  picture = bz_aspect_picture_new ();
  bz_aspect_picture_set_paintable (BZ_ASPECT_PICTURE (picture), GDK_PAINTABLE (texture));
  bz_aspect_picture_set_ratio (BZ_ASPECT_PICTURE (picture), 0.0);

  gtk_widget_set_hexpand (picture, TRUE);
  gtk_widget_set_halign (picture, GTK_ALIGN_FILL);

  if (title != NULL)
    gtk_accessible_update_property (GTK_ACCESSIBLE (picture),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, title, -1);

  index = g_list_model_get_n_items (G_LIST_MODEL (self->screenshot_textures));
  g_list_store_append (self->screenshot_textures, texture);
  {
    g_autoptr (GtkStringObject) caption_obj = NULL;

    caption_obj = gtk_string_object_new (title != NULL ? title : "");
    g_list_store_append (self->screenshot_captions, caption_obj);
  }

  button = gtk_button_new ();
  gtk_widget_add_css_class (button, "article-image-button");
  gtk_widget_set_hexpand (button, TRUE);
  gtk_widget_set_halign (button, GTK_ALIGN_FILL);
  gtk_button_set_child (GTK_BUTTON (button), picture);
  g_object_set_data (G_OBJECT (button), "bz-screenshot-index", GUINT_TO_POINTER (index));
  g_signal_connect (button, "clicked", G_CALLBACK (screenshot_clicked), self);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (box, 15);
  gtk_widget_set_margin_bottom (box, 15);
  gtk_box_append (GTK_BOX (box), button);

  if (title != NULL)
    {
      GtkWidget *caption = NULL;

      caption = gtk_label_new (title);
      gtk_widget_add_css_class (caption, "caption");
      gtk_widget_add_css_class (caption, "dimmed");
      gtk_label_set_xalign (GTK_LABEL (caption), 0.5);
      gtk_box_append (GTK_BOX (box), caption);
    }

  return box;
}

static GtkWidget *
markdown_bind_inline_uri (BzArticle         *self,
                          const char        *title,
                          const char        *src,
                          BgeMarkdownRender *markdown)
{
  g_autoptr (GFile) file = NULL;

  if (src == NULL)
    return NULL;

  if (g_str_has_prefix (src, "appstream://") || g_str_has_prefix (src, "bazaar-hook://"))
    return build_appstream_wrap_box (src);

  file = g_file_new_for_uri (src);
  if (file != NULL)
    return build_screenshot_box (self, title, file);

  return NULL;
}
