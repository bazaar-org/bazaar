/* bz-app-permissions.c
 *
 * Copyright 2026 Alexander Vanhee
 * Copyright (C) 2022 Red Hat <www.redhat.com>
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

#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include "bz-app-permissions.h"

#define DOES_NOT_CONTAIN ((guint) ~0)

G_DEFINE_FLAGS_TYPE (
    BzAppPermissionsFlags,
    bz_app_permissions_flags,
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_NONE, "none"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_NETWORK, "network"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS, "system-bus"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS, "session-bus"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_DEVICES, "devices"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_HOME_FULL, "home-full"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_HOME_READ, "home-read"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL, "filesystem-full"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ, "filesystem-read"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL, "downloads-full"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ, "downloads-read"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_SETTINGS, "settings"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_X11, "x11"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX, "escape-sandbox"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_OTHER, "filesystem-other"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER, "bus-policy-other"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES, "audio-devices"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_INPUT_DEVICES, "input-devices"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES, "system-devices"),
    G_DEFINE_ENUM_VALUE (BZ_APP_PERMISSIONS_FLAGS_SCREEN, "screen"))

G_DEFINE_ENUM_TYPE (
    BzFilesystemPathType,
    bz_filesystem_path_type,
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_SYSTEM_ROOT, "system-root"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_HOME_SUBDIR, "home-subdir"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_HOST_OS, "host-os"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_HOST_ETC, "host-etc"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_DESKTOP, "xdg-desktop"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_DOCUMENTS, "xdg-documents"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_MUSIC, "xdg-music"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_PICTURES, "xdg-pictures"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_PUBLIC_SHARE, "xdg-public-share"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_VIDEOS, "xdg-videos"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_TEMPLATES, "xdg-templates"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_CACHE, "xdg-cache"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_CONFIG, "xdg-config"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_DATA, "xdg-data"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_XDG_RUN, "xdg-run"),
    G_DEFINE_ENUM_VALUE (BZ_FILESYSTEM_PATH_CUSTOM, "custom"))

G_DEFINE_ENUM_TYPE (
    BzBusPolicyPermission,
    bz_bus_policy_permission,
    G_DEFINE_ENUM_VALUE (BZ_BUS_POLICY_PERMISSION_UNKNOWN, "unknown"),
    G_DEFINE_ENUM_VALUE (BZ_BUS_POLICY_PERMISSION_NONE, "none"),
    G_DEFINE_ENUM_VALUE (BZ_BUS_POLICY_PERMISSION_SEE, "see"),
    G_DEFINE_ENUM_VALUE (BZ_BUS_POLICY_PERMISSION_TALK, "talk"),
    G_DEFINE_ENUM_VALUE (BZ_BUS_POLICY_PERMISSION_OWN, "own"))

struct _BzAppPermissions
{
  GObject parent;

  gboolean              is_sealed;
  BzAppPermissionsFlags flags;
  GPtrArray            *filesystem_read;
  GPtrArray            *filesystem_full;
  GPtrArray            *bus_policies;
};

G_DEFINE_FINAL_TYPE (BzAppPermissions, bz_app_permissions, G_TYPE_OBJECT)

static gint  cmp_filesystem_path_pointers (gconstpointer item1,
                                           gconstpointer item2);
static int   cmp_bus_policy_qsort (const void *item1,
                                   const void *item2);
static guint app_permissions_get_array_index (GPtrArray           *array,
                                              BzFilesystemPathType type,
                                              const char          *subpath);
static guint get_strv_index (const gchar *const *strv,
                             const gchar        *value);

static void
bz_app_permissions_finalize (GObject *object)
{
  BzAppPermissions *self = BZ_APP_PERMISSIONS (object);

  g_clear_pointer (&self->filesystem_read, g_ptr_array_unref);
  g_clear_pointer (&self->filesystem_full, g_ptr_array_unref);
  g_clear_pointer (&self->bus_policies, g_ptr_array_unref);

  G_OBJECT_CLASS (bz_app_permissions_parent_class)->finalize (object);
}

static void
bz_app_permissions_class_init (BzAppPermissionsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = bz_app_permissions_finalize;
}

static void
bz_app_permissions_init (BzAppPermissions *self)
{
}

BzFilesystemPath *
bz_filesystem_path_new (BzFilesystemPathType type,
                        const char          *subpath)
{
  BzFilesystemPath *path = g_new0 (BzFilesystemPath, 1);
  path->type             = type;
  path->subpath          = g_strdup (subpath);
  return path;
}

void
bz_filesystem_path_free (BzFilesystemPath *path)
{
  g_return_if_fail (path != NULL);
  g_free (path->subpath);
  g_free (path);
}

char *
bz_filesystem_path_to_display_string (const BzFilesystemPath *path)
{
  g_return_val_if_fail (path != NULL, NULL);

  switch (path->type)
    {
    case BZ_FILESYSTEM_PATH_SYSTEM_ROOT:
      return g_strdup_printf (_ ("System folder %s"), path->subpath ? path->subpath : "/");
    case BZ_FILESYSTEM_PATH_HOME_SUBDIR:
      return g_strdup_printf (_ ("Home subfolder %s"), path->subpath);
    case BZ_FILESYSTEM_PATH_HOST_OS:
      return g_strdup (_ ("Host system folders"));
    case BZ_FILESYSTEM_PATH_HOST_ETC:
      return g_strdup (_ ("Host system configuration from /etc"));
    case BZ_FILESYSTEM_PATH_XDG_DESKTOP:
      if (path->subpath)
        return g_strdup_printf (_ ("Desktop subfolder %s"), path->subpath);
      return g_strdup (_ ("Desktop folder"));
    case BZ_FILESYSTEM_PATH_XDG_DOCUMENTS:
      if (path->subpath)
        return g_strdup_printf (_ ("Documents subfolder %s"), path->subpath);
      return g_strdup (_ ("Documents folder"));
    case BZ_FILESYSTEM_PATH_XDG_MUSIC:
      if (path->subpath)
        return g_strdup_printf (_ ("Music subfolder %s"), path->subpath);
      return g_strdup (_ ("Music folder"));
    case BZ_FILESYSTEM_PATH_XDG_PICTURES:
      if (path->subpath)
        return g_strdup_printf (_ ("Pictures subfolder %s"), path->subpath);
      return g_strdup (_ ("Pictures folder"));
    case BZ_FILESYSTEM_PATH_XDG_PUBLIC_SHARE:
      if (path->subpath)
        return g_strdup_printf (_ ("Public Share subfolder %s"), path->subpath);
      return g_strdup (_ ("Public Share folder"));
    case BZ_FILESYSTEM_PATH_XDG_VIDEOS:
      if (path->subpath)
        return g_strdup_printf (_ ("Videos subfolder %s"), path->subpath);
      return g_strdup (_ ("Videos folder"));
    case BZ_FILESYSTEM_PATH_XDG_TEMPLATES:
      if (path->subpath)
        return g_strdup_printf (_ ("Templates subfolder %s"), path->subpath);
      return g_strdup (_ ("Templates folder"));
    case BZ_FILESYSTEM_PATH_XDG_CACHE:
      if (path->subpath)
        return g_strdup_printf (_ ("User cache subfolder %s"), path->subpath);
      return g_strdup (_ ("User cache folder"));
    case BZ_FILESYSTEM_PATH_XDG_CONFIG:
      if (path->subpath)
        return g_strdup_printf (_ ("User configuration subfolder %s"), path->subpath);
      return g_strdup (_ ("User configuration folder"));
    case BZ_FILESYSTEM_PATH_XDG_DATA:
      if (path->subpath)
        return g_strdup_printf (_ ("User data subfolder %s"), path->subpath);
      return g_strdup (_ ("User data folder"));
    case BZ_FILESYSTEM_PATH_XDG_RUN:
      if (path->subpath)
        return g_strdup_printf (_ ("User runtime subfolder %s"), path->subpath);
      return g_strdup (_ ("User runtime folder"));
    case BZ_FILESYSTEM_PATH_CUSTOM:
      return g_strdup_printf (_ ("Filesystem access to %s"), path->subpath);
    default:
      return g_strdup (_ ("Unknown filesystem path"));
    }
}

const char *
bz_filesystem_path_to_icon_name (const BzFilesystemPath *path)
{
  switch (path->type)
    {
    case BZ_FILESYSTEM_PATH_HOST_OS:
      return "computer-symbolic";
    case BZ_FILESYSTEM_PATH_HOST_ETC:
      return "emblem-system-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_DESKTOP:
      return "user-desktop-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_DOCUMENTS:
      return "folder-documents-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_MUSIC:
      return "folder-music-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_PICTURES:
      return "folder-pictures-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_PUBLIC_SHARE:
      return "folder-publicshare-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_VIDEOS:
      return "folder-videos-symbolic";
    case BZ_FILESYSTEM_PATH_XDG_CONFIG:
      return "emblem-system-symbolic";
    case BZ_FILESYSTEM_PATH_SYSTEM_ROOT:
    case BZ_FILESYSTEM_PATH_HOME_SUBDIR:
    case BZ_FILESYSTEM_PATH_XDG_TEMPLATES:
    case BZ_FILESYSTEM_PATH_XDG_CACHE:
    case BZ_FILESYSTEM_PATH_XDG_DATA:
    case BZ_FILESYSTEM_PATH_XDG_RUN:
    case BZ_FILESYSTEM_PATH_CUSTOM:
    default:
      return "folder-symbolic";
    }
}

BzBusPolicy *
bz_bus_policy_new (GBusType              bus_type,
                   const char           *bus_name,
                   BzBusPolicyPermission permission)
{
  BzBusPolicy *policy = NULL;

  g_return_val_if_fail (bus_type != G_BUS_TYPE_NONE, NULL);
  g_return_val_if_fail (bus_name != NULL && *bus_name != '\0', NULL);

  policy             = g_new0 (BzBusPolicy, 1);
  policy->bus_type   = bus_type;
  policy->bus_name   = g_strdup (bus_name);
  policy->permission = permission;

  return policy;
}

void
bz_bus_policy_free (BzBusPolicy *self)
{
  g_return_if_fail (self != NULL);

  g_free (self->bus_name);
  g_free (self);
}

BzAppPermissions *
bz_app_permissions_new (void)
{
  return g_object_new (BZ_TYPE_APP_PERMISSIONS, NULL);
}

void
bz_app_permissions_seal (BzAppPermissions *self)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));

  if (self->is_sealed)
    return;

  self->is_sealed = TRUE;

  if (self->filesystem_read)
    qsort (self->filesystem_read->pdata, self->filesystem_read->len, sizeof (gpointer), cmp_filesystem_path_pointers);

  if (self->filesystem_full)
    qsort (self->filesystem_full->pdata, self->filesystem_full->len, sizeof (gpointer), cmp_filesystem_path_pointers);

  if (self->bus_policies)
    qsort (self->bus_policies->pdata, self->bus_policies->len, sizeof (gpointer), cmp_bus_policy_qsort);
}

gboolean
bz_app_permissions_is_sealed (BzAppPermissions *self)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), TRUE);

  return self->is_sealed;
}

gboolean
bz_app_permissions_is_empty (BzAppPermissions *self)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), TRUE);

  return (self->flags == BZ_APP_PERMISSIONS_FLAGS_NONE &&
          (self->filesystem_read == NULL || self->filesystem_read->len == 0) &&
          (self->filesystem_full == NULL || self->filesystem_full->len == 0) &&
          (self->bus_policies == NULL || self->bus_policies->len == 0));
}

void
bz_app_permissions_set_flags (BzAppPermissions     *self,
                              BzAppPermissionsFlags flags)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));

  g_assert (!self->is_sealed);

  self->flags = flags;
}

BzAppPermissionsFlags
bz_app_permissions_get_flags (BzAppPermissions *self)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), BZ_APP_PERMISSIONS_FLAGS_NONE);

  return self->flags;
}

void
bz_app_permissions_add_flag (BzAppPermissions     *self,
                             BzAppPermissionsFlags flags)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));
  g_return_if_fail (flags != BZ_APP_PERMISSIONS_FLAGS_NONE);

  g_assert (!self->is_sealed);

  self->flags = self->flags | flags;
}

void
bz_app_permissions_remove_flag (BzAppPermissions     *self,
                                BzAppPermissionsFlags flags)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));
  g_return_if_fail (flags != BZ_APP_PERMISSIONS_FLAGS_NONE);

  g_assert (!self->is_sealed);

  self->flags = (self->flags & (~flags));
}

void
bz_app_permissions_add_filesystem_read (BzAppPermissions    *self,
                                        BzFilesystemPathType type,
                                        const char          *subpath)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));

  g_assert (!self->is_sealed);

  if (app_permissions_get_array_index (self->filesystem_read, type, subpath) != DOES_NOT_CONTAIN ||
      app_permissions_get_array_index (self->filesystem_full, type, subpath) != DOES_NOT_CONTAIN)
    return;

  if (self->filesystem_read == NULL)
    self->filesystem_read = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_filesystem_path_free);

  g_ptr_array_add (self->filesystem_read, bz_filesystem_path_new (type, subpath));
}

const GPtrArray *
bz_app_permissions_get_filesystem_read (BzAppPermissions *self)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), NULL);

  return self->filesystem_read;
}

void
bz_app_permissions_add_filesystem_full (BzAppPermissions    *self,
                                        BzFilesystemPathType type,
                                        const char          *subpath)
{
  guint read_index = DOES_NOT_CONTAIN;

  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));

  g_assert (!self->is_sealed);

  if (app_permissions_get_array_index (self->filesystem_full, type, subpath) != DOES_NOT_CONTAIN)
    return;

  if (self->filesystem_full == NULL)
    self->filesystem_full = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_filesystem_path_free);

  g_ptr_array_add (self->filesystem_full, bz_filesystem_path_new (type, subpath));

  read_index = app_permissions_get_array_index (self->filesystem_read, type, subpath);
  if (read_index != DOES_NOT_CONTAIN)
    {
      g_ptr_array_remove_index (self->filesystem_read, read_index);
      if (self->filesystem_read->len == 0)
        g_clear_pointer (&self->filesystem_read, g_ptr_array_unref);
    }
}

const GPtrArray *
bz_app_permissions_get_filesystem_full (BzAppPermissions *self)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), NULL);

  return self->filesystem_full;
}

void
bz_app_permissions_add_bus_policy (BzAppPermissions     *self,
                                   GBusType              bus_type,
                                   const char           *bus_name,
                                   BzBusPolicyPermission permission)
{
  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));
  g_return_if_fail (bus_type != G_BUS_TYPE_NONE);
  g_return_if_fail (bus_name != NULL && *bus_name != '\0');
  g_return_if_fail (permission != BZ_BUS_POLICY_PERMISSION_UNKNOWN);

  g_assert (!self->is_sealed);

  for (unsigned int i = 0; self->bus_policies != NULL && i < self->bus_policies->len; i++)
    {
      BzBusPolicy *policy = g_ptr_array_index (self->bus_policies, i);

      if (policy->bus_type == bus_type &&
          g_str_equal (policy->bus_name, bus_name))
        {
          policy->permission = MAX (policy->permission, permission);
          return;
        }
    }

  if (permission == BZ_BUS_POLICY_PERMISSION_NONE)
    return;

  if (self->bus_policies == NULL)
    self->bus_policies = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_bus_policy_free);

  g_ptr_array_add (self->bus_policies, bz_bus_policy_new (bus_type, bus_name, permission));
}

const BzBusPolicy *const *
bz_app_permissions_get_bus_policies (BzAppPermissions *self,
                                     size_t           *out_n_bus_policies)
{
  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), NULL);
  g_return_val_if_fail (self->is_sealed, NULL);

  if (out_n_bus_policies != NULL)
    *out_n_bus_policies = (self->bus_policies != NULL) ? self->bus_policies->len : 0;

  return (self->bus_policies != NULL && self->bus_policies->len > 0) ? (const BzBusPolicy *const *) self->bus_policies->pdata : NULL;
}

BzAppPermissions *
bz_app_permissions_new_from_metadata (GKeyFile *keyfile,
                                      GError  **error)
{
  char                **strv               = NULL;
  BzAppPermissions     *permissions        = NULL;
  BzAppPermissionsFlags flags              = BZ_APP_PERMISSIONS_FLAGS_NONE;
  g_autofree char      *app_id             = NULL;
  g_autofree char      *mpris_id           = NULL;
  g_autofree char      *app_id_non_devel   = NULL;
  g_autofree char      *mpris_id_non_devel = NULL;

  g_return_val_if_fail (keyfile != NULL, NULL);

  permissions = bz_app_permissions_new ();
  app_id      = g_key_file_get_value (keyfile, "Application", "name", NULL);

  strv = g_key_file_get_string_list (keyfile, "Context", "sockets", NULL, NULL);
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "system-bus"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS | BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "session-bus"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS | BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
  if (strv != NULL &&
      !g_strv_contains ((const gchar *const *) strv, "fallback-x11") &&
      g_strv_contains ((const gchar *const *) strv, "x11"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_X11;
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "fallback-x11") &&
      !g_strv_contains ((const gchar *const *) strv, "wayland"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_X11;
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "pulseaudio"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES;
  if (strv != NULL && g_strv_contains ((const char *const *) strv, "gpg-agent"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
  g_strfreev (strv);

  strv = g_key_file_get_string_list (keyfile, "Context", "devices", NULL, NULL);
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "all"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_DEVICES;
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "input"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_INPUT_DEVICES;
  if (strv != NULL && (g_strv_contains ((const gchar *const *) strv, "shm") ||
                       g_strv_contains ((const gchar *const *) strv, "kvm")))
    flags |= BZ_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES;
  g_strfreev (strv);

  strv = g_key_file_get_string_list (keyfile, "Context", "shared", NULL, NULL);
  if (strv != NULL && g_strv_contains ((const gchar *const *) strv, "network"))
    flags |= BZ_APP_PERMISSIONS_FLAGS_NETWORK;
  g_strfreev (strv);

  strv = g_key_file_get_string_list (keyfile, "Context", "filesystems", NULL, NULL);
  if (strv != NULL)
    {
      const struct
      {
        const gchar          *key;
        BzAppPermissionsFlags perm;
      } filesystems_access[] = {
        {                              "home",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_FULL },
        {                           "home:rw",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_FULL },
        {                           "home:ro",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_READ },
        {                                 "~",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_FULL },
        {                              "~:rw",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_FULL },
        {                              "~:ro",                                       BZ_APP_PERMISSIONS_FLAGS_HOME_READ },
        {                              "host",                                 BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
        {                           "host:rw",                                 BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
        {                           "host:ro",                                 BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ },
        {          "xdg-config/kdeglobals:ro",                                            BZ_APP_PERMISSIONS_FLAGS_NONE },
        {                      "xdg-download",                                  BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL },
        {                   "xdg-download:rw",                                  BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL },
        {                   "xdg-download:ro",                                  BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ },
        { "xdg-data/flatpak/overrides:create",                                  BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },
        {                "xdg-run/pipewire-0", BZ_APP_PERMISSIONS_FLAGS_SCREEN | BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },
        {             "xdg-run/pipewire-0:rw", BZ_APP_PERMISSIONS_FLAGS_SCREEN | BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },
        {             "xdg-run/pipewire-0:ro", BZ_APP_PERMISSIONS_FLAGS_SCREEN | BZ_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },
        {                     "xdg-run/gvfsd",                                 BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
      };
      guint filesystems_hits = 0;
      guint strv_len         = g_strv_length (strv);

      for (guint i = 0; i < G_N_ELEMENTS (filesystems_access); i++)
        {
          guint index = get_strv_index ((const gchar *const *) strv, filesystems_access[i].key);
          if (index < strv_len)
            {
              flags |= filesystems_access[i].perm;
              filesystems_hits++;
              strv[index][0] = '\0';
            }
        }

      if ((flags & BZ_APP_PERMISSIONS_FLAGS_HOME_FULL) != 0)
        flags = flags & ~BZ_APP_PERMISSIONS_FLAGS_HOME_READ;
      if ((flags & BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL) != 0)
        flags = flags & ~BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ;
      if ((flags & BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL) != 0)
        flags = flags & ~BZ_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ;

      if (strv_len > filesystems_hits)
        {
          const struct
          {
            const gchar         *prefix;
            BzFilesystemPathType type;
          } filesystems_other[] = {
            {                "/",      BZ_FILESYSTEM_PATH_SYSTEM_ROOT },
            {            "home/",      BZ_FILESYSTEM_PATH_HOME_SUBDIR },
            {               "~/",      BZ_FILESYSTEM_PATH_HOME_SUBDIR },
            {          "host-os",          BZ_FILESYSTEM_PATH_HOST_OS },
            {         "host-etc",         BZ_FILESYSTEM_PATH_HOST_ETC },
            {      "xdg-desktop",      BZ_FILESYSTEM_PATH_XDG_DESKTOP },
            {    "xdg-documents",    BZ_FILESYSTEM_PATH_XDG_DOCUMENTS },
            {        "xdg-music",        BZ_FILESYSTEM_PATH_XDG_MUSIC },
            {     "xdg-pictures",     BZ_FILESYSTEM_PATH_XDG_PICTURES },
            { "xdg-public-share", BZ_FILESYSTEM_PATH_XDG_PUBLIC_SHARE },
            {       "xdg-videos",       BZ_FILESYSTEM_PATH_XDG_VIDEOS },
            {    "xdg-templates",    BZ_FILESYSTEM_PATH_XDG_TEMPLATES },
            {        "xdg-cache",        BZ_FILESYSTEM_PATH_XDG_CACHE },
            {       "xdg-config",       BZ_FILESYSTEM_PATH_XDG_CONFIG },
            {         "xdg-data",         BZ_FILESYSTEM_PATH_XDG_DATA },
            {          "xdg-run",          BZ_FILESYSTEM_PATH_XDG_RUN }
          };

          flags |= BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_OTHER;

          for (guint j = 0; strv[j]; j++)
            {
              gchar       *perm        = strv[j];
              gboolean     is_readonly = FALSE;
              gchar       *colon       = NULL;
              guint        i           = 0;
              const gchar *subpath     = NULL;

              if (!perm[0])
                continue;

              is_readonly = g_str_has_suffix (perm, ":ro");
              colon       = strrchr (perm, ':');
              if (colon)
                *colon = '\0';

              for (i = 0; i < G_N_ELEMENTS (filesystems_other); i++)
                {
                  if (g_str_has_prefix (perm, filesystems_other[i].prefix))
                    {
                      const gchar *slash = NULL;

                      if (filesystems_other[i].type == BZ_FILESYSTEM_PATH_SYSTEM_ROOT)
                        {
                          if (perm[1] != '\0')
                            subpath = perm + 1;
                          else
                            subpath = NULL;
                        }
                      else
                        {
                          slash = strchr (perm, '/');
                          if (slash && slash != perm)
                            subpath = slash + 1;
                        }

                      if (is_readonly)
                        bz_app_permissions_add_filesystem_read (permissions, filesystems_other[i].type, subpath);
                      else
                        bz_app_permissions_add_filesystem_full (permissions, filesystems_other[i].type, subpath);
                      break;
                    }
                }

              if (i == G_N_ELEMENTS (filesystems_other))
                {
                  if (is_readonly)
                    bz_app_permissions_add_filesystem_read (permissions, BZ_FILESYSTEM_PATH_CUSTOM, perm);
                  else
                    bz_app_permissions_add_filesystem_full (permissions, BZ_FILESYSTEM_PATH_CUSTOM, perm);
                }
            }
        }
    }
  g_strfreev (strv);

  if (!(flags & (BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS | BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS)))
    {
      const struct
      {
        GBusType              bus_type;
        const char           *keyfile_group;
        BzAppPermissionsFlags unfiltered_flag;
      } bus_policy_types[] = {
        { G_BUS_TYPE_SESSION, "Session Bus Policy", BZ_APP_PERMISSIONS_FLAGS_SESSION_BUS },
        {  G_BUS_TYPE_SYSTEM,  "System Bus Policy",  BZ_APP_PERMISSIONS_FLAGS_SYSTEM_BUS },
      };

      if (app_id != NULL)
        {
          mpris_id           = g_strconcat ("org.mpris.MediaPlayer2.", app_id, NULL);
          app_id_non_devel   = g_str_has_suffix (app_id, ".Devel") ? g_strndup (app_id, strlen (app_id) - strlen (".Devel")) : NULL;
          mpris_id_non_devel = (app_id_non_devel != NULL) ? g_strconcat ("org.mpris.MediaPlayer2.", app_id_non_devel, NULL) : NULL;
        }

      for (size_t h = 0; h < G_N_ELEMENTS (bus_policy_types); h++)
        {
          g_auto (GStrv) bus_policies = NULL;

          if (flags & bus_policy_types[h].unfiltered_flag)
            continue;

          bus_policies = g_key_file_get_keys (keyfile, bus_policy_types[h].keyfile_group, NULL, NULL);

          for (size_t i = 0; bus_policies != NULL && bus_policies[i] != NULL; i++)
            {
              const struct
              {
                GBusType              bus_type;
                const char           *bus_name;
                gboolean              is_prefix;
                BzBusPolicyPermission permission_is_at_least;
                BzAppPermissionsFlags flags;
              } bus_policy_permissions[] = {
                { G_BUS_TYPE_SESSION,                              "ca.desrt.dconf", FALSE, BZ_BUS_POLICY_PERMISSION_TALK,        BZ_APP_PERMISSIONS_FLAGS_SETTINGS },
                { G_BUS_TYPE_SESSION,                     "org.freedesktop.Flatpak", FALSE, BZ_BUS_POLICY_PERMISSION_TALK,  BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },
                { G_BUS_TYPE_SESSION, "org.freedesktop.impl.portal.PermissionStore", FALSE, BZ_BUS_POLICY_PERMISSION_TALK,  BZ_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },
                { G_BUS_TYPE_SESSION,                                "org.gtk.vfs.",  TRUE, BZ_BUS_POLICY_PERMISSION_TALK, BZ_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
              };
              const char           *bus_name_pattern = bus_policies[i];
              g_autofree char      *bus_policy_str   = NULL;
              BzBusPolicyPermission bus_policy       = BZ_BUS_POLICY_PERMISSION_UNKNOWN;
              size_t                j                = 0;

              bus_policy_str = g_key_file_get_string (keyfile, bus_policy_types[h].keyfile_group, bus_name_pattern, NULL);

              g_assert (bus_policy_str != NULL);

              if (g_str_equal (bus_policy_str, "none"))
                bus_policy = BZ_BUS_POLICY_PERMISSION_NONE;
              else if (g_str_equal (bus_policy_str, "see"))
                bus_policy = BZ_BUS_POLICY_PERMISSION_SEE;
              else if (g_str_equal (bus_policy_str, "talk"))
                bus_policy = BZ_BUS_POLICY_PERMISSION_TALK;
              else if (g_str_equal (bus_policy_str, "own"))
                bus_policy = BZ_BUS_POLICY_PERMISSION_OWN;
              else
                continue;

              if (app_id != NULL &&
                  bus_policy_types[h].bus_type == G_BUS_TYPE_SESSION &&
                  (g_str_equal (bus_name_pattern, app_id) ||
                   (g_str_has_prefix (bus_name_pattern, app_id) && bus_name_pattern[strlen (app_id)] == '.') ||
                   g_str_equal (bus_name_pattern, mpris_id) ||
                   g_str_equal (bus_name_pattern, "org.freedesktop.DBus") ||
                   g_str_has_prefix (bus_name_pattern, "org.freedesktop.portal.")))
                continue;

              if (app_id_non_devel != NULL &&
                  bus_policy_types[h].bus_type == G_BUS_TYPE_SESSION &&
                  (g_str_equal (bus_name_pattern, app_id_non_devel) ||
                   (g_str_has_prefix (bus_name_pattern, app_id_non_devel) && bus_name_pattern[strlen (app_id_non_devel)] == '.') ||
                   g_str_equal (bus_name_pattern, mpris_id_non_devel)))
                continue;

              for (j = 0; j < G_N_ELEMENTS (bus_policy_permissions); j++)
                {
                  if (bus_policy_permissions[j].bus_type == bus_policy_types[h].bus_type &&
                      ((!bus_policy_permissions[j].is_prefix && g_str_equal (bus_name_pattern, bus_policy_permissions[j].bus_name)) ||
                       (bus_policy_permissions[j].is_prefix && g_str_has_prefix (bus_name_pattern, bus_policy_permissions[j].bus_name))) &&
                      bus_policy >= bus_policy_permissions[j].permission_is_at_least)
                    {
                      flags |= bus_policy_permissions[j].flags;
                      break;
                    }
                }

              if (j == G_N_ELEMENTS (bus_policy_permissions))
                {
                  flags |= BZ_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER;
                  bz_app_permissions_add_bus_policy (permissions,
                                                     bus_policy_types[h].bus_type,
                                                     bus_name_pattern,
                                                     bus_policy);
                }
            }
        }
    }

  bz_app_permissions_set_flags (permissions, flags);
  bz_app_permissions_seal (permissions);

  return permissions;
}

void
bz_app_permissions_serialize (BzAppPermissions *self,
                              GVariantBuilder  *builder)
{
  GFlagsClass *flags_class = NULL;
  guint        i           = 0;

  g_return_if_fail (BZ_IS_APP_PERMISSIONS (self));
  g_return_if_fail (builder != NULL);

  flags_class = g_type_class_ref (BZ_TYPE_APP_PERMISSIONS_FLAGS);

  if (self->flags != BZ_APP_PERMISSIONS_FLAGS_NONE)
    {
      g_autoptr (GVariantBuilder) sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));

      for (i = 0; i < flags_class->n_values; i++)
        {
          if (self->flags & flags_class->values[i].value)
            g_variant_builder_add (sub_builder, "s", flags_class->values[i].value_nick);
        }

      g_variant_builder_add (builder, "{sv}", "permissions-flags", g_variant_builder_end (sub_builder));
    }

  g_type_class_unref (flags_class);

  if (self->filesystem_read != NULL && self->filesystem_read->len > 0)
    {
      GEnumClass *enum_class                  = NULL;
      g_autoptr (GVariantBuilder) sub_builder = NULL;

      enum_class  = g_type_class_ref (BZ_TYPE_FILESYSTEM_PATH_TYPE);
      sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));

      for (i = 0; i < self->filesystem_read->len; i++)
        {
          const BzFilesystemPath *path       = g_ptr_array_index (self->filesystem_read, i);
          GEnumValue             *enum_value = g_enum_get_value (enum_class, path->type);

          g_variant_builder_add (sub_builder, "(ss)",
                                 enum_value->value_nick,
                                 path->subpath ? path->subpath : "");
        }

      g_variant_builder_add (builder, "{sv}", "permissions-filesystem-read", g_variant_builder_end (sub_builder));
      g_type_class_unref (enum_class);
    }

  if (self->filesystem_full != NULL && self->filesystem_full->len > 0)
    {
      GEnumClass *enum_class                  = NULL;
      g_autoptr (GVariantBuilder) sub_builder = NULL;

      enum_class  = g_type_class_ref (BZ_TYPE_FILESYSTEM_PATH_TYPE);
      sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(ss)"));

      for (i = 0; i < self->filesystem_full->len; i++)
        {
          const BzFilesystemPath *path       = g_ptr_array_index (self->filesystem_full, i);
          GEnumValue             *enum_value = g_enum_get_value (enum_class, path->type);

          g_variant_builder_add (sub_builder, "(ss)",
                                 enum_value->value_nick,
                                 path->subpath ? path->subpath : "");
        }

      g_variant_builder_add (builder, "{sv}", "permissions-filesystem-full", g_variant_builder_end (sub_builder));
      g_type_class_unref (enum_class);
    }

  if (self->bus_policies != NULL && self->bus_policies->len > 0)
    {
      GEnumClass *enum_class                  = NULL;
      g_autoptr (GVariantBuilder) sub_builder = NULL;

      enum_class  = g_type_class_ref (BZ_TYPE_BUS_POLICY_PERMISSION);
      sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("a(iss)"));

      for (i = 0; i < self->bus_policies->len; i++)
        {
          BzBusPolicy *policy     = g_ptr_array_index (self->bus_policies, i);
          GEnumValue  *enum_value = g_enum_get_value (enum_class, policy->permission);

          g_variant_builder_add (sub_builder, "(iss)",
                                 (gint32) policy->bus_type,
                                 policy->bus_name,
                                 enum_value->value_nick);
        }

      g_variant_builder_add (builder, "{sv}", "permissions-bus-policies", g_variant_builder_end (sub_builder));
      g_type_class_unref (enum_class);
    }
}

gboolean
bz_app_permissions_deserialize (BzAppPermissions *self,
                                GVariant         *import,
                                GError          **error)
{
  g_autoptr (GVariantIter) iter = NULL;

  g_return_val_if_fail (BZ_IS_APP_PERMISSIONS (self), FALSE);
  g_return_val_if_fail (import != NULL, FALSE);

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "permissions-flags") == 0)
        {
          GFlagsClass *flags_class            = NULL;
          g_autoptr (GVariantIter) flags_iter = NULL;

          flags_class = g_type_class_ref (BZ_TYPE_APP_PERMISSIONS_FLAGS);
          flags_iter  = g_variant_iter_new (value);

          self->flags = BZ_APP_PERMISSIONS_FLAGS_NONE;

          for (;;)
            {
              g_autofree char *flag_nick = NULL;

              if (!g_variant_iter_next (flags_iter, "s", &flag_nick))
                break;

              for (guint i = 0; i < flags_class->n_values; i++)
                {
                  if (g_str_equal (flag_nick, flags_class->values[i].value_nick))
                    {
                      self->flags |= flags_class->values[i].value;
                      break;
                    }
                }
            }

          g_type_class_unref (flags_class);
        }
      else if (g_strcmp0 (key, "permissions-filesystem-read") == 0)
        {
          GEnumClass *enum_class              = NULL;
          g_autoptr (GVariantIter) paths_iter = NULL;

          enum_class = g_type_class_ref (BZ_TYPE_FILESYSTEM_PATH_TYPE);

          if (self->filesystem_read == NULL)
            self->filesystem_read = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_filesystem_path_free);

          paths_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *type_nick = NULL;
              g_autofree char *subpath   = NULL;

              if (!g_variant_iter_next (paths_iter, "(ss)", &type_nick, &subpath))
                break;

              for (guint i = 0; i < enum_class->n_values; i++)
                {
                  if (g_str_equal (type_nick, enum_class->values[i].value_nick))
                    {
                      g_ptr_array_add (self->filesystem_read,
                                       bz_filesystem_path_new (enum_class->values[i].value,
                                                               subpath[0] ? subpath : NULL));
                      break;
                    }
                }
            }

          g_type_class_unref (enum_class);
        }
      else if (g_strcmp0 (key, "permissions-filesystem-full") == 0)
        {
          GEnumClass *enum_class              = NULL;
          g_autoptr (GVariantIter) paths_iter = NULL;

          enum_class = g_type_class_ref (BZ_TYPE_FILESYSTEM_PATH_TYPE);

          if (self->filesystem_full == NULL)
            self->filesystem_full = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_filesystem_path_free);

          paths_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *type_nick = NULL;
              g_autofree char *subpath   = NULL;

              if (!g_variant_iter_next (paths_iter, "(ss)", &type_nick, &subpath))
                break;

              for (guint i = 0; i < enum_class->n_values; i++)
                {
                  if (g_str_equal (type_nick, enum_class->values[i].value_nick))
                    {
                      g_ptr_array_add (self->filesystem_full,
                                       bz_filesystem_path_new (enum_class->values[i].value,
                                                               subpath[0] ? subpath : NULL));
                      break;
                    }
                }
            }

          g_type_class_unref (enum_class);
        }
      else if (g_strcmp0 (key, "permissions-bus-policies") == 0)
        {
          GEnumClass *enum_class                 = NULL;
          g_autoptr (GVariantIter) policies_iter = NULL;

          enum_class = g_type_class_ref (BZ_TYPE_BUS_POLICY_PERMISSION);

          if (self->bus_policies == NULL)
            self->bus_policies = g_ptr_array_new_with_free_func ((GDestroyNotify) bz_bus_policy_free);

          policies_iter = g_variant_iter_new (value);
          for (;;)
            {
              gint32           bus_type_int    = 0;
              g_autofree char *bus_name        = NULL;
              g_autofree char *permission_nick = NULL;

              if (!g_variant_iter_next (policies_iter, "(iss)", &bus_type_int, &bus_name, &permission_nick))
                break;

              for (guint i = 0; i < enum_class->n_values; i++)
                {
                  if (g_str_equal (permission_nick, enum_class->values[i].value_nick))
                    {
                      g_ptr_array_add (self->bus_policies,
                                       bz_bus_policy_new ((GBusType) bus_type_int,
                                                          bus_name,
                                                          enum_class->values[i].value));
                      break;
                    }
                }
            }

          g_type_class_unref (enum_class);
        }
    }

  bz_app_permissions_seal (self);
  return TRUE;
}

static gint
cmp_filesystem_path_pointers (gconstpointer item1,
                              gconstpointer item2)
{
  const BzFilesystemPath *const *pitem1 = item1;
  const BzFilesystemPath *const *pitem2 = item2;
  const BzFilesystemPath        *path1  = *pitem1;
  const BzFilesystemPath        *path2  = *pitem2;

  if (path1->type != path2->type)
    return path1->type - path2->type;

  return g_strcmp0 (path1->subpath, path2->subpath);
}

static int
cmp_bus_policy_qsort (const void *item1,
                      const void *item2)
{
  const BzBusPolicy *const *pitem1  = item1;
  const BzBusPolicy *const *pitem2  = item2;
  const BzBusPolicy        *policy1 = *pitem1;
  const BzBusPolicy        *policy2 = *pitem2;

  if (policy1->bus_type != policy2->bus_type)
    return policy1->bus_type - policy2->bus_type;

  return strcmp (policy1->bus_name, policy2->bus_name);
}

static guint
app_permissions_get_array_index (GPtrArray           *array,
                                 BzFilesystemPathType type,
                                 const char          *subpath)
{
  if (array == NULL)
    return DOES_NOT_CONTAIN;

  for (guint i = 0; i < array->len; i++)
    {
      const BzFilesystemPath *path = g_ptr_array_index (array, i);
      if (path->type == type && g_strcmp0 (path->subpath, subpath) == 0)
        return i;
    }

  return DOES_NOT_CONTAIN;
}

static guint
get_strv_index (const gchar *const *strv,
                const gchar        *value)
{
  guint ii;

  for (ii = 0; strv[ii]; ii++)
    {
      if (g_str_equal (strv[ii], value))
        break;
    }

  return ii;
}
