/* bz-auth-state.c
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

#include <libsecret/secret.h>

#include "bz-async-texture.h"
#include "bz-auth-state.h"

#define SECRET_SCHEMA_NAME "io.github.kolunmi.Bazaar.FlathubAuth"
#define SECRET_LABEL       "Flathub Authentication"

struct _BzAuthState
{
  GObject parent_instance;

  char           *name;
  char           *token;
  char           *profile_icon_url;
  GDateTime      *token_expires;
  BzAsyncTexture *paintable;

  gboolean loading;
  guint    expiration_timeout_id;
};

G_DEFINE_FINAL_TYPE (BzAuthState, bz_auth_state, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_NAME,
  PROP_TOKEN,
  PROP_PROFILE_ICON_URL,
  PROP_AUTHENTICATED,
  PROP_PAINTABLE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static const SecretSchema *
get_secret_schema (void)
{
  static const SecretSchema schema = {
    SECRET_SCHEMA_NAME,
    SECRET_SCHEMA_NONE,
    {
      { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
      { "NULL", 0 },
      }
  };

  return &schema;
}

static gboolean
on_token_expired (gpointer user_data)
{
  BzAuthState *self           = BZ_AUTH_STATE (user_data);
  self->expiration_timeout_id = 0;
  bz_auth_state_clear (self);
  return G_SOURCE_REMOVE;
}

static void
schedule_token_expiration (BzAuthState *self)
{
  GDateTime *now;
  gint64     seconds_until_expiration;

  if (self->expiration_timeout_id != 0)
    g_source_remove (self->expiration_timeout_id);

  self->expiration_timeout_id = 0;

  if (self->token_expires == NULL)
    return;

  now                      = g_date_time_new_now_utc ();
  seconds_until_expiration = g_date_time_difference (self->token_expires, now) / G_TIME_SPAN_SECOND;
  g_date_time_unref (now);

  if (seconds_until_expiration <= 0)
    {
      bz_auth_state_clear (self);
      return;
    }

  if (seconds_until_expiration > G_MAXUINT / 1000)
    self->expiration_timeout_id = g_timeout_add_seconds (G_MAXUINT / 1000, on_token_expired, self);
  else
    self->expiration_timeout_id = g_timeout_add_seconds (seconds_until_expiration, on_token_expired, self);
}

static void
save_to_secrets (BzAuthState *self)
{
  GHashTable *attributes;
  GError     *error                   = NULL;
  g_autoptr (GVariantBuilder) builder = NULL;
  g_autoptr (GVariant) variant        = NULL;
  g_autofree char *serialized         = NULL;

  if (self->loading)
    return;

  attributes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert (attributes, g_strdup ("service"), g_strdup ("flathub"));

  builder = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);

  if (self->name != NULL)
    g_variant_builder_add (builder, "{sv}", "name", g_variant_new_string (self->name));
  if (self->token != NULL)
    g_variant_builder_add (builder, "{sv}", "token", g_variant_new_string (self->token));
  if (self->token_expires != NULL)
    {
      g_autofree char *expires = g_date_time_format_iso8601 (self->token_expires);
      g_variant_builder_add (builder, "{sv}", "token-expires", g_variant_new_string (expires));
    }
  if (self->profile_icon_url != NULL)
    g_variant_builder_add (builder, "{sv}", "profile-icon-url", g_variant_new_string (self->profile_icon_url));

  variant    = g_variant_builder_end (builder);
  serialized = g_variant_print (variant, FALSE);

  secret_password_storev_sync (
      get_secret_schema (),
      attributes,
      NULL,
      SECRET_LABEL,
      serialized,
      NULL,
      &error);

  g_hash_table_unref (attributes);

  if (error != NULL)
    {
      g_warning ("Failed to save authentication to secrets: %s", error->message);
      g_error_free (error);
    }
}

static void
load_from_secrets (BzAuthState *self)
{
  GHashTable      *attributes;
  GError          *error  = NULL;
  g_autofree char *secret = NULL;

  attributes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert (attributes, g_strdup ("service"), g_strdup ("flathub"));

  secret = secret_password_lookupv_sync (
      get_secret_schema (),
      attributes,
      NULL,
      &error);

  g_hash_table_unref (attributes);

  if (error != NULL)
    {
      if (!g_error_matches (error, SECRET_ERROR, SECRET_ERROR_NO_SUCH_OBJECT))
        g_warning ("Failed to load authentication from secrets: %s", error->message);
      g_error_free (error);
      return;
    }

  if (secret != NULL)
    {
      g_autoptr (GVariant) variant  = NULL;
      g_autoptr (GVariantIter) iter = NULL;

      variant = g_variant_parse (G_VARIANT_TYPE_VARDICT, secret, NULL, NULL, &error);

      if (error != NULL)
        {
          g_warning ("Failed to parse secret: %s", error->message);
          g_error_free (error);
          return;
        }

      if (variant != NULL)
        {
          iter = g_variant_iter_new (variant);
          for (;;)
            {
              g_autofree char *key       = NULL;
              g_autoptr (GVariant) value = NULL;

              if (!g_variant_iter_next (iter, "{sv}", &key, &value))
                break;

              if (g_strcmp0 (key, "name") == 0)
                {
                  g_clear_pointer (&self->name, g_free);
                  self->name = g_variant_dup_string (value, NULL);
                }
              else if (g_strcmp0 (key, "token") == 0)
                {
                  g_clear_pointer (&self->token, g_free);
                  self->token = g_variant_dup_string (value, NULL);
                }
              else if (g_strcmp0 (key, "token-expires") == 0)
                {
                  g_autoptr (GDateTime) dt = g_date_time_new_from_iso8601 (g_variant_get_string (value, NULL), NULL);
                  if (dt != NULL)
                    {
                      g_clear_pointer (&self->token_expires, g_date_time_unref);
                      self->token_expires = g_steal_pointer (&dt);
                    }
                }
              else if (g_strcmp0 (key, "profile-icon-url") == 0)
                {
                  g_clear_pointer (&self->profile_icon_url, g_free);
                  self->profile_icon_url = g_variant_dup_string (value, NULL);

                  g_clear_object (&self->paintable);
                  if (self->profile_icon_url != NULL && self->profile_icon_url[0] != '\0')
                    {
                      g_autoptr (GFile) file = g_file_new_for_uri (self->profile_icon_url);
                      self->paintable        = bz_async_texture_new (file, NULL);
                    }
                }
            }
        }
    }

  if (self->token_expires != NULL)
    {
      GDateTime *now = g_date_time_new_now_utc ();
      if (g_date_time_compare (now, self->token_expires) >= 0)
        {
          g_date_time_unref (now);
          bz_auth_state_clear (self);
          return;
        }
      g_date_time_unref (now);
      schedule_token_expiration (self);
    }
}

static void
clear_secrets (BzAuthState *self)
{
  gboolean result                   = FALSE;
  g_autoptr (GError) local_error    = NULL;
  g_autoptr (GHashTable) attributes = NULL;

  attributes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_replace (attributes, g_strdup ("service"), g_strdup ("flathub"));

  result = secret_password_clearv_sync (
      get_secret_schema (),
      attributes,
      NULL,
      &local_error);
  if (!result)
    g_warning ("Failed to clear auth values from secrets: %s", local_error->message);
}

static void
bz_auth_state_dispose (GObject *object)
{
  BzAuthState *self = BZ_AUTH_STATE (object);

  g_clear_handle_id (&self->expiration_timeout_id, g_source_remove);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->token, g_free);
  g_clear_pointer (&self->profile_icon_url, g_free);
  g_clear_pointer (&self->token_expires, g_date_time_unref);
  g_clear_object (&self->paintable);

  G_OBJECT_CLASS (bz_auth_state_parent_class)->dispose (object);
}

static void
bz_auth_state_finalize (GObject *object)
{
  BzAuthState *self = BZ_AUTH_STATE (object);

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->token, g_free);
  g_clear_pointer (&self->profile_icon_url, g_free);
  g_clear_pointer (&self->token_expires, g_date_time_unref);

  G_OBJECT_CLASS (bz_auth_state_parent_class)->finalize (object);
}

static void
bz_auth_state_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzAuthState *self = BZ_AUTH_STATE (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;
    case PROP_TOKEN:
      g_value_set_string (value, self->token);
      break;
    case PROP_PROFILE_ICON_URL:
      g_value_set_string (value, self->profile_icon_url);
      break;
    case PROP_AUTHENTICATED:
      g_value_set_boolean (value, bz_auth_state_is_authenticated (self));
      break;
    case PROP_PAINTABLE:
      g_value_set_object (value, bz_auth_state_get_paintable (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_auth_state_class_init (BzAuthStateClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_auth_state_dispose;
  object_class->finalize     = bz_auth_state_finalize;
  object_class->get_property = bz_auth_state_get_property;

  properties[PROP_NAME] =
      g_param_spec_string (
          "name",
          NULL, NULL,
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_TOKEN] =
      g_param_spec_string (
          "token",
          NULL, NULL,
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PROFILE_ICON_URL] =
      g_param_spec_string (
          "profile-icon-url",
          NULL, NULL,
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_AUTHENTICATED] =
      g_param_spec_boolean (
          "authenticated",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_PAINTABLE] =
      g_param_spec_object (
          "paintable",
          NULL, NULL,
          GDK_TYPE_PAINTABLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
bz_auth_state_init (BzAuthState *self)
{
  self->loading = TRUE;
  load_from_secrets (self);
  self->loading = FALSE;
}

BzAuthState *
bz_auth_state_new (void)
{
  return g_object_new (BZ_TYPE_AUTH_STATE, NULL);
}

const char *
bz_auth_state_get_name (BzAuthState *self)
{
  g_return_val_if_fail (BZ_IS_AUTH_STATE (self), NULL);
  return self->name;
}

const char *
bz_auth_state_get_token (BzAuthState *self)
{
  g_return_val_if_fail (BZ_IS_AUTH_STATE (self), NULL);
  return self->token;
}

const char *
bz_auth_state_get_profile_icon_url (BzAuthState *self)
{
  g_return_val_if_fail (BZ_IS_AUTH_STATE (self), NULL);
  return self->profile_icon_url;
}

gboolean
bz_auth_state_is_authenticated (BzAuthState *self)
{
  g_return_val_if_fail (BZ_IS_AUTH_STATE (self), FALSE);
  return self->token != NULL && self->token[0] != '\0';
}

GdkPaintable *
bz_auth_state_get_paintable (BzAuthState *self)
{
  g_return_val_if_fail (BZ_IS_AUTH_STATE (self), NULL);
  return (GdkPaintable *) self->paintable;
}

void
bz_auth_state_set_authenticated (BzAuthState *self,
                                 const char  *name,
                                 const char  *token,
                                 GDateTime   *token_expires,
                                 const char  *profile_icon_url)
{
  gboolean was_authenticated = FALSE;
  gboolean name_changed      = FALSE;
  gboolean token_changed     = FALSE;
  gboolean icon_changed      = FALSE;

  g_return_if_fail (BZ_IS_AUTH_STATE (self));

  was_authenticated = bz_auth_state_is_authenticated (self);

  if (g_strcmp0 (self->name, name) != 0)
    {
      g_clear_pointer (&self->name, g_free);
      self->name   = g_strdup (name);
      name_changed = TRUE;
    }

  if (g_strcmp0 (self->token, token) != 0)
    {
      g_clear_pointer (&self->token, g_free);
      self->token   = g_strdup (token);
      token_changed = TRUE;
    }

  g_clear_pointer (&self->token_expires, g_date_time_unref);
  if (token_expires != NULL)
    self->token_expires = g_date_time_ref (token_expires);

  if (g_strcmp0 (self->profile_icon_url, profile_icon_url) != 0)
    {
      g_clear_pointer (&self->profile_icon_url, g_free);
      self->profile_icon_url = g_strdup (profile_icon_url);
      icon_changed           = TRUE;

      g_clear_object (&self->paintable);
      if (profile_icon_url != NULL && profile_icon_url[0] != '\0')
        {
          g_autoptr (GFile) file = g_file_new_for_uri (profile_icon_url);
          self->paintable        = bz_async_texture_new (file, NULL);
        }
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAINTABLE]);
    }

  if (name_changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
  if (token_changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TOKEN]);
  if (icon_changed)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROFILE_ICON_URL]);

  if (!!was_authenticated != !!bz_auth_state_is_authenticated (self))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTHENTICATED]);

  schedule_token_expiration (self);
  save_to_secrets (self);
}

void
bz_auth_state_clear (BzAuthState *self)
{
  g_return_if_fail (BZ_IS_AUTH_STATE (self));

  g_clear_handle_id (&self->expiration_timeout_id, g_source_remove);
  clear_secrets (self);
  bz_auth_state_set_authenticated (self, NULL, NULL, NULL, NULL);
}
