/* bz-flathub-category.c
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

#include <glib/gi18n.h>

#include "appstream.h"
#include "bz-flathub-category.h"
#include "bz-flathub-sub-category.h"
#include "bz-serializable.h"

struct _BzFlathubCategory
{
  GObject parent_instance;

  BzApplicationMapFactory *map_factory;
  char                    *name;
  GListModel              *applications;
  GListModel              *quality_applications;
  int                      total_entries;
  gboolean                 is_spotlight;
  GListModel              *subcategories;
};

static void
serializable_iface_init (BzSerializableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzFlathubCategory,
    bz_flathub_category,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_SERIALIZABLE, serializable_iface_init));

enum
{
  PROP_0,

  PROP_MAP_FACTORY,
  PROP_NAME,
  PROP_DISPLAY_NAME,
  PROP_SHORT_NAME,
  PROP_IS_XDG,
  PROP_SYMBOLIC_ICON_NAME,
  PROP_ICON_NAME,
  PROP_APPLICATIONS,
  PROP_QUALITY_APPLICATIONS,
  PROP_TOTAL_ENTRIES,
  PROP_IS_SPOTLIGHT,
  PROP_SUBCATEGORIES,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
clear (BzFlathubCategory *self);

typedef struct
{
  const char *id;
  const char *display_name;
  const char *short_name;
  const char *more_of_name;
  gboolean    is_xdg;
  const char *icon_name;
  const char *symbolic_icon_name;
  const void *subcategories;
} CategoryInfo;

typedef struct
{
  const char *display_name;
  const char *category_id;
} Subcategory;

static const Subcategory audiovideo_subcategories[] = {
  {   N_ ("Editing"), "AudioVideoEditing" },
  {      N_ ("Midi"),              "Midi" },
  {     N_ ("Mixer"),             "Mixer" },
  {     N_ ("Music"),             "Music" },
  {    N_ ("Player"),            "Player" },
  {  N_ ("Recorder"),          "Recorder" },
  { N_ ("Sequencer"),         "Sequencer" },
  {     N_ ("Tuner"),             "Tuner" },
  {        N_ ("TV"),                "TV" },
  {             NULL,                NULL }
};

static const Subcategory game_subcategories[] = {
  {    N_ ("Emulation"),      "Emulator" },
  {       N_ ("Action"),    "ActionGame" },
  {    N_ ("Adventure"), "AdventureGame" },
  {       N_ ("Arcade"),    "ArcadeGame" },
  {       N_ ("Blocks"),    "BlocksGame" },
  {        N_ ("Board"),     "BoardGame" },
  {         N_ ("Card"),      "CardGame" },
  {         N_ ("Kids"),      "KidsGame" },
  {        N_ ("Logic"),     "LogicGame" },
  { N_ ("Role Playing"),   "RolePlaying" },
  {      N_ ("Shooter"),       "Shooter" },
  {   N_ ("Simulation"),    "Simulation" },
  {       N_ ("Sports"),    "SportsGame" },
  {     N_ ("Strategy"),  "StrategyGame" },
  {                NULL,            NULL }
};

static const Subcategory network_subcategories[] = {
  {     N_ ("Browsers"), "WebBrowser"  },
  {         N_ ("Chat"), "Chat"        },
  {         N_ ("Mail"), "Email"       },
  { N_ ("File Sharing"), "FileTransfer"},
  {               NULL,  NULL          }
};

static const CategoryInfo category_info[] = {
  {       "audiovideo",          N_ ("Audio & Video"),    N_ ("Media"),          N_ ("More Audio & Video"),  TRUE, "io.github.kolumni.Bazaar.Audiovideo", "applications-multimedia-symbolic", audiovideo_subcategories },
  {      "development",        N_ ("Developer Tools"),  N_ ("Develop"),        N_ ("More Developer Tools"),  TRUE,    "io.github.kolumni.Bazaar.Develop",                    "code-symbolic",                     NULL },
  {        "education",              N_ ("Education"),    N_ ("Learn"),              N_ ("More Education"),  TRUE,      "io.github.kolumni.Bazaar.Learn",               "open-book-symbolic",                     NULL },
  {             "game",                 N_ ("Gaming"),     N_ ("Play"),                 N_ ("More Gaming"),  TRUE,       "io.github.kolumni.Bazaar.Play",      "applications-games-symbolic",       game_subcategories },
  {         "graphics", N_ ("Graphics & Photography"),   N_ ("Create"), N_ ("More Graphics & Photography"),  TRUE,     "io.github.kolumni.Bazaar.Create",              "paintbrush-symbolic",                     NULL },
  {          "network",             N_ ("Networking"), N_ ("Internet"),             N_ ("More Networking"),  TRUE,    "io.github.kolumni.Bazaar.Network",                   "globe-symbolic",    network_subcategories },
  {           "office",           N_ ("Productivity"),     N_ ("Work"),           N_ ("More Productivity"),  TRUE,       "io.github.kolumni.Bazaar.Work",                 "meeting-symbolic",                     NULL },
  {          "science",                N_ ("Science"),  N_ ("Science"),                N_ ("More Science"),  TRUE,    "io.github.kolumni.Bazaar.Science",    "applications-science-symbolic",                     NULL },
  {           "system",                 N_ ("System"),   N_ ("System"),                 N_ ("More System"),  TRUE,     "io.github.kolumni.Bazaar.System",     "applications-system-symbolic",                     NULL },
  {          "utility",              N_ ("Utilities"),    N_ ("Tools"),              N_ ("More Utilities"),  TRUE,  "io.github.kolumni.Bazaar.Utilities",  "applications-utilities-symbolic",                     NULL },
  {         "trending",               N_ ("Trending"), N_ ("Trending"),               N_ ("More Trending"), FALSE,   "io.github.kolumni.Bazaar.Trending",                                 "",                     NULL },
  {          "popular",                N_ ("Popular"),  N_ ("Popular"),                N_ ("More Popular"), FALSE,    "io.github.kolumni.Bazaar.Popular",                                 "",                     NULL },
  {   "recently-added",         N_ ("Recently Added"),      N_ ("New"),                    N_ ("More New"), FALSE,        "io.github.kolumni.Bazaar.New",                                 "",                     NULL },
  { "recently-updated",       N_ ("Recently Updated"),  N_ ("Updated"),                N_ ("More Updated"), FALSE,    "io.github.kolumni.Bazaar.Updated",                                 "",                     NULL },
  {           "mobile",                 N_ ("Mobile"),   N_ ("Mobile"),                 N_ ("More Mobile"), FALSE,     "io.github.kolumni.Bazaar.Mobile",                                 "",                     NULL },
  {          "adwaita",                N_ ("Adwaita"),  N_ ("Adwaita"),                N_ ("More Adwaita"), FALSE,    "io.github.kolumni.Bazaar.Adwaita",                                 "",                     NULL },
  {              "kde",               N_ ("KDE Apps"), N_ ("KDE Apps"),               N_ ("More KDE Apps"), FALSE,        "io.github.kolumni.Bazaar.Kde",                                 "",                     NULL },
  {               NULL,                          NULL,            NULL,                               NULL, FALSE,                                  NULL,                               NULL,                     NULL }
};

static const CategoryInfo *
get_category_info (const char *category_id)
{
  for (int i = 0; category_info[i].id != NULL; i++)
    {
      if (g_strcmp0 (category_info[i].id, category_id) == 0)
        return &category_info[i];
    }
  return NULL;
}

static GListModel *
create_subcategories (const Subcategory *subcategories)
{
  GListStore *store;
  store = g_list_store_new (BZ_TYPE_FLATHUB_SUB_CATEGORY);

  for (gsize i = 0; subcategories[i].display_name != NULL; i++)
    g_list_store_append (store, g_object_new (BZ_TYPE_FLATHUB_SUB_CATEGORY,
                                              "name", _ (subcategories[i].display_name),
                                              "id", subcategories[i].category_id,
                                              NULL));

  return G_LIST_MODEL (store);
}

static void
bz_flathub_category_dispose (GObject *object)
{
  BzFlathubCategory *self = BZ_FLATHUB_CATEGORY (object);

  clear (self);

  G_OBJECT_CLASS (bz_flathub_category_parent_class)->dispose (object);
}

static void
bz_flathub_category_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BzFlathubCategory *self = BZ_FLATHUB_CATEGORY (object);

  switch (prop_id)
    {
    case PROP_MAP_FACTORY:
      g_value_set_object (value, bz_flathub_category_get_map_factory (self));
      break;
    case PROP_NAME:
      g_value_set_string (value, bz_flathub_category_get_name (self));
      break;
    case PROP_APPLICATIONS:
      g_value_take_object (value, bz_flathub_category_dup_applications (self));
      break;
    case PROP_QUALITY_APPLICATIONS:
      g_value_take_object (value, bz_flathub_category_dup_quality_applications (self));
      break;
    case PROP_DISPLAY_NAME:
      g_value_set_string (value, bz_flathub_category_get_display_name (self));
      break;
    case PROP_SHORT_NAME:
      g_value_set_string (value, bz_flathub_category_get_short_name (self));
      break;
    case PROP_IS_XDG:
      g_value_set_boolean (value, bz_flathub_category_get_is_xdg (self));
      break;
    case PROP_SYMBOLIC_ICON_NAME:
      g_value_set_string (value, bz_flathub_category_get_symbolic_icon_name (self));
      break;
    case PROP_ICON_NAME:
      g_value_set_string (value, bz_flathub_category_get_icon_name (self));
      break;
    case PROP_TOTAL_ENTRIES:
      g_value_set_int (value, bz_flathub_category_get_total_entries (self));
      break;
    case PROP_IS_SPOTLIGHT:
      g_value_set_boolean (value, bz_flathub_category_get_is_spotlight (self));
      break;
    case PROP_SUBCATEGORIES:
      g_value_set_object (value, bz_flathub_category_get_subcategories (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flathub_category_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BzFlathubCategory *self = BZ_FLATHUB_CATEGORY (object);

  switch (prop_id)
    {
    case PROP_MAP_FACTORY:
      bz_flathub_category_set_map_factory (self, g_value_get_object (value));
      break;
    case PROP_NAME:
      bz_flathub_category_set_name (self, g_value_get_string (value));
      break;
    case PROP_APPLICATIONS:
      bz_flathub_category_set_applications (self, g_value_get_object (value));
      break;
    case PROP_QUALITY_APPLICATIONS:
      bz_flathub_category_set_quality_applications (self, g_value_get_object (value));
      break;
    case PROP_TOTAL_ENTRIES:
      bz_flathub_category_set_total_entries (self, g_value_get_int (value));
      break;
    case PROP_IS_SPOTLIGHT:
      bz_flathub_category_set_is_spotlight (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_flathub_category_class_init (BzFlathubCategoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_flathub_category_set_property;
  object_class->get_property = bz_flathub_category_get_property;
  object_class->dispose      = bz_flathub_category_dispose;

  props[PROP_MAP_FACTORY] =
      g_param_spec_object (
          "map-factory",
          NULL, NULL,
          BZ_TYPE_APPLICATION_MAP_FACTORY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_NAME] =
      g_param_spec_string (
          "name",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DISPLAY_NAME] =
      g_param_spec_string (
          "display-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_SHORT_NAME] =
      g_param_spec_string (
          "short-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_IS_XDG] =
      g_param_spec_boolean (
          "is-xdg",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_SYMBOLIC_ICON_NAME] =
      g_param_spec_string (
          "symbolic-icon-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_ICON_NAME] =
      g_param_spec_string (
          "icon-name",
          NULL, NULL, NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  props[PROP_APPLICATIONS] =
      g_param_spec_object (
          "applications",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_QUALITY_APPLICATIONS] =
      g_param_spec_object (
          "quality-applications",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TOTAL_ENTRIES] =
      g_param_spec_int (
          "total-entries",
          NULL, NULL,
          0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_IS_SPOTLIGHT] =
      g_param_spec_boolean (
          "is-spotlight",
          NULL, NULL,
          FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_SUBCATEGORIES] =
      g_param_spec_object (
          "subcategories",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_flathub_category_init (BzFlathubCategory *self)
{
}

static void
bz_flathub_category_real_serialize (BzSerializable  *serializable,
                                    GVariantBuilder *builder)
{
  BzFlathubCategory *self = BZ_FLATHUB_CATEGORY (serializable);

  if (self->name != NULL)
    g_variant_builder_add (builder, "{sv}", "name", g_variant_new_string (self->name));
  if (self->applications != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (self->applications);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;

              string = g_list_model_get_item (self->applications, i);
              g_variant_builder_add (sub_builder, "s", gtk_string_object_get_string (string));
            }

          g_variant_builder_add (builder, "{sv}", "applications", g_variant_builder_end (sub_builder));
        }
    }
  if (self->quality_applications != NULL)
    {
      guint n_items = 0;

      n_items = g_list_model_get_n_items (self->quality_applications);
      if (n_items > 0)
        {
          g_autoptr (GVariantBuilder) sub_builder = NULL;

          sub_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
          for (guint i = 0; i < n_items; i++)
            {
              g_autoptr (GtkStringObject) string = NULL;

              string = g_list_model_get_item (self->quality_applications, i);
              g_variant_builder_add (sub_builder, "s", gtk_string_object_get_string (string));
            }

          g_variant_builder_add (builder, "{sv}", "quality-applications", g_variant_builder_end (sub_builder));
        }
    }
  g_variant_builder_add (builder, "{sv}", "total-entries", g_variant_new_int32 (self->total_entries));
  g_variant_builder_add (builder, "{sv}", "is-spotlight", g_variant_new_boolean (self->is_spotlight));
}

static gboolean
bz_flathub_category_real_deserialize (BzSerializable *serializable,
                                      GVariant       *import,
                                      GError        **error)
{
  BzFlathubCategory *self       = BZ_FLATHUB_CATEGORY (serializable);
  g_autoptr (GVariantIter) iter = NULL;

  clear (self);

  iter = g_variant_iter_new (import);
  for (;;)
    {
      g_autofree char *key       = NULL;
      g_autoptr (GVariant) value = NULL;

      /* TODO automate this, this is awful */
      if (!g_variant_iter_next (iter, "{sv}", &key, &value))
        break;

      if (g_strcmp0 (key, "name") == 0)
        bz_flathub_category_set_name (self, g_variant_get_string (value, NULL));
      else if (g_strcmp0 (key, "applications") == 0)
        {
          g_autoptr (GtkStringList) list     = NULL;
          g_autoptr (GVariantIter) list_iter = NULL;

          list = gtk_string_list_new (NULL);

          list_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *id = NULL;

              if (!g_variant_iter_next (list_iter, "s", &id))
                break;
              gtk_string_list_append (list, id);
            }

          self->applications = (GListModel *) g_steal_pointer (&list);
        }
      else if (g_strcmp0 (key, "quality-applications") == 0)
        {
          g_autoptr (GtkStringList) list     = NULL;
          g_autoptr (GVariantIter) list_iter = NULL;

          list = gtk_string_list_new (NULL);

          list_iter = g_variant_iter_new (value);
          for (;;)
            {
              g_autofree char *id = NULL;

              if (!g_variant_iter_next (list_iter, "s", &id))
                break;
              gtk_string_list_append (list, id);
            }

          self->quality_applications = (GListModel *) g_steal_pointer (&list);
        }
      else if (g_strcmp0 (key, "total-entries") == 0)
        self->total_entries = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "is-spotlight") == 0)
        self->is_spotlight = g_variant_get_boolean (value);
    }

  return TRUE;
}

static void
serializable_iface_init (BzSerializableInterface *iface)
{
  iface->serialize   = bz_flathub_category_real_serialize;
  iface->deserialize = bz_flathub_category_real_deserialize;
}

BzFlathubCategory *
bz_flathub_category_new (void)
{
  return g_object_new (BZ_TYPE_FLATHUB_CATEGORY, NULL);
}

BzApplicationMapFactory *
bz_flathub_category_get_map_factory (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);
  return self->map_factory;
}

const char *
bz_flathub_category_get_name (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);
  return self->name;
}

GListModel *
bz_flathub_category_dup_applications (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  if (self->applications != NULL)
    {
      if (self->map_factory != NULL)
        return bz_application_map_factory_generate (
            self->map_factory, G_LIST_MODEL (self->applications));
      else
        return G_LIST_MODEL (g_object_ref (self->applications));
    }
  else
    return NULL;
}

GListModel *
bz_flathub_category_dup_quality_applications (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  if (self->quality_applications != NULL)
    {
      if (self->map_factory != NULL)
        return bz_application_map_factory_generate (
            self->map_factory, G_LIST_MODEL (self->quality_applications));
      else
        return G_LIST_MODEL (g_object_ref (self->quality_applications));
    }
  else
    return NULL;
}

int
bz_flathub_category_get_total_entries (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), 0);
  return self->total_entries;
}

gboolean
bz_flathub_category_get_is_spotlight (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), FALSE);
  return self->is_spotlight;
}

void
bz_flathub_category_set_map_factory (BzFlathubCategory       *self,
                                     BzApplicationMapFactory *map_factory)
{
  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));
  g_return_if_fail (map_factory == NULL || BZ_IS_APPLICATION_MAP_FACTORY (map_factory));

  g_clear_object (&self->map_factory);
  if (map_factory != NULL)
    self->map_factory = g_object_ref (map_factory);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MAP_FACTORY]);
}

void
bz_flathub_category_set_name (BzFlathubCategory *self,
                              const char        *name)
{
  const CategoryInfo *info;

  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));

  g_clear_pointer (&self->name, g_free);

  self->name = g_strdup (name);
  info       = get_category_info (name);

  if (info != NULL && info->subcategories != NULL)
    {
      g_clear_object (&self->subcategories);
      self->subcategories = create_subcategories ((const Subcategory *) info->subcategories);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SUBCATEGORIES]);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NAME]);
}

void
bz_flathub_category_set_applications (BzFlathubCategory *self,
                                      GListModel        *applications)
{
  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));

  g_clear_pointer (&self->applications, g_object_unref);
  if (applications != NULL)
    self->applications = g_object_ref (applications);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APPLICATIONS]);
}

void
bz_flathub_category_set_quality_applications (BzFlathubCategory *self,
                                              GListModel        *applications)
{
  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));

  g_clear_pointer (&self->quality_applications, g_object_unref);
  if (applications != NULL)
    self->quality_applications = g_object_ref (applications);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_QUALITY_APPLICATIONS]);
}

void
bz_flathub_category_set_total_entries (BzFlathubCategory *self,
                                       int                total_entries)
{
  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));

  if (self->total_entries == total_entries)
    return;

  self->total_entries = total_entries;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TOTAL_ENTRIES]);
}

void
bz_flathub_category_set_is_spotlight (BzFlathubCategory *self,
                                      gboolean           is_spotlight)
{
  g_return_if_fail (BZ_IS_FLATHUB_CATEGORY (self));

  if (self->is_spotlight == is_spotlight)
    return;

  self->is_spotlight = is_spotlight;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_SPOTLIGHT]);
}

const char *
bz_flathub_category_get_display_name (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  info = get_category_info (self->name);
  return info ? _ (info->display_name) : self->name;
}

const char *
bz_flathub_category_get_short_name (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  info = get_category_info (self->name);
  return info ? _ (info->short_name) : self->name;
}

const char *
bz_flathub_category_get_more_of_name (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  info = get_category_info (self->name);
  return info ? _ (info->more_of_name) : self->name;
}

gboolean
bz_flathub_category_get_is_xdg (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), FALSE);

  info = get_category_info (self->name);
  return info ? info->is_xdg : FALSE;
}

const char *
bz_flathub_category_get_symbolic_icon_name (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  info = get_category_info (self->name);
  return info ? info->symbolic_icon_name : NULL;
}

const char *
bz_flathub_category_get_icon_name (BzFlathubCategory *self)
{
  const CategoryInfo *info;

  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);

  info = get_category_info (self->name);
  return info ? info->icon_name : NULL;
}

GListModel *
bz_flathub_category_get_subcategories (BzFlathubCategory *self)
{
  g_return_val_if_fail (BZ_IS_FLATHUB_CATEGORY (self), NULL);
  return self->subcategories;
}

static void
clear (BzFlathubCategory *self)
{
  g_clear_pointer (&self->map_factory, g_object_unref);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->applications, g_object_unref);
  g_clear_pointer (&self->quality_applications, g_object_unref);
  g_clear_object (&self->subcategories);
}

static const char *
bz_flathub_category_map_appstream_id (const char *as_category_id)
{
  g_autofree char    *lowercase = NULL;
  const CategoryInfo *info      = NULL;

  g_return_val_if_fail (as_category_id != NULL, NULL);

  lowercase = g_ascii_strdown (as_category_id, -1);
  info      = get_category_info (lowercase);

  return info ? info->id : NULL;
}

GListModel *
bz_flathub_category_list_from_appstream (GPtrArray *as_categories)
{
  g_autoptr (GListStore) categories = NULL;

  g_return_val_if_fail (as_categories != NULL, NULL);

  if (as_categories->len == 0)
    return NULL;

  categories = g_list_store_new (BZ_TYPE_FLATHUB_CATEGORY);

  for (guint i = 0; i < as_categories->len; i++)
    {
      const char *category_id = NULL;
      const char *mapped_id   = NULL;

      category_id = (const char *) g_ptr_array_index (as_categories, i);

      if (category_id == NULL)
        continue;

      mapped_id = bz_flathub_category_map_appstream_id (category_id);

      if (mapped_id != NULL)
        {
          g_autoptr (BzFlathubCategory) category = NULL;

          category = bz_flathub_category_new ();
          bz_flathub_category_set_name (category, mapped_id);
          g_list_store_append (categories, category);
        }
    }

  if (g_list_model_get_n_items (G_LIST_MODEL (categories)) == 0)
    return NULL;

  return G_LIST_MODEL (g_steal_pointer (&categories));
}

/* End of bz-flathub-category.c */
