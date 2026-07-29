/* bz-banner-view.c
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

#include "bz-banner-view.h"

#include "bz-async-texture.h"
#include "bz-curated-image-info.h"

struct _BzBannerView
{
  AdwBin parent_instance;

  BzCuratedBanner *banner;

  AdwStyleManager *style_manager;
  GtkCssProvider  *css_provider;
  char            *instance_class;

  /* Template widgets */
  GtkBox     *box;
  GtkPicture *picture;
};

G_DEFINE_FINAL_TYPE (BzBannerView, bz_banner_view, ADW_TYPE_BIN)

enum
{
  PROP_0,

  PROP_BANNER,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
dark_changed (BzBannerView    *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr);

static void
refresh_background_color (BzBannerView    *self,
                          AdwStyleManager *mgr);

static BzAsyncTexture *
choose_image (const char *default_variant_uri,
              const char *light_variant_uri,
              const char *dark_variant_uri);

static void
bz_banner_view_dispose (GObject *object)
{
  BzBannerView *self = BZ_BANNER_VIEW (object);

  g_signal_handlers_disconnect_by_func (
      self->style_manager, dark_changed, self);

  if (self->css_provider != NULL)
    {
      gtk_style_context_remove_provider_for_display (
          gtk_widget_get_display (GTK_WIDGET (self)),
          GTK_STYLE_PROVIDER (self->css_provider));
      g_clear_object (&self->css_provider);
    }

  g_clear_pointer (&self->instance_class, g_free);
  g_clear_object (&self->banner);
  g_clear_object (&self->style_manager);

  G_OBJECT_CLASS (bz_banner_view_parent_class)->dispose (object);
}

static void
bz_banner_view_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  BzBannerView *self = BZ_BANNER_VIEW (object);

  switch (prop_id)
    {
    case PROP_BANNER:
      g_value_set_object (value, bz_banner_view_get_banner (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_banner_view_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  BzBannerView *self = BZ_BANNER_VIEW (object);

  switch (prop_id)
    {
    case PROP_BANNER:
      bz_banner_view_set_banner (self, g_value_get_object (value));
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
get_height (gpointer object,
            int      value)
{
  if (value == 0)
    return 250;

  return CLAMP (value, 150, 400);
}

static void
bz_banner_view_class_init (BzBannerViewClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_banner_view_dispose;
  object_class->get_property = bz_banner_view_get_property;
  object_class->set_property = bz_banner_view_set_property;

  props[PROP_BANNER] =
      g_param_spec_object (
          "banner",
          NULL, NULL,
          BZ_TYPE_CURATED_BANNER,
          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_ASYNC_TEXTURE);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-banner-view.ui");
  gtk_widget_class_bind_template_child (widget_class, BzBannerView, picture);
  gtk_widget_class_bind_template_child (widget_class, BzBannerView, box);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, get_image);
  gtk_widget_class_bind_template_callback (widget_class, get_height);
}

static void
dark_changed (BzBannerView    *self,
              GParamSpec      *pspec,
              AdwStyleManager *mgr)
{
  refresh_background_color (self, mgr);

  if (self->banner != NULL)
    g_object_notify (G_OBJECT (self->banner), "image");
}

static void
bz_banner_view_init (BzBannerView *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->style_manager = g_object_ref (
      adw_style_manager_get_default ());
  g_signal_connect_swapped (
      self->style_manager,
      "notify::dark",
      G_CALLBACK (dark_changed),
      self);
}

GtkWidget *
bz_banner_view_new (BzCuratedBanner *banner)
{
  return g_object_new (
      BZ_TYPE_BANNER_VIEW,
      "banner", banner,
      NULL);
}

void
bz_banner_view_set_banner (BzBannerView    *self,
                           BzCuratedBanner *banner)
{
  g_return_if_fail (BZ_IS_BANNER_VIEW (self));
  g_return_if_fail (banner == NULL || BZ_IS_CURATED_BANNER (banner));

  g_clear_object (&self->banner);

  if (banner != NULL)
    {
      self->banner = g_object_ref (banner);
      refresh_background_color (self, NULL);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BANNER]);
}

BzCuratedBanner *
bz_banner_view_get_banner (BzBannerView *self)
{
  g_return_val_if_fail (BZ_IS_BANNER_VIEW (self), NULL);
  return self->banner;
}

static void
refresh_background_color (BzBannerView    *self,
                          AdwStyleManager *mgr)
{
  g_autofree char *css         = NULL;
  g_autofree char *light_color = NULL;
  g_autofree char *dark_color  = NULL;
  const char      *color       = NULL;

  if (self->banner == NULL)
    return;

  if (mgr == NULL)
    mgr = adw_style_manager_get_default ();

  g_object_get (
      self->banner,
      "light-color", &light_color,
      "dark-color", &dark_color,
      NULL);

  color = adw_style_manager_get_dark (mgr) ? dark_color : light_color;
  if (color == NULL)
    return;

  if (self->instance_class == NULL)
    {
      self->instance_class = g_strdup_printf (
          "bz-banner-%p", (void *) self);

      gtk_widget_add_css_class (GTK_WIDGET (self->box), "card");
      gtk_widget_add_css_class (GTK_WIDGET (self->box), self->instance_class);
    }

  css = g_strdup_printf (".card.%s { background: %s; }",
                         self->instance_class, color);

  if (self->css_provider == NULL)
    {
      self->css_provider = gtk_css_provider_new ();
      gtk_style_context_add_provider_for_display (
          gtk_widget_get_display (GTK_WIDGET (self->box)),
          GTK_STYLE_PROVIDER (self->css_provider),
          GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

  gtk_css_provider_load_from_string (self->css_provider, css);
}

static BzAsyncTexture *
choose_image (const char *default_variant_uri,
              const char *light_variant_uri,
              const char *dark_variant_uri)
{
  gboolean    is_dark      = FALSE;
  const char *uri          = NULL;
  g_autoptr (GFile) source = NULL;

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
