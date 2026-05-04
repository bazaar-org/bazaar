/* bz-gnome-extension-entry.c
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

#define G_LOG_DOMAIN  "BAZAAR::GNOME-EXT"
#define BAZAAR_MODULE "gnome-ext"

#include "config.h"

#include <appstream.h>

#include "bz-appstream-parser.h"
#include "bz-env.h"
#include "bz-gnome-extension-entry.h"
#include "bz-io.h"
#include "bz-serializable.h"
#include "bz-util.h"

struct _BzGnomeExtensionEntry
{
  BzEntry parent_instance;

  char   *uuid;
  int     pk;
  guint64 downloads;
};

static void serializable_iface_init (BzSerializableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzGnomeExtensionEntry,
    bz_gnome_extension_entry,
    BZ_TYPE_ENTRY,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_SERIALIZABLE, serializable_iface_init))

enum
{
  PROP_0,
  PROP_UUID,
  PROP_PK,
  PROP_DOWNLOADS,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void     bz_gnome_extension_entry_real_serialize (BzSerializable  *serializable,
                                                         GVariantBuilder *builder);
static gboolean bz_gnome_extension_entry_real_deserialize (BzSerializable *serializable,
                                                           GVariant       *import,
                                                           GError        **error);

static void
bz_gnome_extension_entry_dispose (GObject *object)
{
  BzGnomeExtensionEntry *self = BZ_GNOME_EXTENSION_ENTRY (object);

  g_clear_pointer (&self->uuid, g_free);

  G_OBJECT_CLASS (bz_gnome_extension_entry_parent_class)->dispose (object);
}

static void
bz_gnome_extension_entry_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  BzGnomeExtensionEntry *self = BZ_GNOME_EXTENSION_ENTRY (object);

  switch (prop_id)
    {
    case PROP_UUID:
      g_value_set_string (value, self->uuid);
      break;
    case PROP_PK:
      g_value_set_int (value, self->pk);
      break;
    case PROP_DOWNLOADS:
      g_value_set_uint64 (value, self->downloads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_gnome_extension_entry_class_init (BzGnomeExtensionEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose      = bz_gnome_extension_entry_dispose;
  object_class->get_property = bz_gnome_extension_entry_get_property;

  props[PROP_UUID] =
      g_param_spec_string (
          "uuid", NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_PK] =
      g_param_spec_int (
          "pk", NULL, NULL,
          0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_DOWNLOADS] =
      g_param_spec_uint64 (
          "downloads", NULL, NULL,
          0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_gnome_extension_entry_init (BzGnomeExtensionEntry *self)
{
}

static void
serializable_iface_init (BzSerializableInterface *iface)
{
  iface->serialize   = bz_gnome_extension_entry_real_serialize;
  iface->deserialize = bz_gnome_extension_entry_real_deserialize;
}

BzGnomeExtensionEntry *
bz_gnome_extension_entry_new_from_component (AsComponent *component,
                                             GError     **error)
{
  g_autoptr (BzGnomeExtensionEntry) self = NULL;
  g_autofree char *module_dir            = NULL;
  g_autofree char *unique_id             = NULL;
  g_autofree char *unique_id_checksum    = NULL;
  const char      *id                    = NULL;
  const char      *uuid                  = NULL;
  int              pk                    = 0;
  guint64          downloads             = 0;
  guint64          size                  = 0;
  GHashTable      *custom                = NULL;
  gboolean         result                = FALSE;

  g_return_val_if_fail (AS_IS_COMPONENT (component), NULL);

  id = as_component_get_id (component);
  if (id == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Extension component has no id");
      return NULL;
    }

  custom = as_component_get_custom (component);
  if (custom != NULL)
    {
      const char *pk_str        = NULL;
      const char *uuid_str      = NULL;
      const char *downloads_str = NULL;
      const char *size_str      = NULL;

      pk_str        = g_hash_table_lookup (custom, "GnomeExtensions::pk");
      uuid_str      = g_hash_table_lookup (custom, "GnomeExtensions::uuid");
      downloads_str = g_hash_table_lookup (custom, "GnomeExtensions::downloads");
      size_str      = g_hash_table_lookup (custom, "GnomeExtensions::size");

      if (pk_str != NULL)
        pk = (int) g_ascii_strtoll (pk_str, NULL, 10);
      if (uuid_str != NULL)
        uuid = uuid_str;
      if (downloads_str != NULL)
        downloads = g_ascii_strtoull (downloads_str, NULL, 10);
      if (size_str != NULL)
        size = g_ascii_strtoull (size_str, NULL, 10);
    }

  if (uuid == NULL)
    uuid = id;

  self            = g_object_new (BZ_TYPE_GNOME_EXTENSION_ENTRY, NULL);
  self->uuid      = g_strdup (uuid);
  self->pk        = pk;
  self->downloads = downloads;

  unique_id          = g_strdup_printf ("GNOME-EXT::%s", uuid);
  unique_id_checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, unique_id, -1);
  module_dir         = bz_dup_module_dir ();

  g_object_set (
      self,
      "kinds", (guint) BZ_ENTRY_KIND_APPLICATION,
      "id", id,
      "unique-id", unique_id,
      "unique-id-checksum", unique_id_checksum,
      "remote-repo-name", "gnome-extensions",
      "searchable", TRUE,
      "size", size,
      "installed-size", size,
      NULL);

  result = bz_appstream_parser_populate_entry (
      BZ_ENTRY (self),
      component,
      NULL,
      "gnome-extensions",
      module_dir,
      unique_id_checksum,
      id,
      BZ_ENTRY_KIND_APPLICATION,
      error);
  if (!result)
    return NULL;

  g_object_set (
      self,
      "kinds", (guint) BZ_ENTRY_KIND_APPLICATION,
      "id", id,
      "unique-id", unique_id,
      "unique-id-checksum", unique_id_checksum,
      "remote-repo-name", "gnome-extensions",
      "searchable", TRUE,
      "size", size,
      "installed-size", size,
      NULL);

  return g_steal_pointer (&self);
}

const char *
bz_gnome_extension_entry_get_uuid (BzGnomeExtensionEntry *self)
{
  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (self), NULL);
  return self->uuid;
}

int
bz_gnome_extension_entry_get_pk (BzGnomeExtensionEntry *self)
{
  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (self), 0);
  return self->pk;
}

guint64
bz_gnome_extension_entry_get_downloads (BzGnomeExtensionEntry *self)
{
  g_return_val_if_fail (BZ_IS_GNOME_EXTENSION_ENTRY (self), 0);
  return self->downloads;
}

char *
bz_gnome_extension_entry_uuid_to_id (const char *uuid)
{
  GString         *buf  = NULL;
  g_autofree char *safe = NULL;

  g_return_val_if_fail (uuid != NULL, NULL);

  buf = g_string_new (NULL);
  for (const char *p = uuid; *p != '\0'; p++)
    {
      char c = *p;
      if (g_ascii_isalnum (c) || c == '.' || c == '_' || c == '-')
        g_string_append_c (buf, c);
      else
        g_string_append_c (buf, '-');
    }

  safe = g_string_free (buf, FALSE);
  return g_strdup_printf ("org.gnome.shell.extensions.%s", safe);
}

static void
bz_gnome_extension_entry_real_serialize (BzSerializable  *serializable,
                                         GVariantBuilder *builder)
{
  BzGnomeExtensionEntry *self = BZ_GNOME_EXTENSION_ENTRY (serializable);

  if (self->uuid != NULL)
    g_variant_builder_add (builder, "{sv}", "uuid", g_variant_new_string (self->uuid));
  g_variant_builder_add (builder, "{sv}", "pk", g_variant_new_int32 (self->pk));
  g_variant_builder_add (builder, "{sv}", "downloads", g_variant_new_uint64 (self->downloads));

  bz_entry_serialize (BZ_ENTRY (self), builder);
}

static gboolean
bz_gnome_extension_entry_real_deserialize (BzSerializable *serializable,
                                           GVariant       *import,
                                           GError        **error)
{
  BzGnomeExtensionEntry *self   = BZ_GNOME_EXTENSION_ENTRY (serializable);
  g_autoptr (GVariantIter) iter = NULL;

  g_clear_pointer (&self->uuid, g_free);
  self->downloads = 0;

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "uuid") == 0)
        self->uuid = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "pk") == 0)
        self->pk = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "downloads") == 0)
        self->downloads = g_variant_get_uint64 (value);
    }

  return bz_entry_deserialize (BZ_ENTRY (self), import, error);
}
