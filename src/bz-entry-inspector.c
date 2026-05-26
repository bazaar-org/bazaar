/* bz-entry-inspector.c
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

#include <gtksourceview/gtksource.h>
#include <json-glib/json-glib.h>

#include "bz-entry-inspector.h"
#include "bz-entry.h"
#include "bz-serializable.h"

struct _BzEntryInspector
{
  AdwWindow parent_instance;

  BzResult *result;

  /* Template widgets */
  GtkSourceBuffer *text_buffer;
  GtkCheckButton  *convert_to_json;
};

G_DEFINE_FINAL_TYPE (BzEntryInspector, bz_entry_inspector, ADW_TYPE_WINDOW);

enum
{
  PROP_0,

  PROP_RESULT,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
bz_entry_inspector_dispose (GObject *object)
{
  BzEntryInspector *self = BZ_ENTRY_INSPECTOR (object);

  g_clear_pointer (&self->result, g_object_unref);

  G_OBJECT_CLASS (bz_entry_inspector_parent_class)->dispose (object);
}

static void
bz_entry_inspector_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BzEntryInspector *self = BZ_ENTRY_INSPECTOR (object);

  switch (prop_id)
    {
    case PROP_RESULT:
      g_value_set_object (value, bz_entry_inspector_get_result (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_entry_inspector_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BzEntryInspector *self = BZ_ENTRY_INSPECTOR (object);

  switch (prop_id)
    {
    case PROP_RESULT:
      bz_entry_inspector_set_result (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gen_serialized (BzEntryInspector *self,
                GtkButton        *button)
{
  if (bz_result_get_resolved (self->result))
    {
      BzEntry *entry                      = NULL;
      g_autoptr (GVariantBuilder) builder = NULL;
      g_autoptr (GVariant) variant        = NULL;
      g_autofree char *string             = NULL;

      entry   = bz_result_get_object (self->result);
      builder = g_variant_builder_new (G_VARIANT_TYPE_VARDICT);
      bz_serializable_serialize (BZ_SERIALIZABLE (entry), builder);
      variant = g_variant_builder_end (builder);

      if (gtk_check_button_get_active (self->convert_to_json))
        {
          g_autoptr (JsonNode) node           = NULL;
          g_autoptr (JsonGenerator) generator = NULL;
          GtkSourceLanguageManager *lang_mgr  = NULL;
          GtkSourceLanguage        *language  = NULL;

          node = json_gvariant_serialize (variant);

          generator = json_generator_new ();
          json_generator_set_pretty (generator, TRUE);
          json_generator_set_root (generator, node);

          string = json_generator_to_data (generator, NULL);

          lang_mgr = gtk_source_language_manager_get_default ();
          language = gtk_source_language_manager_get_language (lang_mgr, "json");
          gtk_source_buffer_set_language (self->text_buffer, language);
        }
      else
        {
          string = g_variant_print (variant, FALSE);
          gtk_source_buffer_set_language (self->text_buffer, NULL);
        }

      gtk_text_buffer_set_text (GTK_TEXT_BUFFER (self->text_buffer), string, -1);
    }
  else
    {
      gtk_source_buffer_set_language (self->text_buffer, NULL);
      gtk_text_buffer_set_text (
          GTK_TEXT_BUFFER (self->text_buffer),
          "!!! The entry has not resolved", -1);
    }
}

static void
bz_entry_inspector_class_init (BzEntryInspectorClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bz_entry_inspector_set_property;
  object_class->get_property = bz_entry_inspector_get_property;
  object_class->dispose      = bz_entry_inspector_dispose;

  props[PROP_RESULT] =
      g_param_spec_object (
          "result",
          NULL, NULL,
          BZ_TYPE_RESULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-entry-inspector.ui");
  gtk_widget_class_bind_template_child (widget_class, BzEntryInspector, text_buffer);
  gtk_widget_class_bind_template_child (widget_class, BzEntryInspector, convert_to_json);
  gtk_widget_class_bind_template_callback (widget_class, gen_serialized);
}

static void
bz_entry_inspector_init (BzEntryInspector *self)
{
  GtkSourceStyleSchemeManager *scheme_mgr = NULL;
  GtkSourceStyleScheme        *scheme     = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  scheme_mgr = gtk_source_style_scheme_manager_get_default ();
  scheme     = gtk_source_style_scheme_manager_get_scheme (scheme_mgr, "Adwaita-dark");
  gtk_source_buffer_set_style_scheme (self->text_buffer, scheme);
}

BzEntryInspector *
bz_entry_inspector_new (void)
{
  return g_object_new (BZ_TYPE_ENTRY_INSPECTOR, NULL);
}

BzResult *
bz_entry_inspector_get_result (BzEntryInspector *self)
{
  g_return_val_if_fail (BZ_IS_ENTRY_INSPECTOR (self), NULL);
  return self->result;
}

void
bz_entry_inspector_set_result (BzEntryInspector *self,
                               BzResult         *result)
{
  g_return_if_fail (BZ_IS_ENTRY_INSPECTOR (self));

  g_clear_pointer (&self->result, g_object_unref);
  if (result != NULL)
    self->result = g_object_ref (result);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_RESULT]);
}

/* End of bz-entry-inspector.c */
