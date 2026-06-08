/* bz-lozenge.c
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

#include <gtk/gtk.h>

#include "bz-context-row.h"
#include "bz-lozenge.h"

struct _BzLozenge
{
  GtkBox parent_instance;

  gchar       *title;
  gchar       *label;
  gchar      **icon_names;
  BzImportance importance;

  GtkWidget *icon_box;
  GtkWidget *label_widget;
  GtkWidget *title_label;
};

G_DEFINE_FINAL_TYPE (BzLozenge, bz_lozenge, GTK_TYPE_BOX)

enum
{
  PROP_0,
  PROP_TITLE,
  PROP_LABEL,
  PROP_ICON_NAMES,
  PROP_IMPORTANCE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP] = { NULL };

static void bz_lozenge_rebuild (BzLozenge *self);

static void
bz_lozenge_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BzLozenge *self = NULL;

  self = BZ_LOZENGE (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;
    case PROP_LABEL:
      g_value_set_string (value, self->label);
      break;
    case PROP_ICON_NAMES:
      g_value_set_boxed (value, self->icon_names);
      break;
    case PROP_IMPORTANCE:
      g_value_set_enum (value, self->importance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_lozenge_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  BzLozenge *self = NULL;

  self = BZ_LOZENGE (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      bz_lozenge_set_title (self, g_value_get_string (value));
      break;
    case PROP_LABEL:
      bz_lozenge_set_label (self, g_value_get_string (value));
      break;
    case PROP_ICON_NAMES:
      bz_lozenge_set_icon_names (self, g_value_get_boxed (value));
      break;
    case PROP_IMPORTANCE:
      bz_lozenge_set_importance (self, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_lozenge_dispose (GObject *object)
{
  BzLozenge *self = NULL;

  self = BZ_LOZENGE (object);

  g_clear_pointer (&self->title, g_free);
  g_clear_pointer (&self->label, g_free);
  g_clear_pointer (&self->icon_names, g_strfreev);

  g_clear_pointer (&self->icon_box, gtk_widget_unparent);
  g_clear_pointer (&self->label_widget, gtk_widget_unparent);
  g_clear_pointer (&self->title_label, gtk_widget_unparent);

  G_OBJECT_CLASS (bz_lozenge_parent_class)->dispose (object);
}

static void
bz_lozenge_class_init (BzLozengeClass *klass)
{
  GObjectClass *object_class = NULL;

  object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = bz_lozenge_get_property;
  object_class->set_property = bz_lozenge_set_property;
  object_class->dispose      = bz_lozenge_dispose;

  props[PROP_TITLE] =
      g_param_spec_string ("title",
                           NULL, NULL,
                           NULL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_LABEL] =
      g_param_spec_string ("label",
                           NULL, NULL,
                           NULL,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ICON_NAMES] =
      g_param_spec_boxed ("icon-names",
                          NULL, NULL,
                          G_TYPE_STRV,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_IMPORTANCE] =
      g_param_spec_enum ("importance",
                         NULL, NULL,
                         BZ_TYPE_IMPORTANCE,
                         BZ_IMPORTANCE_NEUTRAL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_lozenge_init (BzLozenge *self)
{
  GtkWidget *container = NULL;

  self->importance = BZ_IMPORTANCE_NEUTRAL;

  gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
  gtk_box_set_spacing (GTK_BOX (self), 12);
  gtk_widget_set_halign (GTK_WIDGET (self), GTK_ALIGN_CENTER);

  container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_halign (container, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (self), container);

  self->icon_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_halign (self->icon_box, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (container), self->icon_box);

  self->label_widget = gtk_label_new (NULL);
  gtk_widget_set_halign (self->label_widget, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (self->label_widget, "circular-lozenge");
  gtk_widget_add_css_class (self->label_widget, "large");
  gtk_box_append (GTK_BOX (container), self->label_widget);

  self->title_label = gtk_label_new (NULL);
  gtk_label_set_justify (GTK_LABEL (self->title_label), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (self->title_label), TRUE);
  gtk_label_set_wrap_mode (GTK_LABEL (self->title_label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign (GTK_LABEL (self->title_label), 0.5);
  gtk_widget_add_css_class (self->title_label, "title-2");
  gtk_box_append (GTK_BOX (self), self->title_label);

  bz_lozenge_rebuild (self);
}

GtkWidget *
bz_lozenge_new (void)
{
  return g_object_new (BZ_TYPE_LOZENGE, NULL);
}

void
bz_lozenge_set_title (BzLozenge   *self,
                      const gchar *title)
{
  g_return_if_fail (BZ_IS_LOZENGE (self));

  if (g_strcmp0 (self->title, title) == 0)
    return;

  g_clear_pointer (&self->title, g_free);
  self->title = g_strdup (title);

  bz_lozenge_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TITLE]);
}

const gchar *
bz_lozenge_get_title (BzLozenge *self)
{
  g_return_val_if_fail (BZ_IS_LOZENGE (self), NULL);
  return self->title;
}

void
bz_lozenge_set_label (BzLozenge   *self,
                      const gchar *label)
{
  g_return_if_fail (BZ_IS_LOZENGE (self));

  if (g_strcmp0 (self->label, label) == 0)
    return;

  g_clear_pointer (&self->label, g_free);
  self->label = g_strdup (label);

  bz_lozenge_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LABEL]);
}

const gchar *
bz_lozenge_get_label (BzLozenge *self)
{
  g_return_val_if_fail (BZ_IS_LOZENGE (self), NULL);
  return self->label;
}

void
bz_lozenge_set_icon_names (BzLozenge          *self,
                           const gchar *const *icon_names)
{
  g_return_if_fail (BZ_IS_LOZENGE (self));

  g_clear_pointer (&self->icon_names, g_strfreev);
  self->icon_names = g_strdupv ((gchar **) icon_names);

  bz_lozenge_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAMES]);
}

gchar **
bz_lozenge_get_icon_names (BzLozenge *self)
{
  g_return_val_if_fail (BZ_IS_LOZENGE (self), NULL);
  return g_strdupv (self->icon_names);
}

void
bz_lozenge_set_importance (BzLozenge   *self,
                           BzImportance importance)
{
  g_return_if_fail (BZ_IS_LOZENGE (self));

  if (self->importance == importance)
    return;

  self->importance = importance;

  bz_lozenge_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IMPORTANCE]);
}

BzImportance
bz_lozenge_get_importance (BzLozenge *self)
{
  g_return_val_if_fail (BZ_IS_LOZENGE (self), BZ_IMPORTANCE_NEUTRAL);
  return self->importance;
}

static void
bz_lozenge_rebuild (BzLozenge *self)
{
  GtkWidget   *child     = NULL;
  const gchar *css_class = NULL;

  g_return_if_fail (BZ_IS_LOZENGE (self));

  while ((child = gtk_widget_get_first_child (self->icon_box)) != NULL)
    gtk_box_remove (GTK_BOX (self->icon_box), child);

  if (self->icon_names != NULL && self->icon_names[0] != NULL)
    {
      gtk_widget_set_visible (self->icon_box, TRUE);
      gtk_widget_set_visible (self->label_widget, FALSE);

      css_class = bz_context_row_importance_to_css_class (self->importance);

      for (gsize i = 0; self->icon_names[i] != NULL; i++)
        {
          GtkWidget *icon = NULL;

          icon = gtk_image_new_from_icon_name (self->icon_names[i]);
          gtk_image_set_pixel_size (GTK_IMAGE (icon), 24);
          gtk_widget_set_halign (icon, GTK_ALIGN_CENTER);
          gtk_widget_add_css_class (icon, "circular-lozenge");
          gtk_widget_add_css_class (icon, css_class);
          gtk_widget_add_css_class (icon, "large");
          gtk_box_append (GTK_BOX (self->icon_box), icon);
        }
    }
  else if (self->label != NULL && *self->label != '\0')
    {
      const gchar *new_classes[3] = { "circular-lozenge", "large", NULL };

      gtk_widget_set_visible (self->icon_box, FALSE);
      gtk_widget_set_visible (self->label_widget, TRUE);

      gtk_label_set_markup (GTK_LABEL (self->label_widget), self->label);

      gtk_widget_set_css_classes (self->label_widget, new_classes);

      css_class = bz_context_row_importance_to_css_class (self->importance);
      gtk_widget_add_css_class (self->label_widget, css_class);
    }
  else
    {
      gtk_widget_set_visible (self->icon_box, FALSE);
      gtk_widget_set_visible (self->label_widget, FALSE);
    }

  if (self->title != NULL && *self->title != '\0')
    {
      gtk_label_set_text (GTK_LABEL (self->title_label), self->title);
      gtk_widget_set_visible (self->title_label, TRUE);
    }
  else
    {
      gtk_widget_set_visible (self->title_label, FALSE);
    }
}
