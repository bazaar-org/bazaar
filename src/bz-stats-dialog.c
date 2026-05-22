/* bz-stats-dialog.c
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

#include "config.h"
#include <glib/gi18n.h>

#include "bz-data-graph.h"
#include "bz-stats-dialog.h"
#include "bz-world-map.h"
#include "bz-template-callbacks.h"

struct _BzStatsDialog
{
  AdwBreakpointBin parent_instance;

  GListModel *model;
  GListModel *country_model;
  int         total_downloads;

  /* Template widgets */
  AdwViewStack *stack;
  BzDataGraph  *graph;
  BzWorldMap   *world_map;
};

G_DEFINE_FINAL_TYPE (BzStatsDialog, bz_stats_dialog, ADW_TYPE_BREAKPOINT_BIN)

enum
{
  PROP_0,

  PROP_MODEL,
  PROP_COUNTRY_MODEL,
  PROP_TOTAL_DOWNLOADS,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_stats_dialog_dispose (GObject *object)
{
  BzStatsDialog *self = BZ_STATS_DIALOG (object);

  g_clear_object (&self->model);
  g_clear_object (&self->country_model);

  G_OBJECT_CLASS (bz_stats_dialog_parent_class)->dispose (object);
}

static void
bz_stats_dialog_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzStatsDialog *self = BZ_STATS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;
    case PROP_COUNTRY_MODEL:
      g_value_set_object (value, self->country_model);
      break;
    case PROP_TOTAL_DOWNLOADS:
      g_value_set_int (value, self->total_downloads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_stats_dialog_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzStatsDialog *self = BZ_STATS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_clear_object (&self->model);
      self->model = g_value_dup_object (value);
      if (self->model == NULL || g_list_model_get_n_items (self->model) < 10)
        adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (self->stack), "map");
      else
        adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (self->stack), "graph");
      break;
    case PROP_COUNTRY_MODEL:
      g_clear_object (&self->country_model);
      self->country_model = g_value_dup_object (value);
      break;
    case PROP_TOTAL_DOWNLOADS:
      self->total_downloads = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
format_total_downloads (gpointer object,
                        int      value)
{
  if (value <= 0)
    return g_strdup ("---");
  if (value >= 1000000)
    /* Translators: M is the suffix for millions */
    return g_strdup_printf (_("%.2fM Total Installs"), value / 1000000.0);
  else if (value >= 1000)
    /* Translators: K is the suffix for thousands*/
    return g_strdup_printf (_("%.2fK Total Installs"), value / 1000.0);
  else
    return g_strdup_printf (_("%'d Total Installs"), value);
}

static gboolean
model_has_enough_points (gpointer object,
                         GListModel *model)
{
  if (model == NULL)
    return FALSE;
  return g_list_model_get_n_items (model) >= 10;
}

static void
bz_stats_dialog_class_init (BzStatsDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_stats_dialog_dispose;
  object_class->get_property = bz_stats_dialog_get_property;
  object_class->set_property = bz_stats_dialog_set_property;

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_COUNTRY_MODEL] =
      g_param_spec_object (
          "country-model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE);

  props[PROP_TOTAL_DOWNLOADS] =
      g_param_spec_int (
          "total-downloads",
          NULL, NULL,
          0, G_MAXINT, 0,
          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_DATA_GRAPH);
  g_type_ensure (BZ_TYPE_WORLD_MAP);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-stats-dialog.ui");

  bz_widget_class_bind_all_util_callbacks (widget_class);

  gtk_widget_class_bind_template_callback (widget_class, format_total_downloads);
  gtk_widget_class_bind_template_callback (widget_class, model_has_enough_points);
  gtk_widget_class_bind_template_child (widget_class, BzStatsDialog, stack);
  gtk_widget_class_bind_template_child (widget_class, BzStatsDialog, graph);
  gtk_widget_class_bind_template_child (widget_class, BzStatsDialog, world_map);
}

static void
bz_stats_dialog_init (BzStatsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwBreakpointBin *
bz_stats_dialog_new (GListModel *model,
                     GListModel *country_model,
                     int         total_downloads)
{
  BzStatsDialog *stats_dialog = NULL;

  stats_dialog = g_object_new (
      BZ_TYPE_STATS_DIALOG,
      "model", model,
      "country-model", country_model,
      "total-downloads", total_downloads,
      NULL);

  return ADW_BREAKPOINT_BIN (stats_dialog);
}

void
bz_stats_dialog_animate_open (BzStatsDialog *self)
{
  g_return_if_fail (BZ_IS_STATS_DIALOG (self));
  bz_data_graph_animate_open (self->graph);
}
