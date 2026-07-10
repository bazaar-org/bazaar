/* bz-entry-selection-row.c
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

#include <glib/gi18n.h>

#include "bz-entry-selection-row.h"

#include "bz-flatpak-entry.h"
#include "template-callbacks.h"

struct _BzEntrySelectionRow
{
  AdwActionRow parent_instance;

  GtkCheckButton *radio;

  BzFlatpakEntry *entry;
  BzRepository   *repository;
};

G_DEFINE_FINAL_TYPE (BzEntrySelectionRow, bz_entry_selection_row, ADW_TYPE_ACTION_ROW)

enum
{
  PROP_0,
  PROP_ENTRY,
  PROP_REPOSITORY,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_entry_selection_row_dispose (GObject *object)
{
  BzEntrySelectionRow *self = BZ_ENTRY_SELECTION_ROW (object);

  g_clear_object (&self->entry);
  g_clear_object (&self->repository);

  G_OBJECT_CLASS (bz_entry_selection_row_parent_class)->dispose (object);
}

static void
bz_entry_selection_row_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  BzEntrySelectionRow *self = BZ_ENTRY_SELECTION_ROW (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_value_set_object (value, self->entry);
      break;
    case PROP_REPOSITORY:
      g_value_set_object (value, self->repository);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_entry_selection_row_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  BzEntrySelectionRow *self = BZ_ENTRY_SELECTION_ROW (object);

  switch (prop_id)
    {
    case PROP_ENTRY:
      g_clear_object (&self->entry);
      self->entry = g_value_dup_object (value);
      break;
    case PROP_REPOSITORY:
      g_clear_object (&self->repository);
      self->repository = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static char *
get_subtitle (gpointer    object,
              const char *version,
              gboolean    is_user)
{
  const char *scope = is_user ? _ ("this user") : _ ("all users");
  return g_strdup_printf ("%s • %s", version, scope);
}

static void
bz_entry_selection_row_class_init (BzEntrySelectionRowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_entry_selection_row_dispose;
  object_class->get_property = bz_entry_selection_row_get_property;
  object_class->set_property = bz_entry_selection_row_set_property;

  props[PROP_ENTRY] =
      g_param_spec_object (
          "entry",
          NULL, NULL,
          BZ_TYPE_FLATPAK_ENTRY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_REPOSITORY] =
      g_param_spec_object (
          "repository",
          NULL, NULL,
          BZ_TYPE_REPOSITORY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BZ_TYPE_REPOSITORY);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-entry-selection-row.ui");
  bz_widget_class_bind_all_util_callbacks (widget_class);
  gtk_widget_class_bind_template_callback (widget_class, get_subtitle);

  gtk_widget_class_bind_template_child (widget_class, BzEntrySelectionRow, radio);
}

static void
bz_entry_selection_row_init (BzEntrySelectionRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

BzEntrySelectionRow *
bz_entry_selection_row_new (BzFlatpakEntry *entry,
                            BzRepository   *repository)
{
  g_return_val_if_fail (BZ_IS_ENTRY (entry), NULL);

  return g_object_new (BZ_TYPE_ENTRY_SELECTION_ROW,
                       "entry", entry,
                       "repository", repository,
                       NULL);
}

GtkCheckButton *
bz_entry_selection_row_get_radio (BzEntrySelectionRow *self)
{
  g_return_val_if_fail (BZ_IS_ENTRY_SELECTION_ROW (self), NULL);
  return self->radio;
}
