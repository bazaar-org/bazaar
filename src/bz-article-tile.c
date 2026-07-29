/* bz-article-tile.c
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

#define BAZAAR_MODULE "article-tile"

#include "config.h"

#include "bz-article-list-view.h"
#include "bz-article-tile.h"
#include "bz-article.h"
#include "bz-aspect-picture.h"
#include "bz-async-texture.h"
#include "bz-curated-article.h"
#include "bz-curated-articles-info.h"
#include "io.h"

struct _BzArticleTile
{
  BzListTile parent_instance;

  BzCuratedArticle *article;
  float             aspect_ratio;

  GBinding *aspect_ratio_binding;

  GtkWidget *picture_box;
  GtkWidget *picture;
};

G_DEFINE_FINAL_TYPE (BzArticleTile, bz_article_tile, BZ_TYPE_LIST_TILE);

enum
{
  PROP_0,
  PROP_ARTICLE,
  PROP_ASPECT_RATIO,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_article_tile_dispose (GObject *object)
{
  BzArticleTile *self = BZ_ARTICLE_TILE (object);

  g_clear_object (&self->article);
  g_clear_object (&self->aspect_ratio_binding);

  G_OBJECT_CLASS (bz_article_tile_parent_class)->dispose (object);
}

static void
bz_article_tile_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzArticleTile *self = BZ_ARTICLE_TILE (object);
  switch (prop_id)
    {
    case PROP_ARTICLE:
      g_value_set_object (value, bz_article_tile_get_article (self));
      break;
    case PROP_ASPECT_RATIO:
      g_value_set_float (value, self->aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_article_tile_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzArticleTile *self = BZ_ARTICLE_TILE (object);
  switch (prop_id)
    {
    case PROP_ARTICLE:
      bz_article_tile_set_article (self, g_value_get_object (value));
      break;
    case PROP_ASPECT_RATIO:
      self->aspect_ratio = g_value_get_float (value);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ASPECT_RATIO]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
tile_clicked_cb (GtkButton     *button,
                 BzArticleTile *self)
{
  GtkWidget         *nav_view = NULL;
  AdwNavigationPage *page     = NULL;

  if (self->article == NULL)
    return;

  nav_view = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_NAVIGATION_VIEW);
  if (nav_view == NULL)
    return;

  page = bz_article_new (self->article);
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (nav_view), page);
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static gboolean
is_null (gpointer    object,
         const char *value)
{
  return value == NULL;
}

static float
get_aspect_ratio (gpointer object,
                  float    ratio)
{
  return ratio > 0.0f ? ratio : 1.77f;
}

static BzAsyncTexture *
get_image (gpointer    object,
           const char *uri)
{
  g_autoptr (GFile) source     = NULL;
  g_autoptr (GFile) cache_file = NULL;
  g_autofree char *cache_dir   = NULL;
  g_autofree char *checksum    = NULL;
  g_autofree char *cache_path  = NULL;

  if (uri == NULL)
    return NULL;

  source     = g_file_new_for_uri (uri);
  checksum   = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);
  cache_dir  = bz_dup_module_dir ();
  cache_path = g_build_filename (cache_dir, checksum, NULL);
  cache_file = g_file_new_for_path (cache_path);

  return bz_async_texture_new_lazy (source, cache_file);
}

static void
bz_article_tile_class_init (BzArticleTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_article_tile_set_property;
  object_class->get_property = bz_article_tile_get_property;
  object_class->dispose      = bz_article_tile_dispose;

  props[PROP_ARTICLE] =
      g_param_spec_object (
          "article",
          NULL, NULL,
          BZ_TYPE_CURATED_ARTICLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ASPECT_RATIO] =
      g_param_spec_float (
          "aspect-ratio",
          NULL, NULL,
          0.0f, G_MAXFLOAT, 1.0f,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LIST_TILE);
  g_type_ensure (BZ_TYPE_ASYNC_TEXTURE);
  g_type_ensure (BZ_TYPE_ASPECT_PICTURE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-article-tile.ui");
  gtk_widget_class_bind_template_callback (widget_class, tile_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, get_image);
  gtk_widget_class_bind_template_callback (widget_class, get_aspect_ratio);
  gtk_widget_class_bind_template_child (widget_class, BzArticleTile, picture_box);
  gtk_widget_class_bind_template_child (widget_class, BzArticleTile, picture);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_BUTTON);
}

static void
bz_article_tile_init (BzArticleTile *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_article_tile_new (void)
{
  return g_object_new (BZ_TYPE_ARTICLE_TILE, NULL);
}

BzCuratedArticle *
bz_article_tile_get_article (BzArticleTile *self)
{
  g_return_val_if_fail (BZ_IS_ARTICLE_TILE (self), NULL);
  return self->article;
}

void
bz_article_tile_set_article (BzArticleTile    *self,
                             BzCuratedArticle *article)
{
  g_return_if_fail (BZ_IS_ARTICLE_TILE (self));

  g_clear_object (&self->article);

  if (article != NULL)
    self->article = g_object_ref (article);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ARTICLE]);
}
