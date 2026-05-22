/* bz-curated-view.c
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

#include "bz-curated-row.h"
#include "bz-curated-view.h"
#include "bz-dynamic-list-view.h"
#include "bz-entry-group.h"
#include "bz-root-curated-config.h"
#include "bz-row-view.h"

struct _BzCuratedView
{
  AdwBin parent_instance;

  BzStateInfo *state;

  BzContentProvider *curated_provider;
  GPtrArray         *css_providers;

  /* Template widgets */
  AdwViewStack *stack;
};

G_DEFINE_FINAL_TYPE (BzCuratedView, bz_curated_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_STATE,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

enum
{
  SIGNAL_BROWSE_FLATHUB,

  LAST_SIGNAL,
};
static guint signals[LAST_SIGNAL];

static void
items_changed (BzCuratedView *self,
               guint          position,
               guint          removed,
               guint          added,
               GListModel    *model);

static void
online_changed (BzCuratedView *self,
                GParamSpec    *pspec,
                BzStateInfo   *info);

static void
set_page (BzCuratedView *self);

static void
release_css_provider (gpointer ptr);

static void
bz_curated_view_dispose (GObject *object)
{
  BzCuratedView *self = BZ_CURATED_VIEW (object);

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (
        self->state, online_changed, self);
  if (self->curated_provider != NULL)
    g_signal_handlers_disconnect_by_func (
        self->curated_provider, items_changed, self);

  g_clear_object (&self->state);

  g_clear_object (&self->curated_provider);
  g_clear_pointer (&self->css_providers, g_ptr_array_unref);

  G_OBJECT_CLASS (bz_curated_view_parent_class)->dispose (object);
}

static void
bz_curated_view_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BzCuratedView *self = BZ_CURATED_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      g_value_set_object (value, bz_curated_view_get_state (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_curated_view_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BzCuratedView *self = BZ_CURATED_VIEW (object);

  switch (prop_id)
    {
    case PROP_STATE:
      bz_curated_view_set_state (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
browse_flathub_cb (BzCuratedView *self,
                   GtkButton     *button)
{
  g_signal_emit (self, signals[SIGNAL_BROWSE_FLATHUB], 0);
}

static void
bz_curated_view_class_init (BzCuratedViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_curated_view_dispose;
  object_class->get_property = bz_curated_view_get_property;
  object_class->set_property = bz_curated_view_set_property;

  props[PROP_STATE] =
      g_param_spec_object (
          "state",
          NULL, NULL,
          BZ_TYPE_STATE_INFO,
          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[SIGNAL_BROWSE_FLATHUB] =
      g_signal_new (
          "browse-flathub",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);

  g_type_ensure (BZ_TYPE_ROW_VIEW);
  g_type_ensure (BZ_TYPE_ROOT_CURATED_CONFIG);
  g_type_ensure (BZ_TYPE_CURATED_ROW);
  g_type_ensure (BZ_TYPE_DYNAMIC_LIST_VIEW);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-curated-view.ui");
  gtk_widget_class_bind_template_child (widget_class, BzCuratedView, stack);
  gtk_widget_class_bind_template_callback (widget_class, browse_flathub_cb);
}

static void
bz_curated_view_init (BzCuratedView *self)
{
  self->css_providers = g_ptr_array_new_with_free_func (release_css_provider);
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_curated_view_new (void)
{
  return g_object_new (BZ_TYPE_CURATED_VIEW, NULL);
}

void
bz_curated_view_set_state (BzCuratedView *self,
                           BzStateInfo   *state)
{
  g_return_if_fail (BZ_IS_CURATED_VIEW (self));
  g_return_if_fail (state == NULL || BZ_IS_STATE_INFO (state));

  if (self->state != NULL)
    g_signal_handlers_disconnect_by_func (
        self->state, online_changed, self);
  if (self->curated_provider != NULL)
    {
      g_signal_handlers_disconnect_by_func (
          self->curated_provider, items_changed, self);
      items_changed (
          self,
          0,
          g_list_model_get_n_items (G_LIST_MODEL (self->curated_provider)),
          0,
          G_LIST_MODEL (self->curated_provider));
    }
  g_clear_object (&self->state);
  g_clear_object (&self->curated_provider);

  if (state != NULL)
    {
      self->state = g_object_ref (state);
      g_signal_connect_swapped (
          state,
          "notify::online",
          G_CALLBACK (online_changed),
          self);

      g_object_get (
          state,
          "curated-provider", &self->curated_provider,
          NULL);
      if (self->curated_provider != NULL)
        {
          items_changed (
              self,
              0,
              0,
              g_list_model_get_n_items (G_LIST_MODEL (self->curated_provider)),
              G_LIST_MODEL (self->curated_provider));
          g_signal_connect_swapped (
              self->curated_provider, "items-changed",
              G_CALLBACK (items_changed), self);
        }
    }
  else
    set_page (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}

BzStateInfo *
bz_curated_view_get_state (BzCuratedView *self)
{
  g_return_val_if_fail (BZ_IS_CURATED_VIEW (self), NULL);
  return self->state;
}

static void
items_changed (BzCuratedView *self,
               guint          position,
               guint          removed,
               guint          added,
               GListModel    *model)
{
  if (removed > 0)
    g_ptr_array_remove_range (self->css_providers, position, removed);

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (BzRootCuratedConfig) config = NULL;
      const char *css                        = NULL;
      g_autoptr (GtkCssProvider) provider    = NULL;

      config = g_list_model_get_item (model, position + i);
      css    = bz_root_curated_config_get_css (config);

      provider = gtk_css_provider_new ();
      if (css != NULL)
        gtk_css_provider_load_from_string (provider, css);
      gtk_style_context_add_provider_for_display (
          gdk_display_get_default (),
          GTK_STYLE_PROVIDER (provider),
          GTK_STYLE_PROVIDER_PRIORITY_USER);

      g_ptr_array_insert (self->css_providers,
                          position + i,
                          g_steal_pointer (&provider));
    }

  set_page (self);
}

static void
online_changed (BzCuratedView *self,
                GParamSpec    *pspec,
                BzStateInfo   *info)
{
  set_page (self);
}

static void
set_page (BzCuratedView *self)
{
  const char *page = NULL;

  if (self->state != NULL &&
      !bz_state_info_get_online (self->state))
    page = "offline";
  else if (self->curated_provider != NULL &&
           g_list_model_get_n_items (G_LIST_MODEL (self->curated_provider)) > 0)
    page = "content";
  else
    page = "empty";

  adw_view_stack_set_visible_child_name (self->stack, page);
}

static void
release_css_provider (gpointer ptr)
{
  GtkCssProvider *provider = ptr;

  gtk_style_context_remove_provider_for_display (
      gdk_display_get_default (),
      GTK_STYLE_PROVIDER (provider));
  g_object_unref (provider);
}
