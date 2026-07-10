/* bz-application-map-factory.c
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

#include "bz-application-map-factory.h"
#include "util.h"

struct _BzApplicationMapFactory
{
  GObject parent_instance;

  GtkMapListModelMapFunc func;
  gpointer               user_data;
  GDestroyNotify         ref_user_data;
  GDestroyNotify         unref_user_data;
  GtkFilter             *filter;
};

G_DEFINE_FINAL_TYPE (BzApplicationMapFactory, bz_application_map_factory, G_TYPE_OBJECT);

static void
bz_application_map_factory_dispose (GObject *object)
{
  BzApplicationMapFactory *self = BZ_APPLICATION_MAP_FACTORY (object);

  g_clear_object (&self->filter);

  if (self->unref_user_data != NULL)
    g_clear_pointer (&self->user_data, self->unref_user_data);

  G_OBJECT_CLASS (bz_application_map_factory_parent_class)->dispose (object);
}

static void
bz_application_map_factory_class_init (BzApplicationMapFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = bz_application_map_factory_dispose;
}

static void
bz_application_map_factory_init (BzApplicationMapFactory *self)
{
}

BzApplicationMapFactory *
bz_application_map_factory_new (GtkMapListModelMapFunc func,
                                gpointer               user_data,
                                GDestroyNotify         ref_user_data,
                                GDestroyNotify         unref_user_data,
                                GtkFilter             *filter)
{
  BzApplicationMapFactory *self = NULL;

  g_return_val_if_fail (func != NULL, NULL);
  g_return_val_if_fail (filter == NULL || GTK_IS_FILTER (filter), NULL);

  self                  = g_object_new (BZ_TYPE_APPLICATION_MAP_FACTORY, NULL);
  self->func            = func;
  self->user_data       = user_data;
  self->ref_user_data   = ref_user_data;
  self->unref_user_data = unref_user_data;
  self->filter          = bz_maybe_ref (filter, g_object_ref_sink);

  return self;
}

GListModel *
bz_application_map_factory_generate (BzApplicationMapFactory *self,
                                     GListModel              *model)
{
  g_autoptr (GListModel) backing = NULL;
  GtkMapListModel *map_model     = NULL;

  g_return_val_if_fail (BZ_IS_APPLICATION_MAP_FACTORY (self), NULL);
  g_return_val_if_fail (G_IS_LIST_MODEL (model), NULL);

  if (self->filter != NULL)
    {
      GtkFilterListModel *filter_model = NULL;

      filter_model = gtk_filter_list_model_new (
          g_object_ref (model), g_object_ref (self->filter));
      backing = G_LIST_MODEL (filter_model);
    }
  else
    backing = g_object_ref (model);

  if (self->ref_user_data != NULL && self->unref_user_data != NULL)
    self->ref_user_data (self->user_data);

  map_model = gtk_map_list_model_new (
      g_steal_pointer (&backing),
      self->func,
      self->user_data,
      self->ref_user_data != NULL
          ? self->unref_user_data
          : NULL);

  return G_LIST_MODEL (map_model);
}

gpointer
bz_application_map_factory_convert_one (BzApplicationMapFactory *self,
                                        gpointer                 item)
{
  g_return_val_if_fail (BZ_IS_APPLICATION_MAP_FACTORY (self), NULL);
  g_return_val_if_fail (item != NULL, NULL);

  return self->func (item, self->user_data);
}

/* End of bz-application-map-factory.c */
