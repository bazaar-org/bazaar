/* bz-yaml-parser.c
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

#define G_LOG_DOMAIN "BAZAAR::YAML"

#include "config.h"

#include <gtk/gtk.h>
#include <xmlb.h>
#include <yaml.h>

#include "bz-parser.h"
#include "util.h"
#include "bz-yaml-parser.h"

/* clang-format off */
G_DEFINE_QUARK (bz-yaml-error-quark, bz_yaml_error);
/* clang-format on */

static void
deinit_schema_node (gpointer data);

BZ_DEFINE_DATA (
    schema_node,
    SchemaNode,
    {
      int kind;
      union
      {
        struct
        {
          char *vtype;
        } scalar;
        struct
        {
          GType       type;
          GHashTable *type_hints;
        } object;
        struct
        {
          SchemaNodeData *child;
        } list;
        struct
        {
          GHashTable *children;
        } mappings;
      };
    },
    deinit_schema_node (self);)

struct _BzYamlParser
{
  GObject parent_instance;

  SchemaNodeData *schema;
};

static void
parser_iface_init (BzParserInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (
    BzYamlParser,
    bz_yaml_parser,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (BZ_TYPE_PARSER, parser_iface_init))

enum
{
  KIND_SCALAR,
  KIND_OBJECT,
  KIND_LIST,
  KIND_MAPPINGS,
};

static SchemaNodeData *
compile_schema (XbNode *node);

static gboolean
parse (BzYamlParser   *self,
       yaml_parser_t  *parser,
       yaml_event_t   *event,
       gboolean        parse_first,
       gboolean        toplevel,
       SchemaNodeData *schema,
       GHashTable     *output,
       GHashTable     *anchors,
       GPtrArray      *path_stack,
       GError        **error);

static GObject *
parse_object (BzYamlParser  *self,
              yaml_parser_t *parser,
              yaml_event_t  *event,
              GType          object_gtype,
              GHashTable    *type_hints,
              GHashTable    *anchors,
              const char    *prop_path,
              GError       **error);

static char *
join_path_stack (GPtrArray *path_stack);

static void
destroy_gvalue (GValue *value);

static GValue *
make_gvalue_alloc (GType type);

static GValue *
copy_gvalue_alloc (GValue *value);

static void
bz_yaml_parser_dispose (GObject *object)
{
  BzYamlParser *self = BZ_YAML_PARSER (object);

  g_clear_pointer (&self->schema, schema_node_data_unref);

  G_OBJECT_CLASS (bz_yaml_parser_parent_class)->dispose (object);
}

static void
bz_yaml_parser_class_init (BzYamlParserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = bz_yaml_parser_dispose;

  g_type_ensure (GTK_TYPE_STRING_OBJECT);
}

static void
bz_yaml_parser_init (BzYamlParser *self)
{
}

static GHashTable *
bz_yaml_parser_real_process_bytes (BzParser *iface_self,
                                   GBytes   *bytes,
                                   GError  **error)
{
  BzYamlParser *self               = BZ_YAML_PARSER (iface_self);
  g_autoptr (GError) local_error   = NULL;
  gsize         bytes_size         = 0;
  const guchar *bytes_data         = NULL;
  yaml_parser_t parser             = { 0 };
  yaml_event_t  event              = { 0 };
  g_autoptr (GHashTable) output    = NULL;
  g_autoptr (GHashTable) anchors   = NULL;
  g_autoptr (GPtrArray) path_stack = NULL;
  gboolean result                  = FALSE;

  g_return_val_if_fail (BZ_IS_YAML_PARSER (self), NULL);
  g_return_val_if_fail (bytes != NULL, NULL);

  bytes_data = g_bytes_get_data (bytes, &bytes_size);

  yaml_parser_initialize (&parser);
  yaml_parser_set_input_string (&parser, bytes_data, bytes_size);

  output = g_hash_table_new_full (
      g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) destroy_gvalue);
  anchors = g_hash_table_new_full (
      g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) destroy_gvalue);
  path_stack = g_ptr_array_new_with_free_func (g_free);

  result = parse (
      self,
      &parser,
      &event,
      TRUE,
      TRUE,
      self->schema,
      output,
      anchors,
      path_stack,
      &local_error);
  yaml_parser_delete (&parser);

  if (result)
    return g_steal_pointer (&output);
  else
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return NULL;
    }
}

static void
parser_iface_init (BzParserInterface *iface)
{
  iface->process_bytes = bz_yaml_parser_real_process_bytes;
}

BzYamlParser *
bz_yaml_parser_new_for_resource_schema (const char *path)
{
  g_autoptr (GError) local_error    = NULL;
  g_autoptr (GBytes) bytes          = NULL;
  const char *resource_data         = NULL;
  g_autoptr (XbSilo) silo           = NULL;
  g_autoptr (XbNode) root           = NULL;
  g_autoptr (SchemaNodeData) schema = NULL;
  g_autoptr (BzYamlParser) parser   = NULL;

  g_return_val_if_fail (path != NULL, NULL);

  bytes = g_resources_lookup_data (
      path, G_RESOURCE_LOOKUP_FLAGS_NONE, &local_error);
  if (bytes == NULL)
    g_critical ("Could not load internal resource: %s", local_error->message);
  g_assert (bytes != NULL);
  resource_data = g_bytes_get_data (bytes, NULL);

  silo = xb_silo_new_from_xml (resource_data, &local_error);
  if (silo == NULL)
    g_critical ("Could not parse internal xml resource: %s", local_error->message);
  g_assert (silo != NULL);

  root = xb_silo_get_root (silo);

  parser         = g_object_new (BZ_TYPE_YAML_PARSER, NULL);
  parser->schema = compile_schema (root);

  return g_steal_pointer (&parser);
}

static SchemaNodeData *
compile_schema (XbNode *node)
{
  const char *element               = NULL;
  g_autoptr (SchemaNodeData) schema = NULL;

  element = xb_node_get_element (node);
  schema  = schema_node_data_new ();

#define ERROR_OUT(...)                                   \
  G_STMT_START                                           \
  {                                                      \
    g_critical ("Fatal: internal schema: " __VA_ARGS__); \
    g_assert (FALSE);                                    \
  }                                                      \
  G_STMT_END

  if (g_strcmp0 (element, "scalar") == 0)
    {
      const char *type = NULL;

      type = xb_node_get_attr (node, "type");
      if (type == NULL)
        ERROR_OUT ("scalar must have a type");
      if (!g_variant_type_is_basic ((const GVariantType *) type))
        ERROR_OUT ("invalid variant type for scalar '%s'", type);

      schema->kind         = KIND_SCALAR;
      schema->scalar.vtype = g_strdup (type);
    }
  else if (g_strcmp0 (element, "object") == 0)
    {
      const char *class                  = NULL;
      GType       gtype                  = G_TYPE_INVALID;
      g_autoptr (GTypeClass) gtype_class = NULL;
      XbNode *child                      = NULL;

      class = xb_node_get_attr (node, "class");
      if (class == NULL)
        ERROR_OUT ("object must have a class");

      gtype = g_type_from_name (class);
      if (gtype == G_TYPE_INVALID || !g_type_is_a (gtype, G_TYPE_OBJECT))
        ERROR_OUT ("'%s' is not a valid object class", class);

      schema->kind              = KIND_OBJECT;
      schema->object.type       = gtype;
      schema->object.type_hints = g_hash_table_new_full (
          g_str_hash, g_str_equal, g_free, NULL);

      gtype_class = g_type_class_ref (gtype);

      child = xb_node_get_child (node);
      while (child != NULL)
        {
          const char *child_element  = NULL;
          const char *name           = NULL;
          GParamSpec *property       = NULL;
          const char *typehint_name  = NULL;
          GType       typehint_gtype = G_TYPE_INVALID;
          XbNode     *next           = NULL;

          child_element = xb_node_get_element (child);
          if (g_strcmp0 (child_element, "typehint") != 0)
            ERROR_OUT ("\"typehint\" is the only valid child element of \"object\"");

          name = xb_node_get_attr (child, "name");
          if (name == NULL)
            ERROR_OUT ("typehint must have a name");

          /* TODO: implement recursive checking */
          if (strchr (name, '.') == NULL)
            {
              property = g_object_class_find_property (G_OBJECT_CLASS (gtype_class), name);
              if (property == NULL)
                ERROR_OUT ("typehint property '%s' is invalid", name);
            }

          typehint_name = xb_node_get_attr (child, "type");
          if (typehint_name == NULL)
            ERROR_OUT ("typehint must have a type");

          typehint_gtype = g_type_from_name (typehint_name);
          if (typehint_gtype == G_TYPE_INVALID || !g_type_is_a (typehint_gtype, G_TYPE_OBJECT))
            ERROR_OUT ("'%s' is not a valid object class", typehint_name);

          g_hash_table_replace (
              schema->object.type_hints,
              g_strdup (name),
              GSIZE_TO_POINTER (typehint_gtype));

          next = xb_node_get_next (child);
          g_object_unref (child);
          child = next;
        }
    }
  else if (g_strcmp0 (element, "list") == 0)
    {
      g_autoptr (XbNode) child = NULL;

      child = xb_node_get_child (node);
      if (child == NULL)
        ERROR_OUT ("list must have a child");

      schema->kind       = KIND_LIST;
      schema->list.child = compile_schema (child);
    }
  else if (g_strcmp0 (element, "mappings") == 0)
    {
      XbNode *child = NULL;

      schema->kind              = KIND_MAPPINGS;
      schema->mappings.children = g_hash_table_new_full (
          g_str_hash, g_str_equal, g_free, schema_node_data_unref);

      child = xb_node_get_child (node);
      while (child != NULL)
        {
          const char *child_element      = NULL;
          const char *key                = NULL;
          g_autoptr (XbNode) child_child = NULL;
          XbNode *next                   = NULL;

          child_element = xb_node_get_element (child);
          if (g_strcmp0 (child_element, "mapping") != 0)
            ERROR_OUT ("\"mapping\" is the only valid child element of \"mappings\"");

          key = xb_node_get_attr (child, "key");
          if (key == NULL)
            ERROR_OUT ("mapping must have a key");

          child_child = xb_node_get_child (child);
          if (child == NULL)
            ERROR_OUT ("mapping must have a child");

          g_hash_table_replace (
              schema->mappings.children,
              g_strdup (key),
              compile_schema (child_child));

          next = xb_node_get_next (child);
          g_object_unref (child);
          child = next;
        }
    }
  else
    ERROR_OUT ("unrecognized element '%s'", element);

#undef ERROR_OUT

  return g_steal_pointer (&schema);
}

static gboolean
parse (BzYamlParser   *self,
       yaml_parser_t  *parser,
       yaml_event_t   *event,
       gboolean        parse_first,
       gboolean        toplevel,
       SchemaNodeData *schema,
       GHashTable     *output,
       GHashTable     *anchors,
       GPtrArray      *path_stack,
       GError        **error)
{
  if (parse_first && !yaml_parser_parse (parser, event))
    {
      g_set_error (
          error,
          BZ_YAML_ERROR,
          BZ_YAML_ERROR_INVALID_YAML,
          "Failed to parse YAML at line %zu, column %zu: %s",
          parser->problem_mark.line,
          parser->problem_mark.column,
          parser->problem);
      return FALSE;
    }

#define NEXT_EVENT()                                            \
  G_STMT_START                                                  \
  {                                                             \
    yaml_event_delete (event);                                  \
    if (!yaml_parser_parse (parser, event))                     \
      {                                                         \
        g_set_error (                                           \
            error,                                              \
            BZ_YAML_ERROR,                                      \
            BZ_YAML_ERROR_INVALID_YAML,                         \
            "Failed to parse YAML at line %zu, column %zu: %s", \
            parser->problem_mark.line,                          \
            parser->problem_mark.column,                        \
            parser->problem);                                   \
        return FALSE;                                           \
      }                                                         \
  }                                                             \
  G_STMT_END

#define EXPECT(event_type, string_type)                                      \
  if (event->type != (event_type))                                           \
    {                                                                        \
      g_set_error (                                                          \
          error,                                                             \
          BZ_YAML_ERROR,                                                     \
          BZ_YAML_ERROR_DOES_NOT_CONFORM,                                    \
          "Failed to validate YAML against schema at line %zu, column %zu: " \
          "expected " string_type " here",                                   \
          event->start_mark.line,                                            \
          event->start_mark.column);                                         \
      yaml_event_delete (event);                                             \
      return FALSE;                                                          \
    }

#define TRY_ALIAS(var, gtype, string_type)                                       \
  if (event->type == YAML_ALIAS_EVENT)                                           \
    {                                                                            \
      GValue *_value = NULL;                                                     \
                                                                                 \
      _value = g_hash_table_lookup (                                             \
          anchors,                                                               \
          event->data.alias.anchor);                                             \
      if (_value == NULL)                                                        \
        {                                                                        \
          g_set_error (                                                          \
              error,                                                             \
              BZ_YAML_ERROR,                                                     \
              BZ_YAML_ERROR_INVALID_YAML,                                        \
              "Failed to resolve YAML alias at line %zu, column %zu: "           \
              "the anchor \"%s\" does not yet exist",                            \
              event->start_mark.line,                                            \
              event->start_mark.column,                                          \
              (const char *) event->data.alias.anchor);                          \
          yaml_event_delete (event);                                             \
          return FALSE;                                                          \
        }                                                                        \
                                                                                 \
      if (!G_VALUE_HOLDS (_value, (gtype)))                                      \
        {                                                                        \
          g_set_error (                                                          \
              error,                                                             \
              BZ_YAML_ERROR,                                                     \
              BZ_YAML_ERROR_DOES_NOT_CONFORM,                                    \
              "Failed to validate YAML against schema at line %zu, column %zu: " \
              "the alias needs to be of type " string_type " here",              \
              event->start_mark.line,                                            \
              event->start_mark.column);                                         \
          yaml_event_delete (event);                                             \
          return FALSE;                                                          \
        }                                                                        \
                                                                                 \
      (var) = copy_gvalue_alloc (_value);                                        \
    }

  if (toplevel)
    {
      EXPECT (YAML_STREAM_START_EVENT, "start of stream");
      NEXT_EVENT ();
      EXPECT (YAML_DOCUMENT_START_EVENT, "start of document");
      NEXT_EVENT ();
    }

  switch (schema->kind)
    {
    case KIND_SCALAR:
      {
        GValue *value = NULL;

        TRY_ALIAS (value, G_TYPE_VARIANT, "scalar");
        if (value != NULL)
          {
            GVariant *variant = NULL;

            variant = g_value_get_variant (value);
            if (!g_variant_type_equal (
                    g_variant_get_type (variant),
                    (const GVariantType *) schema->scalar.vtype))
              {
                g_set_error (
                    error,
                    BZ_YAML_ERROR,
                    BZ_YAML_ERROR_DOES_NOT_CONFORM,
                    "Failed to validate YAML against schema at line %zu, column %zu: "
                    "the alias needs to be of scalar type %s here",
                    event->start_mark.line,
                    event->start_mark.column,
                    schema->scalar.vtype);
                destroy_gvalue (value);
                yaml_event_delete (event);
                return FALSE;
              }
          }
        else
          {
            g_autofree char *anchor      = NULL;
            g_autoptr (GVariant) variant = NULL;

            EXPECT (YAML_SCALAR_EVENT, "scalar");
            anchor = bz_maybe_strdup ((const char *) event->data.scalar.anchor);

            if (g_variant_type_equal ((const GVariantType *) schema->scalar.vtype, G_VARIANT_TYPE_STRING))
              variant = g_variant_new_string ((const char *) event->data.scalar.value);
            else
              {
                g_autoptr (GError) local_error = NULL;

                variant = g_variant_parse (
                    (const GVariantType *) schema->scalar.vtype,
                    (const char *) event->data.scalar.value,
                    NULL,
                    NULL,
                    &local_error);
                if (variant == NULL)
                  {
                    g_set_error (
                        error,
                        BZ_YAML_ERROR,
                        BZ_YAML_ERROR_BAD_SCALAR,
                        "Failed to parse scalar variant at line %zu, column %zu: "
                        "%s",
                        event->start_mark.line,
                        event->start_mark.column,
                        local_error->message);
                    yaml_event_delete (event);
                    return FALSE;
                  }
              }

            value = make_gvalue_alloc (G_TYPE_VARIANT);
            g_value_set_variant (value, g_steal_pointer (&variant));

            if (anchor != NULL)
              g_hash_table_replace (
                  anchors,
                  g_steal_pointer (&anchor),
                  copy_gvalue_alloc (value));
          }

        g_hash_table_replace (output, join_path_stack (path_stack), value);
      }
      break;
    case KIND_OBJECT:
      {
        g_autoptr (GObject) object = NULL;
        GValue *value              = NULL;

        TRY_ALIAS (value, schema->object.type, "object");
        if (value == NULL)
          {
            object = parse_object (
                self,
                parser,
                event,
                schema->object.type,
                schema->object.type_hints,
                anchors,
                NULL,
                error);
            if (object == NULL)
              /* event is already cleaned up */
              return FALSE;

            value = make_gvalue_alloc (schema->object.type);
            g_value_set_object (value, object);
          }

        g_hash_table_replace (output, join_path_stack (path_stack), value);
      }
      break;
    case KIND_LIST:
      {
        GValue *value = NULL;

        TRY_ALIAS (value, G_TYPE_PTR_ARRAY, "list");
        if (value == NULL)
          {
            g_autofree char *anchor    = NULL;
            g_autoptr (GPtrArray) list = NULL;

            EXPECT (YAML_SEQUENCE_START_EVENT, "list");
            anchor = bz_maybe_strdup ((const char *) event->data.sequence_start.anchor);

            list = g_ptr_array_new_with_free_func ((GDestroyNotify) destroy_gvalue);
            for (;;)
              {
                g_autoptr (GHashTable) list_output    = NULL;
                g_autoptr (GPtrArray) list_path_stack = NULL;
                gboolean result                       = FALSE;
                GValue  *append                       = NULL;

                NEXT_EVENT ();
                if (event->type == YAML_SEQUENCE_END_EVENT)
                  break;

                list_output = g_hash_table_new_full (
                    g_str_hash, g_str_equal,
                    g_free, (GDestroyNotify) destroy_gvalue);
                list_path_stack = g_ptr_array_new_with_free_func (g_free);

                result = parse (
                    self,
                    parser,
                    event,
                    FALSE,
                    FALSE,
                    schema->list.child,
                    list_output,
                    anchors,
                    list_path_stack,
                    error);
                if (!result)
                  /* event is already cleaned up */
                  return FALSE;

                append = make_gvalue_alloc (G_TYPE_HASH_TABLE);
                g_value_set_boxed (append, list_output);
                g_ptr_array_add (list, append);
              }

            value = make_gvalue_alloc (G_TYPE_PTR_ARRAY);
            g_value_set_boxed (value, list);

            if (anchor != NULL)
              g_hash_table_replace (
                  anchors,
                  g_steal_pointer (&anchor),
                  copy_gvalue_alloc (value));
          }

        g_hash_table_replace (output, join_path_stack (path_stack), value);
      }
      break;
    case KIND_MAPPINGS:
      {
        EXPECT (YAML_MAPPING_START_EVENT, "mappings");

        for (;;)
          {
            g_autofree char *key        = NULL;
            SchemaNodeData  *map_schema = NULL;
            gboolean         result     = FALSE;

            NEXT_EVENT ();

            if (event->type == YAML_MAPPING_END_EVENT)
              break;
            EXPECT (YAML_SCALAR_EVENT, "scalar key");

            key        = g_strdup ((const char *) event->data.scalar.value);
            map_schema = g_hash_table_lookup (schema->mappings.children, key);
            if (map_schema == NULL)
              {
                g_autofree char *path = NULL;

                path = join_path_stack (path_stack);
                g_set_error (
                    error,
                    BZ_YAML_ERROR,
                    BZ_YAML_ERROR_DOES_NOT_CONFORM,
                    "Failed to validate YAML against schema at line %zu, column %zu: "
                    "key '%s' shouldn't exist at path %s",
                    event->start_mark.line,
                    event->start_mark.column,
                    key,
                    path);
                yaml_event_delete (event);
                return FALSE;
              }

            g_ptr_array_add (path_stack, g_steal_pointer (&key));

            result = parse (
                self,
                parser,
                event,
                TRUE,
                FALSE,
                map_schema,
                output,
                anchors,
                path_stack,
                error);
            if (!result)
              /* event is already cleaned up */
              return FALSE;

            g_ptr_array_set_size (path_stack, path_stack->len - 1);
          }
      }
      break;
    default:
      g_assert_not_reached ();
    }

  if (toplevel)
    {
      NEXT_EVENT ();
      EXPECT (YAML_DOCUMENT_END_EVENT, "end of document");
      NEXT_EVENT ();
      EXPECT (YAML_STREAM_END_EVENT, "end of stream");
    }

  yaml_event_delete (event);
  return TRUE;
}

static GObject *
parse_object (BzYamlParser  *self,
              yaml_parser_t *parser,
              yaml_event_t  *event,
              GType          object_gtype,
              GHashTable    *type_hints,
              GHashTable    *anchors,
              const char    *prop_path,
              GError       **error)
{
  GValue          *value             = NULL;
  g_autofree char *object_anchor     = NULL;
  g_autoptr (GTypeClass) gtype_class = NULL;
  g_autoptr (GObject) object         = NULL;

  TRY_ALIAS (value, object_gtype, "object mapping");
  if (value != NULL)
    {
      object = g_value_dup_object (value);
      destroy_gvalue (value);
      return g_steal_pointer (&object);
    }

  EXPECT (YAML_MAPPING_START_EVENT, "object mapping");
  object_anchor = bz_maybe_strdup ((const char *) event->data.mapping_start.anchor);

  gtype_class = g_type_class_ref (object_gtype);
  object      = g_object_new (object_gtype, NULL);

  for (;;)
    {
      g_autofree char *property = NULL;
      GParamSpec      *spec     = NULL;

      NEXT_EVENT ();

      if (event->type == YAML_MAPPING_END_EVENT)
        break;
      EXPECT (YAML_SCALAR_EVENT, "scalar key");

      property = g_strdup ((const char *) event->data.scalar.value);
      spec     = g_object_class_find_property (G_OBJECT_CLASS (gtype_class), property);
      if (spec == NULL)
        {
          g_set_error (
              error,
              BZ_YAML_ERROR,
              BZ_YAML_ERROR_DOES_NOT_CONFORM,
              "Failed to validate YAML against schema at line %zu, column %zu: "
              "property '%s' doesn't exist on type %s",
              event->start_mark.line,
              event->start_mark.column,
              property,
              g_type_name (object_gtype));
          yaml_event_delete (event);
          return FALSE;
        }

      NEXT_EVENT ();

      if (g_type_is_a (spec->value_type, G_TYPE_LIST_MODEL))
        {
          g_autofree char *replace_prop_path = NULL;
          GType            element_gtype     = 0;
          g_autoptr (GListModel) list        = NULL;

          if (prop_path != NULL)
            replace_prop_path = g_strdup_printf ("%s.%s", prop_path, property);
          element_gtype = GPOINTER_TO_SIZE (g_hash_table_lookup (
              type_hints, replace_prop_path != NULL ? replace_prop_path : property));

          TRY_ALIAS (value, G_TYPE_LIST_MODEL, "mappings");
          if (value != NULL)
            {
              list = g_value_dup_object (value);

              if (!G_IS_LIST_MODEL (list) ||
                  g_list_model_get_item_type (list) != element_gtype)
                {
                  g_set_error (
                      error,
                      BZ_YAML_ERROR,
                      BZ_YAML_ERROR_DOES_NOT_CONFORM,
                      "Failed to validate YAML against schema at line %zu, column %zu: "
                      "the alias needs to be a list of object type %s here",
                      event->start_mark.line,
                      event->start_mark.column,
                      g_type_name (element_gtype));
                  destroy_gvalue (value);
                  yaml_event_delete (event);
                  return FALSE;
                }
            }
          else
            {
              g_autofree char *anchor = NULL;

              EXPECT (YAML_SEQUENCE_START_EVENT, "sequence");
              anchor = bz_maybe_strdup ((const char *) event->data.sequence_start.anchor);

              if (element_gtype == GTK_TYPE_STRING_OBJECT)
                {
                  list = (GListModel *) gtk_string_list_new (NULL);

                  for (;;)
                    {
                      NEXT_EVENT ();

                      if (event->type == YAML_SEQUENCE_END_EVENT)
                        break;
                      EXPECT (YAML_SCALAR_EVENT, "scalar list value");

                      gtk_string_list_append (
                          GTK_STRING_LIST (list),
                          (const char *) event->data.scalar.value);
                    }
                }
              else
                {
                  if (element_gtype == 0)
                    element_gtype = G_TYPE_OBJECT;

                  list = (GListModel *) g_list_store_new (element_gtype);

                  for (;;)
                    {
                      g_autoptr (GObject) child_object = NULL;

                      NEXT_EVENT ();
                      if (event->type == YAML_SEQUENCE_END_EVENT)
                        break;

                      child_object = parse_object (
                          self,
                          parser,
                          event,
                          element_gtype,
                          type_hints,
                          anchors,
                          replace_prop_path != NULL ? replace_prop_path : property,
                          error);
                      if (child_object == NULL)
                        /* event is already cleaned up */
                        return FALSE;

                      g_list_store_append (G_LIST_STORE (list), child_object);
                    }
                }

              if (anchor != NULL)
                {
                  value = make_gvalue_alloc (G_TYPE_LIST_MODEL);
                  g_value_set_object (value, list);

                  g_hash_table_replace (
                      anchors,
                      g_steal_pointer (&anchor),
                      value);
                }
            }

          g_object_set (object, property, list, NULL);
        }
      else if (g_type_is_a (spec->value_type, G_TYPE_OBJECT))
        {
          g_autofree char *replace_prop_path = NULL;
          g_autoptr (GObject) prop_object    = NULL;

          if (prop_path != NULL)
            replace_prop_path = g_strdup_printf ("%s.%s", prop_path, property);

          prop_object = parse_object (
              self,
              parser,
              event,
              spec->value_type,
              type_hints,
              anchors,
              replace_prop_path != NULL ? replace_prop_path : property,
              error);
          if (prop_object == NULL)
            /* event is already cleaned up */
            return FALSE;

          g_object_set (object, property, prop_object, NULL);
        }
      else if (g_type_is_a (spec->value_type, G_TYPE_ENUM))
        {
          g_autoptr (GEnumClass) class = NULL;
          GEnumValue *enum_value       = NULL;

          TRY_ALIAS (value, spec->value_type, "scalar enum value");
          if (value != NULL)
            {
              g_object_set_property (object, property, value);
              destroy_gvalue (value);
            }
          else
            {
              g_autofree char *anchor = NULL;

              EXPECT (YAML_SCALAR_EVENT, "scalar enum value");
              anchor = bz_maybe_strdup ((const char *) event->data.scalar.anchor);

              class = g_type_class_ref (spec->value_type);

              enum_value = g_enum_get_value_by_nick (
                  class, (const char *) event->data.scalar.value);
              if (enum_value == NULL)
                enum_value = g_enum_get_value_by_name (
                    class, (const char *) event->data.scalar.value);

              if (enum_value == NULL)
                {
                  g_set_error (
                      error,
                      BZ_YAML_ERROR,
                      BZ_YAML_ERROR_BAD_SCALAR,
                      "Failed to parse scalar enum at line %zu, column %zu: "
                      "'%s' does not exist in type %s",
                      event->start_mark.line,
                      event->start_mark.column,
                      (const char *) event->data.scalar.value,
                      g_type_name (spec->value_type));
                  yaml_event_delete (event);
                  return FALSE;
                }

              if (anchor != NULL)
                {
                  value = make_gvalue_alloc (spec->value_type);
                  g_value_set_enum (value, enum_value->value);

                  g_hash_table_replace (
                      anchors,
                      g_steal_pointer (&anchor),
                      value);
                }

              g_object_set (object, property, enum_value->value, NULL);
            }
        }
      else
        {
          g_autoptr (GError) local_error = NULL;
          const GVariantType *vtype      = NULL;
          g_autoptr (GVariant) variant   = NULL;

          TRY_ALIAS (value, spec->value_type, "scalar");
          if (value != NULL)
            {
              g_object_set_property (object, property, value);
              destroy_gvalue (value);
            }
          else
            {
              g_autofree char *anchor = NULL;

              if (spec->value_type == G_TYPE_STRING &&
                  event->type == YAML_MAPPING_START_EVENT)
                {
                  /* Handle optional translated strings */
                  const char *const *langs   = NULL;
                  g_autofree char   *english = NULL;

                  anchor = bz_maybe_strdup ((const char *) event->data.mapping_start.anchor);

                  langs = g_get_language_names ();
                  for (;;)
                    {
                      g_autofree char *code = NULL;

                      NEXT_EVENT ();
                      if (event->type == YAML_MAPPING_END_EVENT)
                        break;
                      EXPECT (YAML_SCALAR_EVENT, "scalar key language code");
                      if (variant != NULL)
                        continue;

                      code = g_strdup ((const char *) event->data.scalar.value);

                      NEXT_EVENT ();
                      EXPECT (YAML_SCALAR_EVENT, "scalar translated string");

                      if (g_strv_contains (langs, code))
                        variant = g_variant_new_string ((const char *) event->data.scalar.value);
                      else if (english == NULL &&
                               g_strcmp0 (code, "en") == 0)
                        english = g_strdup ((const char *) event->data.scalar.value);
                    }

                  if (variant == NULL)
                    variant = g_variant_new_string (english != NULL ? english : "NULL");
                }
              else
                {
                  EXPECT (YAML_SCALAR_EVENT, "scalar value");
                  anchor = bz_maybe_strdup ((const char *) event->data.scalar.anchor);

                  switch (spec->value_type)
                    {
                    case G_TYPE_BOOLEAN:
                      vtype = G_VARIANT_TYPE_BOOLEAN;
                      break;
                    case G_TYPE_INT:
                      vtype = G_VARIANT_TYPE_INT32;
                      break;
                    case G_TYPE_INT64:
                      vtype = G_VARIANT_TYPE_INT64;
                      break;
                    case G_TYPE_UINT:
                      vtype = G_VARIANT_TYPE_UINT32;
                      break;
                    case G_TYPE_UINT64:
                      vtype = G_VARIANT_TYPE_UINT64;
                      break;
                    case G_TYPE_DOUBLE:
                    case G_TYPE_FLOAT:
                      vtype = G_VARIANT_TYPE_DOUBLE;
                      break;
                    case G_TYPE_STRING:
                    default:
                      vtype = G_VARIANT_TYPE_STRING;
                      break;
                    }

                  if (g_variant_type_equal (vtype, G_VARIANT_TYPE_STRING))
                    variant = g_variant_new_string ((const char *) event->data.scalar.value);
                  else
                    {
                      variant = g_variant_parse (
                          vtype,
                          (const char *) event->data.scalar.value,
                          NULL,
                          NULL,
                          &local_error);
                      if (variant == NULL)
                        {
                          g_set_error (
                              error,
                              BZ_YAML_ERROR,
                              BZ_YAML_ERROR_BAD_SCALAR,
                              "Failed to parse scalar variant at line %zu, column %zu: %s",
                              event->start_mark.line,
                              event->start_mark.column,
                              local_error->message);
                          yaml_event_delete (event);
                          return FALSE;
                        }
                    }
                }

              if (anchor != NULL)
                value = make_gvalue_alloc (spec->value_type);

              switch (spec->value_type)
                {
                case G_TYPE_BOOLEAN:
                  g_object_set (object, property, g_variant_get_boolean (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_boolean (value, g_variant_get_boolean (variant));
                  break;
                case G_TYPE_INT:
                  g_object_set (object, property, g_variant_get_int32 (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_int (value, g_variant_get_int32 (variant));
                  break;
                case G_TYPE_INT64:
                  g_object_set (object, property, g_variant_get_int64 (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_int64 (value, g_variant_get_int64 (variant));
                  break;
                case G_TYPE_UINT:
                  g_object_set (object, property, g_variant_get_uint32 (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_uint (value, g_variant_get_uint32 (variant));
                  break;
                case G_TYPE_UINT64:
                  g_object_set (object, property, g_variant_get_uint64 (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_uint64 (value, g_variant_get_uint64 (variant));
                  break;
                case G_TYPE_DOUBLE:
                case G_TYPE_FLOAT:
                  g_object_set (object, property, g_variant_get_double (variant), NULL);
                  if (anchor != NULL)
                    g_value_set_double (value, g_variant_get_double (variant));
                  break;
                case G_TYPE_STRING:
                default:
                  g_object_set (object, property, g_variant_get_string (variant, NULL), NULL);
                  if (anchor != NULL)
                    g_value_set_string (value, g_variant_get_string (variant, NULL));
                  break;
                }

              if (anchor != NULL)
                g_hash_table_replace (
                    anchors,
                    g_steal_pointer (&anchor),
                    value);
            }
        }
    }

  if (object_anchor != NULL)
    {
      value = make_gvalue_alloc (G_TYPE_LIST_MODEL);
      g_value_set_object (value, object);

      g_hash_table_replace (
          anchors,
          g_steal_pointer (&object_anchor),
          value);
    }

  return g_steal_pointer (&object);
}

static char *
join_path_stack (GPtrArray *path_stack)
{
  GString *string = NULL;

  if (path_stack->len == 0)
    return g_strdup ("/");

  string = g_string_new (NULL);
  for (guint i = 0; i < path_stack->len; i++)
    {
      const char *component = NULL;

      component = g_ptr_array_index (path_stack, i);
      g_string_append_printf (string, "/%s", component);
    }

  return g_string_free_and_steal (string);
}

static void
destroy_gvalue (GValue *value)
{
  g_value_unset (value);
  g_free (value);
}

static GValue *
make_gvalue_alloc (GType type)
{
  GValue *ret = NULL;

  ret = g_new0 (typeof (*ret), 1);
  g_value_init (ret, type);
  return ret;
}

static GValue *
copy_gvalue_alloc (GValue *value)
{
  GValue *ret = NULL;

  ret = g_new0 (typeof (*ret), 1);
  g_value_init (ret, value->g_type);
  g_value_copy (value, ret);
  return ret;
}

static void
deinit_schema_node (gpointer data)
{
  SchemaNodeData *self = data;

  switch (self->kind)
    {
    case KIND_SCALAR:
      g_clear_pointer (&self->scalar.vtype, g_free);
      break;
    case KIND_OBJECT:
      g_clear_pointer (&self->object.type_hints, g_hash_table_unref);
      break;
    case KIND_LIST:
      g_clear_pointer (&self->list.child, schema_node_data_unref);
      break;
    case KIND_MAPPINGS:
      g_clear_pointer (&self->mappings.children, g_hash_table_unref);
      break;
    default:
      g_assert_not_reached ();
    }
}
