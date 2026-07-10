/* bz-article-list-view.c
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

#include <libdex.h>

#include "bz-article-list-view.h"
#include "bz-article-tile.h"
#include "bz-curated-article.h"
#include "bz-curated-articles-info.h"
#include "bz-dynamic-list-view.h"
#include "env.h"
#include "global-net.h"
#include "bz-parser.h"
#include "bz-yaml-parser.h"

struct _BzArticleListView
{
  AdwBin parent_instance;

  BzCuratedArticlesInfo *articles;
  GtkWidget             *list_view;
};

G_DEFINE_FINAL_TYPE (BzArticleListView, bz_article_list_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_ARTICLES,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static DexFuture *
load_articles_fiber (BzCuratedArticlesInfo *info);

static void
load_articles_then (DexFuture         *future,
                    BzArticleListView *self);

static void
bz_article_list_view_dispose (GObject *object)
{
  BzArticleListView *self = BZ_ARTICLE_LIST_VIEW (object);

  g_clear_object (&self->articles);

  G_OBJECT_CLASS (bz_article_list_view_parent_class)->dispose (object);
}

static void
bz_article_list_view_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  BzArticleListView *self = BZ_ARTICLE_LIST_VIEW (object);

  switch (prop_id)
    {
    case PROP_ARTICLES:
      g_value_set_object (value, bz_article_list_view_get_articles (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_article_list_view_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  BzArticleListView *self = BZ_ARTICLE_LIST_VIEW (object);

  switch (prop_id)
    {
    case PROP_ARTICLES:
      bz_article_list_view_set_articles (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
on_bind_widget (BzDynamicListView *list_view,
                GtkWidget         *widget,
                GObject           *object,
                BzArticleListView *self)
{
  if (!BZ_IS_ARTICLE_TILE (widget) || self->articles == NULL)
    return;

  g_object_bind_property (self->articles, "aspect-ratio",
                          BZ_ARTICLE_TILE (widget), "aspect-ratio",
                          G_BINDING_SYNC_CREATE);
}

static void
bz_article_list_view_class_init (BzArticleListViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_article_list_view_dispose;
  object_class->get_property = bz_article_list_view_get_property;
  object_class->set_property = bz_article_list_view_set_property;

  props[PROP_ARTICLES] =
      g_param_spec_object (
          "articles",
          NULL, NULL,
          BZ_TYPE_CURATED_ARTICLES_INFO,
          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);
  g_type_ensure (BZ_TYPE_ARTICLE_TILE);
}

static void
bz_article_list_view_init (BzArticleListView *self)
{
  self->list_view = g_object_new (
      BZ_TYPE_DYNAMIC_LIST_VIEW,
      "hexpand", TRUE,
      "scroll", FALSE,
      "noscroll-kind", BZ_DYNAMIC_LIST_VIEW_KIND_FLOW_BOX,
      "child-type", "BzArticleTile",
      "child-prop", "article",
      "margin-top", 12,
      NULL);

  g_signal_connect (self->list_view, "bind-widget",
                    G_CALLBACK (on_bind_widget), self);

  adw_bin_set_child (ADW_BIN (self), self->list_view);
}

GtkWidget *
bz_article_list_view_new (BzCuratedArticlesInfo *articles)
{
  return g_object_new (
      BZ_TYPE_ARTICLE_LIST_VIEW,
      "articles", articles,
      NULL);
}

void
bz_article_list_view_set_articles (BzArticleListView     *self,
                                   BzCuratedArticlesInfo *articles)
{
  g_autoptr (DexFuture) future = NULL;

  g_return_if_fail (BZ_IS_ARTICLE_LIST_VIEW (self));
  g_return_if_fail (articles == NULL || BZ_IS_CURATED_ARTICLES_INFO (articles));

  g_clear_object (&self->articles);

  if (articles != NULL)
    self->articles = g_object_ref (articles);

  g_object_set (self->list_view, "model", NULL, NULL);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ARTICLES]);

  if (articles == NULL)
    return;

  future = dex_scheduler_spawn (
      dex_scheduler_get_default (),
      bz_get_dex_stack_size (),
      (DexFiberFunc) load_articles_fiber,
      g_object_ref (articles),
      g_object_unref);
  future = dex_future_then (
      future,
      (DexFutureCallback) load_articles_then,
      g_object_ref (self), g_object_unref);
  dex_future_disown (g_steal_pointer (&future));
}

BzCuratedArticlesInfo *
bz_article_list_view_get_articles (BzArticleListView *self)
{
  g_return_val_if_fail (BZ_IS_ARTICLE_LIST_VIEW (self), NULL);
  return self->articles;
}

static DexFuture *
load_articles_fiber (BzCuratedArticlesInfo *info)
{
  g_autoptr (GError) local_error  = NULL;
  g_autoptr (GBytes) bytes        = NULL;
  g_autoptr (BzYamlParser) parser = NULL;
  g_autoptr (GHashTable) results  = NULL;
  g_autoptr (GListStore) store    = NULL;
  const char *uri                 = NULL;
  GListModel *fallback            = NULL;
  GValue     *value               = NULL;
  GPtrArray  *list                = NULL;

  uri      = bz_curated_articles_info_get_uri (info);
  fallback = bz_curated_articles_info_get_list (info);

  if (uri == NULL)
    goto use_fallback;

  bytes = dex_await_boxed (bz_fetch_uri_contents (uri), &local_error);
  if (bytes == NULL)
    {
      g_warning ("Failed to fetch articles from %s: %s", uri, local_error->message);
      goto use_fallback;
    }

  g_type_ensure (BZ_TYPE_CURATED_ARTICLE);
  parser = bz_yaml_parser_new_for_resource_schema (
      "/io/github/kolunmi/Bazaar/article-list-schema.xml");
  results = bz_parser_process_bytes (BZ_PARSER (parser), bytes, &local_error);
  if (results == NULL)
    {
      g_warning ("Failed to parse articles yaml: %s", local_error->message);
      goto use_fallback;
    }

  value = g_hash_table_lookup (results, "/");
  list  = g_value_get_boxed (value);
  store = g_list_store_new (BZ_TYPE_CURATED_ARTICLE);

  for (guint i = 0; i < list->len; i++)
    {
      GHashTable       *item    = g_value_get_boxed (g_ptr_array_index (list, i));
      GValue           *v       = g_hash_table_lookup (item, "/");
      BzCuratedArticle *article = g_value_get_object (v);

      g_list_store_append (store, article);
    }

  return dex_future_new_take_object (g_steal_pointer (&store));

use_fallback:
  return dex_future_new_take_object (
      fallback != NULL
          ? g_object_ref (fallback)
          : (GListModel *) g_list_store_new (BZ_TYPE_CURATED_ARTICLE));
}

static void
load_articles_then (DexFuture         *future,
                    BzArticleListView *self)
{
  g_autoptr (GError) local_error = NULL;
  const GValue *value            = NULL;
  GListModel   *model            = NULL;

  value = dex_future_get_value (future, &local_error);
  if (value == NULL)
    {
      g_warning ("Failed to load articles: %s", local_error->message);
      return;
    }

  model = g_value_get_object (value);
  g_object_set (self->list_view, "model", model, NULL);
}
