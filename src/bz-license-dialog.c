/* bz-license-dialog.c
 *
 * Copyright 2025 Alexander Vanhee
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

#include <adwaita.h>
#include <glib/gi18n.h>

#include "bz-entry.h"
#include "bz-license-dialog.h"
#include "bz-lozenge.h"
#include "bz-spdx.h"
#include "bz-url.h"

struct _BzLicenseDialog
{
  AdwBin parent_instance;

  BzEntry *entry;
};

G_DEFINE_FINAL_TYPE (BzLicenseDialog, bz_license_dialog, ADW_TYPE_BIN)

enum
{
  PROP_0,
  PROP_ENTRY,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { NULL };

static void
bz_license_dialog_dispose (GObject *object)
{
  BzLicenseDialog *self = NULL;

  self = BZ_LICENSE_DIALOG (object);

  g_clear_object (&self->entry);

  G_OBJECT_CLASS (bz_license_dialog_parent_class)->dispose (object);
}

static void
bz_license_dialog_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BzLicenseDialog *self = NULL;

  self = BZ_LICENSE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_value_set_object (value, self->entry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_license_dialog_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BzLicenseDialog *self = NULL;

  self = BZ_LICENSE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_clear_object (&self->entry);
      self->entry = g_value_dup_object (value);
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

static char *
get_label_cb (gpointer object,
              BzEntry *entry)
{
  g_autofree char *license  = NULL;
  gboolean         is_floss = FALSE;

  if (entry == NULL)
    return g_strdup ("");

  g_object_get (
      entry,
      "is-floss", &is_floss,
      "project-license", &license,
      NULL);

  if (license == NULL || *license == '\0')
    return g_strdup (_ ("Unknown License"));

  if (is_floss)
    return g_strdup (_ ("Community Built"));

  if (bz_spdx_is_proprietary (license))
    return g_strdup (_ ("Proprietary"));

  return g_strdup (_ ("Special License"));
}

static char *
get_involved_tooltip (gpointer object,
                      BzEntry *entry)
{
  g_autoptr (GListModel) share_urls = NULL;
  g_autoptr (BzUrl) first_url       = NULL;
  const char *url                   = NULL;

  if (entry == NULL)
    return NULL;

  g_object_get (entry, "share-urls", &share_urls, NULL);

  if (share_urls == NULL || g_list_model_get_n_items (share_urls) < 1)
    return NULL;

  first_url = g_list_model_get_item (share_urls, 1);
  url       = bz_url_get_url (first_url);

  if (url != NULL && *url != '\0')
    return g_strdup (url);

  return NULL;
}

static char *
format_license_link (const char *license)
{
  g_autofree char *license_name = NULL;
  g_autofree char *license_url  = NULL;

  if (!bz_spdx_is_valid (license))
    return g_strdup (license);

  license_name = bz_spdx_get_name (license);
  if (license_name == NULL || *license_name == '\0')
    {
      g_clear_pointer (&license_name, g_free);
      license_name = g_strdup (license);
    }

  license_url = bz_spdx_get_url (license);
  return g_strdup_printf ("<a href=\"%s\">%s</a>", license_url, license_name);
}

static char *
get_license_info (gpointer object,
                  BzEntry *entry)
{
  g_autofree char *license  = NULL;
  gboolean         is_floss = FALSE;
  g_autofree char *link     = NULL;

  if (entry == NULL)
    return g_strdup ("");

  g_object_get (
      entry,
      "is-floss", &is_floss,
      "project-license", &license,
      NULL);

  if (license == NULL || *license == '\0')
    {
      if (is_floss)
        return g_strdup (_ ("This app is developed in the open by an international community.\n\n"
                            "You can participate and help make it even better."));
      else
        return g_strdup (_ ("The license of this app is not known"));
    }

  if (is_floss)
    {
      link = format_license_link (license);
      return g_strdup_printf (_ ("This app is developed in the open by an international community, "
                                 "and released under the %s license.\n\n"
                                 "You can participate and help make it even better."),
                              link);
    }

  if (bz_spdx_is_proprietary (license))
    {
      return g_strdup (_ ("This app is not developed in the open, so only its developers know how it works. "
                          "It may be insecure in ways that are hard to detect, and it may change without oversight.\n\n"
                          "You may or may not be able to contribute to this app."));
    }

  link = format_license_link (license);
  return g_strdup_printf (_ ("This app is developed under the special license %s.\n\n"
                             "You may or may not be able to contribute to this app."),
                          link);
}

static void
contribute_cb (BzLicenseDialog *self)
{
  g_autoptr (GListModel) share_urls = NULL;
  g_autoptr (BzUrl) first_url       = NULL;
  const char *url                   = NULL;

  g_object_get (self->entry, "share-urls", &share_urls, NULL);

  if (share_urls == NULL || g_list_model_get_n_items (share_urls) < 1)
    return;

  first_url = g_list_model_get_item (share_urls, 1);
  url       = bz_url_get_url (first_url);

  if (url != NULL && *url != '\0')
    g_app_info_launch_default_for_uri (url, NULL, NULL);
}

static char *
get_eula_url (BzEntry *entry)
{
  g_autofree char *license = NULL;
  const char *url = NULL;

  if (entry == NULL)
    return NULL;

  g_object_get (entry, "project-license", &license, NULL);

  if (license == NULL || !g_str_has_prefix (license, "LicenseRef-proprietary="))
    return NULL;

  url = license + strlen ("LicenseRef-proprietary=");
  return (*url != '\0') ? g_strdup (url) : NULL;
}

static gboolean
should_show_eula (gpointer object,
                  BzEntry *entry)
{
  g_autofree char *url = get_eula_url (entry);
  return url != NULL;
}

static char *
eula_tooltip (gpointer object,
              BzEntry *entry)
{
  return get_eula_url (entry);
}

static void
eula_cb (BzLicenseDialog *self)
{
  g_autofree char *url = get_eula_url (self->entry);
  if (url != NULL)
    g_app_info_launch_default_for_uri (url, NULL, NULL);
}

static void
bz_license_dialog_class_init (BzLicenseDialogClass *klass)
{
  GObjectClass   *object_class = NULL;
  GtkWidgetClass *widget_class = NULL;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_license_dialog_dispose;
  object_class->get_property = bz_license_dialog_get_property;
  object_class->set_property = bz_license_dialog_set_property;

  props[PROP_ENTRY] =
      g_param_spec_object (
          "entry",
          NULL, NULL,
          BZ_TYPE_ENTRY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_LOZENGE);
  gtk_widget_class_set_template_from_resource (
      widget_class,
      "/io/github/kolunmi/Bazaar/bz-license-dialog.ui");

  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, get_label_cb);
  gtk_widget_class_bind_template_callback (widget_class, get_license_info);
  gtk_widget_class_bind_template_callback (widget_class, get_involved_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, should_show_eula);
  gtk_widget_class_bind_template_callback (widget_class, eula_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, contribute_cb);
  gtk_widget_class_bind_template_callback (widget_class, eula_cb);
}

static void
bz_license_dialog_init (BzLicenseDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

AdwDialog *
bz_license_dialog_new (BzEntry *entry)
{
  BzLicenseDialog *widget = NULL;
  AdwDialog       *dialog = NULL;

  widget = g_object_new (BZ_TYPE_LICENSE_DIALOG, "entry", entry, NULL);

  dialog = adw_dialog_new ();
  adw_dialog_set_content_width (dialog, 425);
  adw_dialog_set_child (dialog, GTK_WIDGET (widget));

  return dialog;
}

AdwNavigationPage *
bz_license_page_new (BzEntry *entry)
{
  BzLicenseDialog   *widget = NULL;
  AdwNavigationPage *page   = NULL;

  widget = g_object_new (BZ_TYPE_LICENSE_DIALOG, "entry", entry, NULL);
  page   = adw_navigation_page_new (GTK_WIDGET (widget), _ ("License"));
  adw_navigation_page_set_tag (page, "license");

  return page;
}
