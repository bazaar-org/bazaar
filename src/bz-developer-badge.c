/* bz-developer-badge.c
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

#include <glib/gi18n.h>

#include "bz-developer-badge.h"
#include "bz-verification-status.h"

struct _BzDeveloperBadge
{
  GtkBox parent_instance;

  BzEntry *entry;

  GtkLabel      *developer_label;
  GtkMenuButton *info_button;
  GtkPopover    *popover;
  GtkLabel      *popover_label;
};

G_DEFINE_FINAL_TYPE (BzDeveloperBadge, bz_developer_badge, GTK_TYPE_BOX)

enum
{
  PROP_0,
  PROP_ENTRY,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static gboolean
is_null (gpointer object,
         GObject *value)
{
  return value == NULL;
}

static gboolean
invert_boolean (gpointer object,
                gboolean value)
{
  return !value;
}

static char *
get_developer_name (gpointer object,
                    GObject *entry)
{
  if (entry == NULL || !BZ_IS_ENTRY (entry))
    return NULL;

  return g_strdup (bz_entry_get_developer (BZ_ENTRY (entry)));
}

static char *
get_icon_name (gpointer object,
               GObject *status)
{
  gboolean verified = FALSE;

  if (status == NULL)
    return g_strdup ("info-outline-symbolic");

  g_object_get (status, "verified", &verified, NULL);

  return g_strdup (verified ? "verified-checkmark-symbolic" : "info-outline-symbolic");
}

static char *
get_verified_tooltip (gpointer object,
                      GObject *status)
{
  gboolean verified = FALSE;

  if (status == NULL)
    return g_strdup (_ ("Not Verified"));

  g_object_get (status, "verified", &verified, NULL);

  return g_strdup (verified ? _ ("Verified") : _ ("Not Verified"));
}

static char *
format_app_id (const char *app_id)
{
  return g_strdup_printf ("<b>%s</b>", app_id);
}

static char *
format_website_url (const char *website)
{
  return g_strdup_printf ("<b><a href='https://%s' title='https://%s'>%s</a></b>",
                          website, website, website);
}

static char *
format_provider_name (const char *login_provider)
{
  if (g_strcmp0 (login_provider, "github") == 0)
    return g_strdup ("GitHub");
  else if (g_strcmp0 (login_provider, "gitlab") == 0)
    return g_strdup ("GitLab");
  else if (g_strcmp0 (login_provider, "gnome") == 0)
    return g_strdup ("GNOME GitLab");
  else if (g_strcmp0 (login_provider, "kde") == 0)
    return g_strdup ("KDE GitLab");
  else
    return g_strdup (login_provider);
}

static char *
format_provider_url (const char *login_provider,
                     const char *login_name)
{
  if (g_strcmp0 (login_provider, "github") == 0)
    return g_strdup_printf ("https://github.com/%s", login_name);
  else if (g_strcmp0 (login_provider, "gitlab") == 0)
    return g_strdup_printf ("https://gitlab.com/%s", login_name);
  else if (g_strcmp0 (login_provider, "gnome") == 0)
    return g_strdup_printf ("https://gitlab.gnome.org/%s", login_name);
  else if (g_strcmp0 (login_provider, "kde") == 0)
    return g_strdup_printf ("https://invent.kde.org/%s", login_name);
  else
    return NULL;
}

static char *
get_developer_link (gpointer              object,
                    BzEntry              *entry,
                    BzVerificationStatus *status)
{
  const char      *dev            = NULL;
  const char      *method         = NULL;
  const char      *login_name     = NULL;
  const char      *login_provider = NULL;
  const char      *website        = NULL;
  gboolean         verified       = FALSE;
  g_autofree char *escaped_dev    = NULL;

  if (entry == NULL || !BZ_IS_ENTRY (entry))
    return NULL;

  dev         = bz_entry_get_developer (entry);
  escaped_dev = g_markup_escape_text (dev, -1);

  if (status == NULL)
    return g_steal_pointer (&escaped_dev);

  g_object_get (status,
                "verified", &verified,
                "method", &method,
                "login-name", &login_name,
                "login-provider", &login_provider,
                "website", &website,
                NULL);

  if (!verified)
    return g_steal_pointer (&escaped_dev);

  if (g_strcmp0 (method, "login_provider") == 0 && login_name != NULL && login_provider != NULL)
    {
      g_autofree char *url = NULL;

      url = format_provider_url (login_provider, login_name);
      if (url != NULL)
        return g_strdup_printf ("<a href='%s' title='%s'>%s</a>", url, url, escaped_dev);
    }

  if (g_strcmp0 (method, "website") == 0 && website != NULL && *website != '\0')
      return g_strdup_printf ("<a href='https://%s' title='https://%s'>%s</a>", website, website, escaped_dev);

  return g_steal_pointer (&escaped_dev);
}

static char *
get_popover_text (gpointer object,
                  GObject *entry_obj,
                  GObject *status_obj)
{
  BzVerificationStatus *status            = NULL;
  const char           *app_id            = NULL;
  const char           *website           = NULL;
  const char           *method            = NULL;
  const char           *login_name        = NULL;
  const char           *login_provider    = NULL;
  gboolean              verified          = FALSE;
  g_autofree char      *formatted_app_id  = NULL;
  g_autofree char      *formatted_website = NULL;
  g_autofree char      *provider_name     = NULL;

  if (entry_obj == NULL || !BZ_IS_ENTRY (entry_obj))
    return g_strdup (_ ("Developer information not available."));

  app_id           = bz_entry_get_id (BZ_ENTRY (entry_obj));
  formatted_app_id = format_app_id (app_id);

  if (status_obj == NULL)
    return g_strdup_printf (_ ("The ownership of the %s app ID has not been verified and it may be a community package."),
                            formatted_app_id);

  status = BZ_VERIFICATION_STATUS (status_obj);

  g_object_get (status,
                "verified", &verified,
                "method", &method,
                "website", &website,
                "login-name", &login_name,
                "login-provider", &login_provider,
                NULL);

  if (!verified)
    return g_strdup_printf (_ ("The ownership of the %s app ID has not been verified and it may be a community package."),
                            formatted_app_id);

  if (g_strcmp0 (method, "manual") == 0)
    return g_strdup_printf (_ ("The ownership of the %s app ID has been manually verified by the Flathub team."),
                            formatted_app_id);

  if (g_strcmp0 (method, "login_provider") == 0 && login_name != NULL && login_provider != NULL)
    {
      g_autofree char *url         = NULL;
      g_autofree char *linked_name = NULL;

      provider_name = format_provider_name (login_provider);
      url           = format_provider_url (login_provider, login_name);

      if (url != NULL)
        linked_name = g_strdup_printf ("<a href='%s' title='%s'>%s</a>", url, url, login_name);
      else
        linked_name = g_strdup (login_name);

      return g_strdup_printf (_ ("The ownership of the %1$s app ID has been verified by <b>%2$s</b> on <b>%3$s</b>."),
                              formatted_app_id, linked_name, provider_name);
    }

  if (website != NULL && *website != '\0')
    {
      formatted_website = format_website_url (website);
      return g_strdup_printf (_ ("The ownership of the %1$s app ID has been verified using %2$s."),
                              formatted_app_id, formatted_website);
    }

  return g_strdup_printf (_ ("The ownership of the %s app ID has been verified."), formatted_app_id);
}

static void
on_info_button_enter (GtkEventControllerMotion *controller,
                      gdouble                   x,
                      gdouble                   y,
                      gpointer                  user_data)
{
  GtkWidget *widget = GTK_WIDGET (user_data);
  gtk_widget_set_cursor_from_name (widget, "pointer");
}

static void
on_info_button_leave (GtkEventControllerMotion *controller,
                      gpointer                  user_data)
{
  GtkWidget *widget = GTK_WIDGET (user_data);
  gtk_widget_set_cursor (widget, NULL);
}

static gboolean
on_popover_label_activate_link (GtkLabel         *label,
                                const char       *uri,
                                BzDeveloperBadge *self)
{
  gtk_popover_popdown (self->popover);
  return FALSE;
}

static void
bz_developer_badge_dispose (GObject *object)
{
  BzDeveloperBadge *self = BZ_DEVELOPER_BADGE (object);

  g_clear_object (&self->entry);

  G_OBJECT_CLASS (bz_developer_badge_parent_class)->dispose (object);
}

static void
bz_developer_badge_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzDeveloperBadge *self = BZ_DEVELOPER_BADGE (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_value_set_object (value, bz_developer_badge_get_entry (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_developer_badge_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzDeveloperBadge *self = BZ_DEVELOPER_BADGE (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      bz_developer_badge_set_entry (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_developer_badge_class_init (BzDeveloperBadgeClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_developer_badge_dispose;
  object_class->get_property = bz_developer_badge_get_property;
  object_class->set_property = bz_developer_badge_set_property;

  props[PROP_ENTRY] =
      g_param_spec_object (
          "entry",
          NULL, NULL,
          BZ_TYPE_ENTRY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-developer-badge.ui");
  gtk_widget_class_bind_template_child (widget_class, BzDeveloperBadge, developer_label);
  gtk_widget_class_bind_template_child (widget_class, BzDeveloperBadge, info_button);
  gtk_widget_class_bind_template_child (widget_class, BzDeveloperBadge, popover);
  gtk_widget_class_bind_template_child (widget_class, BzDeveloperBadge, popover_label);
  gtk_widget_class_bind_template_callback (widget_class, on_info_button_enter);
  gtk_widget_class_bind_template_callback (widget_class, on_info_button_leave);
  gtk_widget_class_bind_template_callback (widget_class, on_popover_label_activate_link);
  gtk_widget_class_bind_template_callback (widget_class, is_null);
  gtk_widget_class_bind_template_callback (widget_class, invert_boolean);
  gtk_widget_class_bind_template_callback (widget_class, get_developer_name);
  gtk_widget_class_bind_template_callback (widget_class, get_developer_link);
  gtk_widget_class_bind_template_callback (widget_class, get_icon_name);
  gtk_widget_class_bind_template_callback (widget_class, get_verified_tooltip);
  gtk_widget_class_bind_template_callback (widget_class, get_popover_text);
}

static void
bz_developer_badge_init (BzDeveloperBadge *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
bz_developer_badge_new (void)
{
  return g_object_new (BZ_TYPE_DEVELOPER_BADGE, NULL);
}

BzEntry *
bz_developer_badge_get_entry (BzDeveloperBadge *self)
{
  g_return_val_if_fail (BZ_IS_DEVELOPER_BADGE (self), NULL);
  return self->entry;
}

void
bz_developer_badge_set_entry (BzDeveloperBadge *self,
                              BzEntry          *entry)
{
  BzVerificationStatus *status   = NULL;
  gboolean              verified = FALSE;

  g_return_if_fail (BZ_IS_DEVELOPER_BADGE (self));
  g_return_if_fail (entry == NULL || BZ_IS_ENTRY (entry));

  if (g_set_object (&self->entry, entry))
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self), "verified");
      gtk_widget_remove_css_class (GTK_WIDGET (self), "dimmed");

      if (entry != NULL)
        {
          g_object_get (entry, "verification-status", &status, NULL);
          if (status != NULL)
            {
              g_object_get (status, "verified", &verified, NULL);
              g_object_unref (status);
            }
          gtk_widget_add_css_class (GTK_WIDGET (self), verified ? "verified" : "dimmed");
        }

      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENTRY]);
    }
}
