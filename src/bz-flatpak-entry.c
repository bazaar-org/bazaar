/* bz-flatpak-entry.c
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

#define G_LOG_DOMAIN  "BAZAAR::FLATPAK-ENTRY"
#define BAZAAR_MODULE "entry"

#include "config.h"

#include <glib/gi18n.h>
#include <xmlb.h>

#include "bz-app-permissions.h"
#include "bz-application-map-factory.h"
#include "bz-application.h"
#include "bz-appstream-parser.h"
#include "bz-flatpak-private.h"
#include "bz-io.h"
#include "bz-result.h"
#include "bz-serializable.h"
#include "bz-state-info.h"

#define VERSION_SUFFIX_REGEX "\\s+[0-9][0-9.]*\\s*$"

struct _BzFlatpakEntry
{
  BzEntry parent_instance;

  gboolean  user;
  gboolean  is_bundle;
  gboolean  is_installed_ref;
  char     *flatpak_name;
  char     *flatpak_id;
  char     *flatpak_version;
  char     *application_name;
  char     *application_runtime;
  char     *application_command;
  char     *runtime_name;
  char     *addon_extension_of_ref;
  char     *bundle_path;
  BzResult *runtime_result;

  FlatpakRef *ref;
};

static void
serializable_iface_init (BzSerializableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzFlatpakEntry,
    bz_flatpak_entry,
    BZ_TYPE_ENTRY,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_SERIALIZABLE, serializable_iface_init))

enum
{
  PROP_0,

  PROP_USER,
  PROP_FLATPAK_NAME,
  PROP_IS_BUNDLE,
  PROP_BUNDLE_PATH,
  PROP_FLATPAK_ID,
  PROP_FLATPAK_VERSION,
  PROP_APPLICATION_NAME,
  PROP_APPLICATION_RUNTIME,
  PROP_APPLICATION_COMMAND,
  PROP_RUNTIME_NAME,
  PROP_RUNTIME_RESULT,
  PROP_ADDON_OF_REF,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
clear_entry (BzFlatpakEntry *self);

static void
bz_flatpak_entry_dispose (GObject *object)
{
  BzFlatpakEntry *self = BZ_FLATPAK_ENTRY (object);

  clear_entry (self);
  g_clear_object (&self->ref);

  G_OBJECT_CLASS (bz_flatpak_entry_parent_class)->dispose (object);
}

static void
bz_flatpak_entry_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzFlatpakEntry *self = BZ_FLATPAK_ENTRY (object);

  switch (prop_id)
    {
    case PROP_USER:
      g_value_set_boolean (value, self->user);
      break;
    case PROP_FLATPAK_ID:
      g_value_set_string (value, self->flatpak_id);
      break;
    case PROP_FLATPAK_NAME:
      g_value_set_string (value, self->flatpak_name);
      break;
    case PROP_FLATPAK_VERSION:
      g_value_set_string (value, self->flatpak_version);
      break;
    case PROP_APPLICATION_NAME:
      g_value_set_string (value, self->application_name);
      break;
    case PROP_IS_BUNDLE:
      g_value_set_boolean (value, self->is_bundle);
      break;
    case PROP_BUNDLE_PATH:
      g_value_set_string (value, self->bundle_path);
      break;
    case PROP_APPLICATION_RUNTIME:
      g_value_set_string (value, self->application_runtime);
      break;
    case PROP_APPLICATION_COMMAND:
      g_value_set_string (value, self->application_command);
      break;
    case PROP_RUNTIME_NAME:
      g_value_set_string (value, self->runtime_name);
      break;
    case PROP_RUNTIME_RESULT:
      g_value_take_object (value, bz_flatpak_entry_dup_runtime_result (self));
      break;
    case PROP_ADDON_OF_REF:
      g_value_set_string (value, self->addon_extension_of_ref);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flatpak_entry_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  // BzFlatpakEntry *self = BZ_FLATPAK_ENTRY (object);

  switch (prop_id)
    {
    case PROP_USER:
    case PROP_FLATPAK_NAME:
    case PROP_FLATPAK_ID:
    case PROP_FLATPAK_VERSION:
    case PROP_APPLICATION_NAME:
    case PROP_IS_BUNDLE:
    case PROP_APPLICATION_RUNTIME:
    case PROP_APPLICATION_COMMAND:
    case PROP_RUNTIME_NAME:
    case PROP_ADDON_OF_REF:
    case PROP_BUNDLE_PATH:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flatpak_entry_class_init (BzFlatpakEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_flatpak_entry_set_property;
  object_class->get_property = bz_flatpak_entry_get_property;
  object_class->dispose      = bz_flatpak_entry_dispose;

  props[PROP_USER] =
      g_param_spec_boolean (
          "user",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE);

  props[PROP_FLATPAK_NAME] =
      g_param_spec_string (
          "flatpak-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_FLATPAK_ID] =
      g_param_spec_string (
          "flatpak-id",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_FLATPAK_VERSION] =
      g_param_spec_string (
          "flatpak-version",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_APPLICATION_NAME] =
      g_param_spec_string (
          "application-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_IS_BUNDLE] =
      g_param_spec_boolean (
          "is-bundle",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE);

  props[PROP_BUNDLE_PATH] =
      g_param_spec_string (
          "bundle-path",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_APPLICATION_RUNTIME] =
      g_param_spec_string (
          "application-runtime",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_APPLICATION_COMMAND] =
      g_param_spec_string (
          "application-command",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_RUNTIME_NAME] =
      g_param_spec_string (
          "runtime-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  props[PROP_RUNTIME_RESULT] =
      g_param_spec_object (
          "runtime",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_ADDON_OF_REF] =
      g_param_spec_string (
          "addon-extension-of-ref",
          NULL, NULL, NULL,
          G_PARAM_READABLE);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_flatpak_entry_init (BzFlatpakEntry *self)
{
}

static void
bz_flatpak_entry_real_serialize (BzSerializable  *serializable,
                                 GVariantBuilder *builder)
{
  BzFlatpakEntry *self = BZ_FLATPAK_ENTRY (serializable);

  g_variant_builder_add (builder, "{sv}", "user", g_variant_new_boolean (self->user));
  g_variant_builder_add (builder, "{sv}", "is-bundle", g_variant_new_boolean (self->is_bundle));
  g_variant_builder_add (builder, "{sv}", "is-installed-ref", g_variant_new_boolean (self->is_installed_ref));
  if (self->bundle_path != NULL)
    g_variant_builder_add (builder, "{sv}", "bundle-path", g_variant_new_string (self->bundle_path));
  if (self->flatpak_name != NULL)
    g_variant_builder_add (builder, "{sv}", "flatpak-name", g_variant_new_string (self->flatpak_name));
  if (self->flatpak_id != NULL)
    g_variant_builder_add (builder, "{sv}", "flatpak-id", g_variant_new_string (self->flatpak_id));
  if (self->flatpak_version != NULL)
    g_variant_builder_add (builder, "{sv}", "flatpak-version", g_variant_new_string (self->flatpak_version));
  if (self->application_name != NULL)
    g_variant_builder_add (builder, "{sv}", "application-name", g_variant_new_string (self->application_name));
  if (self->application_runtime != NULL)
    g_variant_builder_add (builder, "{sv}", "application-runtime", g_variant_new_string (self->application_runtime));
  if (self->application_command != NULL)
    g_variant_builder_add (builder, "{sv}", "application-command", g_variant_new_string (self->application_command));
  if (self->runtime_name != NULL)
    g_variant_builder_add (builder, "{sv}", "runtime-name", g_variant_new_string (self->runtime_name));
  if (self->addon_extension_of_ref != NULL)
    g_variant_builder_add (builder, "{sv}", "addon-extension-of-ref", g_variant_new_string (self->addon_extension_of_ref));

  bz_entry_serialize (BZ_ENTRY (self), builder);
}

static gboolean
bz_flatpak_entry_real_deserialize (BzSerializable *serializable,
                                   GVariant       *import,
                                   GError        **error)
{
  BzFlatpakEntry *self          = BZ_FLATPAK_ENTRY (serializable);
  g_autoptr (GVariantIter) iter = NULL;

  clear_entry (self);

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "user") == 0)
        self->user = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "is-bundle") == 0)
        self->is_bundle = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "is-installed-ref") == 0)
        self->is_installed_ref = g_variant_get_boolean (value);
      else if (g_strcmp0 (key, "bundle-path") == 0)
        self->bundle_path = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "flatpak-name") == 0)
        self->flatpak_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "flatpak-id") == 0)
        self->flatpak_id = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "flatpak-version") == 0)
        self->flatpak_version = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "application-name") == 0)
        self->application_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "application-runtime") == 0)
        self->application_runtime = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "application-command") == 0)
        self->application_command = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "runtime-name") == 0)
        self->runtime_name = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "addon-extension-of-ref") == 0)
        self->addon_extension_of_ref = g_variant_dup_string (value, NULL);
    }

  return bz_entry_deserialize (BZ_ENTRY (self), import, error);
}

static void
serializable_iface_init (BzSerializableInterface *iface)
{
  iface->serialize   = bz_flatpak_entry_real_serialize;
  iface->deserialize = bz_flatpak_entry_real_deserialize;
}

BzResult *
bz_flatpak_entry_dup_runtime_result (BzFlatpakEntry *self)
{
  BzStateInfo             *state             = NULL;
  BzApplicationMapFactory *factory           = NULL;
  g_autofree char         *runtime_unique_id = NULL;

  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);

  state = bz_state_info_get_default ();

  if (self->runtime_result != NULL)
    return g_object_ref (self->runtime_result);

  if (self->application_runtime == NULL)
    return NULL;

  factory = bz_state_info_get_entry_factory (state);
  if (factory == NULL)
    return NULL;

  runtime_unique_id = g_strdup_printf ("FLATPAK-SYSTEM::flathub::runtime/%s",
                                       self->application_runtime);

  self->runtime_result = bz_application_map_factory_convert_one (
      factory,
      gtk_string_object_new (runtime_unique_id));

  return self->runtime_result != NULL ? g_object_ref (self->runtime_result) : NULL;
}

BzFlatpakEntry *
bz_flatpak_entry_new_for_ref (FlatpakRef    *ref,
                              FlatpakRemote *remote,
                              gboolean       user,
                              AsComponent   *component,
                              const char    *appstream_dir,
                              GError       **error)
{
  g_autoptr (BzFlatpakEntry) self          = NULL;
  GBytes *bytes                            = NULL;
  g_autoptr (GKeyFile) key_file            = NULL;
  gboolean         result                  = FALSE;
  guint            kinds                   = 0;
  g_autofree char *module_dir              = NULL;
  const char      *id                      = NULL;
  g_autofree char *unique_id               = NULL;
  g_autofree char *unique_id_checksum      = NULL;
  guint64          download_size           = 0;
  guint64          installed_size          = 0;
  const char      *title                   = NULL;
  const char      *eol                     = NULL;
  const char      *remote_name             = NULL;
  g_autoptr (GdkPaintable) icon_paintable  = NULL;
  g_autoptr (BzAppPermissions) permissions = NULL;
  gboolean searchable                      = FALSE;

  g_return_val_if_fail (FLATPAK_IS_REF (ref), NULL);
  g_return_val_if_fail (FLATPAK_IS_REMOTE_REF (ref) || FLATPAK_IS_BUNDLE_REF (ref) || FLATPAK_IS_INSTALLED_REF (ref), NULL);
  g_return_val_if_fail (component == NULL || appstream_dir != NULL || FLATPAK_IS_BUNDLE_REF (ref) || FLATPAK_IS_INSTALLED_REF (ref), NULL);

  self                   = g_object_new (BZ_TYPE_FLATPAK_ENTRY, NULL);
  self->user             = user;
  self->is_bundle        = FLATPAK_IS_BUNDLE_REF (ref);
  self->is_installed_ref = FLATPAK_IS_INSTALLED_REF (ref);
  self->ref              = g_object_ref (ref);

  if (FLATPAK_IS_BUNDLE_REF (ref))
    {
      GFile *file = NULL;

      file = flatpak_bundle_ref_get_file (FLATPAK_BUNDLE_REF (ref));
      if (file != NULL)
        self->bundle_path = g_file_get_path (file);
    }

  key_file = g_key_file_new ();
  if (FLATPAK_IS_REMOTE_REF (ref))
    bytes = flatpak_remote_ref_get_metadata (FLATPAK_REMOTE_REF (ref));
  else if (FLATPAK_IS_BUNDLE_REF (ref))
    bytes = flatpak_bundle_ref_get_metadata (FLATPAK_BUNDLE_REF (ref));
  else if (FLATPAK_IS_INSTALLED_REF (ref))
    bytes = flatpak_installed_ref_load_metadata (FLATPAK_INSTALLED_REF (ref), NULL, error);

  result = g_key_file_load_from_bytes (
      key_file, bytes, G_KEY_FILE_NONE, error);
  if (!result)
    return NULL;

#define GET_STRING(member, group_name, key) \
  G_STMT_START                              \
  {                                         \
    self->member = g_key_file_get_string (  \
        key_file, group_name, key, error);  \
    if (self->member == NULL)               \
      return NULL;                          \
  }                                         \
  G_STMT_END

  if (!g_log_writer_default_would_drop (G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN))
    {
      gsize n_groups        = 0;
      g_auto (GStrv) groups = NULL;

      g_print ("Debug Key File Data for %s - groups:\n", flatpak_ref_get_name (ref));

      groups = g_key_file_get_groups (key_file, &n_groups);
      for (gsize i = 0; i < n_groups; i++)
        {
          gsize n_keys        = 0;
          g_auto (GStrv) keys = NULL;

          g_print ("   group %s\n", groups[i]);

          keys = g_key_file_get_keys (key_file, groups[i], &n_keys, NULL);
          for (gsize j = 0; j < n_keys; j++)
            {
              g_autofree char *value = NULL;

              value = g_key_file_get_value (key_file, groups[i], keys[j], NULL);
              g_print ("     %s=%s\n", keys[j], value);
            }
        }
    }

  if (g_key_file_has_group (key_file, "Application"))
    {
      kinds |= BZ_ENTRY_KIND_APPLICATION;

      GET_STRING (application_name, "Application", "name");
      GET_STRING (application_runtime, "Application", "runtime");
      if (g_key_file_has_key (key_file, "Application", "command", NULL))
        GET_STRING (application_command, "Application", "command");
    }

  if (g_key_file_has_group (key_file, "ExtensionOf"))
    {
      kinds |= BZ_ENTRY_KIND_ADDON;
      GET_STRING (addon_extension_of_ref, "ExtensionOf", "ref");
    }

  if (g_key_file_has_group (key_file, "Runtime"))
    {
      kinds |= BZ_ENTRY_KIND_RUNTIME;
      GET_STRING (runtime_name, "Runtime", "name");
    }

#undef GET_STRING

  // if (kinds == 0)
  //   {
  //     g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
  //                  "Key file presented no useful information");
  //     return NULL;
  //   }
  module_dir = bz_dup_module_dir ();

  self->flatpak_name    = g_strdup (flatpak_ref_get_name (ref));
  self->flatpak_id      = flatpak_ref_format_ref (ref);
  self->flatpak_version = g_strdup (flatpak_ref_get_branch (ref));

  id                 = flatpak_ref_get_name (ref);
  unique_id          = bz_flatpak_ref_format_unique (ref, user);
  unique_id_checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, unique_id, -1);

  if (remote != NULL)
    remote_name = flatpak_remote_get_name (remote);
  else if (FLATPAK_IS_BUNDLE_REF (ref))
    remote_name = flatpak_bundle_ref_get_origin (FLATPAK_BUNDLE_REF (ref));
  else if (FLATPAK_IS_INSTALLED_REF (ref))
    remote_name = flatpak_installed_ref_get_origin (FLATPAK_INSTALLED_REF (ref));

  if (FLATPAK_IS_REMOTE_REF (ref))
    download_size = flatpak_remote_ref_get_download_size (FLATPAK_REMOTE_REF (ref));
  else if (FLATPAK_IS_BUNDLE_REF (ref))
    {
      g_autoptr (GFileInfo) file_info = NULL;

      GFile *bundle_file = flatpak_bundle_ref_get_file (FLATPAK_BUNDLE_REF (ref));

      file_info = g_file_query_info (bundle_file,
                                     G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                     G_FILE_QUERY_INFO_NONE,
                                     NULL,
                                     NULL);
      if (file_info != NULL)
        download_size = g_file_info_get_size (file_info);
    }

  if (FLATPAK_IS_REMOTE_REF (ref))
    installed_size = flatpak_remote_ref_get_installed_size (FLATPAK_REMOTE_REF (ref));
  else if (FLATPAK_IS_BUNDLE_REF (ref))
    installed_size = flatpak_bundle_ref_get_installed_size (FLATPAK_BUNDLE_REF (ref));
  else if (FLATPAK_IS_INSTALLED_REF (ref))
    installed_size = flatpak_installed_ref_get_installed_size (FLATPAK_INSTALLED_REF (ref));

  if (component != NULL)
    {
      result = bz_appstream_parser_populate_entry (BZ_ENTRY (self),
                                                   component,
                                                   appstream_dir,
                                                   remote_name,
                                                   module_dir,
                                                   unique_id_checksum,
                                                   id,
                                                   kinds,
                                                   error);
      if (!result)
        return NULL;
    }

  g_object_get (self, "icon-paintable", &icon_paintable, NULL);
  if (icon_paintable == NULL)
    {
      if (FLATPAK_IS_BUNDLE_REF (ref))
        {
          for (int size = 128; size > 0; size -= 64)
            {
              g_autoptr (GBytes) icon_bytes = NULL;
              GdkTexture *texture           = NULL;

              icon_bytes = flatpak_bundle_ref_get_icon (FLATPAK_BUNDLE_REF (ref), size);
              if (icon_bytes == NULL)
                continue;

              texture = gdk_texture_new_from_bytes (icon_bytes, NULL);
              /* don't error out even if loading fails */

              if (texture != NULL)
                {
                  icon_paintable = (GdkPaintable *) g_steal_pointer (&texture);
                  break;
                }
            }
        }
      else if (FLATPAK_IS_INSTALLED_REF (ref))
        {
          BzStateInfo      *state     = NULL;
          const char       *icon_name = NULL;
          GtkIconTheme     *theme     = NULL;
          GtkIconPaintable *paintable = NULL;

          state     = bz_state_info_get_default ();
          icon_name = flatpak_ref_get_name (ref);

          theme = user
                      ? bz_state_info_get_user_icon_theme (state)
                      : bz_state_info_get_system_icon_theme (state);

          if (theme != NULL)
            {
              paintable = gtk_icon_theme_lookup_icon (
                  theme,
                  icon_name,
                  NULL,
                  128,
                  1,
                  GTK_TEXT_DIR_NONE,
                  0);

              if (paintable != NULL)
                icon_paintable = (GdkPaintable *) g_steal_pointer (&paintable);
            }
        }
    }

  g_object_get (self, "title", &title, NULL);
  if (title == NULL)
    {
      if (self->application_name != NULL)
        title = self->application_name;
      else if (self->runtime_name != NULL)
        title = self->runtime_name;
      else
        title = self->flatpak_id;
    }

  if ((kinds & BZ_ENTRY_KIND_RUNTIME) && !(kinds & BZ_ENTRY_KIND_ADDON) && self->flatpak_version != NULL)
    {
      static GRegex   *version_regex  = NULL;
      g_autofree char *stripped_title = NULL;

      if (g_once_init_enter_pointer (&version_regex))
        {
          g_autoptr (GRegex) re = NULL;

          /* GNOME runtimes have the flatpak version at the end whilst others don't. */
          re = g_regex_new (VERSION_SUFFIX_REGEX, 0, 0, NULL);
          g_once_init_leave_pointer (&version_regex, g_steal_pointer (&re));
        }
      stripped_title = g_regex_replace (version_regex, title, -1, 0, "", 0, NULL);

      title = g_strdup_printf ("%s %s", stripped_title, self->flatpak_version);
    }

  if (FLATPAK_IS_REMOTE_REF (ref))
    eol = flatpak_remote_ref_get_eol (FLATPAK_REMOTE_REF (ref));
  else if (FLATPAK_IS_INSTALLED_REF (ref))
    eol = flatpak_installed_ref_get_eol (FLATPAK_INSTALLED_REF (ref));

  permissions = bz_app_permissions_new_from_metadata (key_file, error);
  if (permissions == NULL)
    return NULL;

  searchable = !FLATPAK_IS_INSTALLED_REF (ref);

  g_object_set (
      self,
      "kinds", kinds,
      "id", id,
      "unique-id", unique_id,
      "unique-id-checksum", unique_id_checksum,
      "title", title,
      "eol", eol,
      "remote-repo-name", remote_name,
      "size", download_size,
      "installed-size", installed_size,
      "icon-paintable", icon_paintable,
      "permissions", permissions,
      "searchable", searchable,
      NULL);

  return g_steal_pointer (&self);
}

char *
bz_flatpak_ref_parts_format_unique (const char *origin,
                                    const char *fmt,
                                    gboolean    user)
{
  return g_strdup_printf (
      "FLATPAK-%s::%s::%s",
      user ? "USER" : "SYSTEM",
      origin != NULL ? origin : "bundle",
      fmt);
}

char *
bz_flatpak_ref_format_unique (FlatpakRef *ref,
                              gboolean    user)
{
  g_autofree char *fmt    = NULL;
  const char      *origin = NULL;

  fmt = flatpak_ref_format_ref (FLATPAK_REF (ref));

  if (FLATPAK_IS_REMOTE_REF (ref))
    origin = flatpak_remote_ref_get_remote_name (FLATPAK_REMOTE_REF (ref));
  else if (FLATPAK_IS_INSTALLED_REF (ref))
    origin = flatpak_installed_ref_get_origin (FLATPAK_INSTALLED_REF (ref));

  return bz_flatpak_ref_parts_format_unique (origin, fmt, user);
}

FlatpakRef *
bz_flatpak_entry_get_ref (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);

  if (self->ref == NULL)
    self->ref = flatpak_ref_parse (self->flatpak_id, NULL);

  return self->ref;
}

char *
bz_flatpak_id_format_unique (const char *flatpak_id,
                             gboolean    user)
{
  g_autoptr (FlatpakRef) ref = NULL;

  ref = flatpak_ref_parse (flatpak_id, NULL);
  if (ref == NULL)
    return NULL;

  return bz_flatpak_ref_format_unique (ref, user);
}

gboolean
bz_flatpak_entry_is_user (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), FALSE);
  return self->user;
}

const char *
bz_flatpak_entry_get_flatpak_name (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->flatpak_name;
}

const char *
bz_flatpak_entry_get_flatpak_id (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->flatpak_id;
}

const char *
bz_flatpak_entry_get_flatpak_version (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->flatpak_version;
}

const char *
bz_flatpak_entry_get_application_name (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->application_name;
}

const char *
bz_flatpak_entry_get_application_runtime (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->application_runtime;
}

const char *
bz_flatpak_entry_get_runtime_name (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->runtime_name;
}

const char *
bz_flatpak_entry_get_addon_extension_of_ref (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->addon_extension_of_ref;
}

gboolean
bz_flatpak_entry_is_bundle (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), FALSE);
  return self->is_bundle;
}

gboolean
bz_flatpak_entry_is_installed_ref (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), FALSE);

  return self->is_installed_ref;
}

const char *
bz_flatpak_entry_get_bundle_path (BzFlatpakEntry *self)
{
  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), NULL);
  return self->bundle_path;
}

gboolean
bz_flatpak_entry_launch (BzFlatpakEntry    *self,
                         BzFlatpakInstance *flatpak,
                         GError           **error)
{
  FlatpakRef *ref = NULL;
#ifdef SANDBOXED_LIBFLATPAK
  g_autofree char *fmt     = NULL;
  g_autofree char *cmdline = NULL;
#else
  FlatpakInstallation *installation = NULL;
#endif

  g_return_val_if_fail (BZ_IS_FLATPAK_ENTRY (self), FALSE);
  g_return_val_if_fail (BZ_IS_FLATPAK_INSTANCE (flatpak), FALSE);

  ref = bz_flatpak_entry_get_ref (self);

#ifdef SANDBOXED_LIBFLATPAK
  fmt = flatpak_ref_format_ref (FLATPAK_REF (ref));

  if (g_file_test ("/run/systemd", G_FILE_TEST_EXISTS))
    cmdline = g_strdup_printf ("flatpak-spawn --host systemd-run --user --pipe flatpak run %s", fmt);
  else
    cmdline = g_strdup_printf ("flatpak-spawn --host flatpak run %s", fmt);

  return g_spawn_command_line_async (cmdline, error);
#else
  installation =
      self->user
          ? bz_flatpak_instance_get_user_installation (flatpak)
          : bz_flatpak_instance_get_system_installation (flatpak);

  /* async? */
  return flatpak_installation_launch (
      installation,
      flatpak_ref_get_name (ref),
      flatpak_ref_get_arch (ref),
      flatpak_ref_get_branch (ref),
      flatpak_ref_get_commit (ref),
      NULL, error);
#endif
}

static void
clear_entry (BzFlatpakEntry *self)
{
  g_clear_pointer (&self->flatpak_name, g_free);
  g_clear_pointer (&self->flatpak_id, g_free);
  g_clear_pointer (&self->flatpak_version, g_free);
  g_clear_pointer (&self->application_name, g_free);
  g_clear_pointer (&self->application_runtime, g_free);
  g_clear_pointer (&self->application_command, g_free);
  g_clear_pointer (&self->runtime_name, g_free);
  g_clear_pointer (&self->addon_extension_of_ref, g_free);
  g_clear_pointer (&self->bundle_path, g_free);
  g_clear_object (&self->runtime_result);
}
