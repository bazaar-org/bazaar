/* parser.c
 *
 * Copyright 2026 Eva M
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

#define G_LOG_DOMAIN "BGE::WDGT-PARSE"

#include <glib/gi18n.h>
#include <graphene-gobject.h>

#include "../bge-wdgt-spec-private.h"
#include "bge-marshalers.h"
#include "parser.h"
#include "util.h"

#define IS_EOF(_p) ((_p) == NULL || *(_p) == '\0')

#define SINGLE_CHAR_TOKENS      "{}()=:;,"
#define EVAL_SINGLE_CHAR_TOKENS "#(),+-*/^%"

#define STR_DEFARRAY  "defarray"
#define STR_DEFWIDGET "defwidget"
#define STR_FOREACH   "%FOREACH"

#define STR_VARIABLE          "var"
#define STR_REFERENCE         "reference"
#define STR_INIT              "init"
#define STR_STATE             "state"
#define STR_DEFAULT_STATE     "state-default"
#define STR_SET               "set"
#define STR_TRANSITION        "transition"
#define STR_TRANSITION_SPRING "transition-spring"
#define STR_ALLOCATE          "allocate"
#define STR_MEASURE           "measure"
#define STR_SNAPSHOT          "snapshot"

typedef enum
{
  TOKEN_PARSE_DEFAULT = 0,
  TOKEN_PARSE_QUOTED  = 1 << 0,
} TokenParseFlags;

typedef enum
{
  ARGS_PARSE_PARENS = 0,
  ARGS_PARSE_LEFT_ASSIGN,
  ARGS_PARSE_RIGHT_ASSIGN,
} ArgsParseKind;

typedef enum
{
  OPERATOR_ADD = 0,
  OPERATOR_SUBTRACT,
  OPERATOR_MULTIPLY,
  OPERATOR_DIVIDE,
  OPERATOR_MODULUS,
  OPERATOR_POWER,
} Operator;

static const int operator_precedence[] = {
  [OPERATOR_ADD]      = 0,
  [OPERATOR_SUBTRACT] = 0,
  [OPERATOR_MULTIPLY] = 1,
  [OPERATOR_DIVIDE]   = 1,
  [OPERATOR_MODULUS]  = 1,
  [OPERATOR_POWER]    = 2,
};

typedef struct
{
  Operator op;
  guint    pos;
} EvalOperator;

BGE_DEFINE_DATA (
    eval_closure,
    EvalClosure,
    {
      GArray  *ops;
      gdouble *workbuf0;
      gdouble *workbuf1;
    },
    BGE_RELEASE_DATA (ops, g_array_unref);
    BGE_RELEASE_DATA (workbuf0, g_free);
    BGE_RELEASE_DATA (workbuf1, g_free));

static const char *
parse_widget_block (const char  *p,
                    BgeWdgtSpec *spec,
                    GHashTable  *macro_arrays,
                    GHashTable  *macro_replacements,
                    guint       *n_anon_vals,
                    GHashTable  *type_hints,
                    GError     **error);

static const char *
parse_snapshot_block (const char  *p,
                      BgeWdgtSpec *spec,
                      const char  *state,
                      GHashTable  *macro_replacements,
                      guint       *n_anon_vals,
                      GHashTable  *type_hints,
                      GError     **error);

static const char *
parse_eval (const char  *p,
            BgeWdgtSpec *spec,
            const char  *state,
            GHashTable  *macro_replacements,
            guint       *n_anon_vals,
            GHashTable  *type_hints,
            char       **value_out,
            GError     **error);

static const char *
parse_args (const char        *p,
            BgeWdgtSpec       *spec,
            const char        *state,
            const char        *enclosing_object,
            GHashTable        *macro_replacements,
            guint             *n_anon_vals,
            GHashTable        *type_hints,
            const char *const *destinations,
            GType              destinations_types[],
            guint              n_destinations,
            char            ***values_out,
            GType            **types_out,
            guint             *n_out,
            ArgsParseKind      parse_kind,
            GError           **error);

static char *
parse_token_fundamental (const char  *token,
                         BgeWdgtSpec *spec,
                         guint       *n_anon_vals,
                         GError     **error);

static char *
consume_token (const char    **pp,
               const char     *single_chars,
               TokenParseFlags flags,
               gboolean       *was_quoted,
               GHashTable     *macro_replacements,
               GError        **error);

static char *
consume_token_inner (const char    **pp,
                     const char     *single_chars,
                     TokenParseFlags flags,
                     gboolean       *was_quoted,
                     GError        **error);

static gdouble
eval_closure (gpointer         this,
              guint            n_param_values,
              const GValue    *param_values,
              EvalClosureData *data);

static char *
make_object_property_name (const char *object,
                           const char *property,
                           guint       n);

static char *
make_widget_allocation_name (const char *widget,
                             guint       n);

static char *
make_widget_measurement_name (guint n);

static char *
make_anon_name (guint n);

static gint
cmp_operator (EvalOperator *a,
              EvalOperator *b);

static void
_marshal_DIRECT__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data);

static void
_marshal_BOOL__ARGS_DIRECT (GClosure                *closure,
                            GValue                  *return_value,
                            guint                    n_param_values,
                            const GValue            *param_values,
                            gpointer invocation_hint G_GNUC_UNUSED,
                            gpointer                 marshal_data);

static void
_marshal_DOUBLE__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data);

BgeWdgtSpec *
bge_wdgt_parse_string (const char *string,
                       GError    **error)
{
  g_autoptr (GError) local_error            = NULL;
  gboolean result                           = FALSE;
  g_autoptr (BgeWdgtSpec) spec              = NULL;
  g_autoptr (GHashTable) macro_arrays       = NULL;
  g_autoptr (GHashTable) macro_replacements = NULL;
  guint n_anon_vals                         = 0;
  g_autoptr (GHashTable) type_hints         = NULL;

  g_return_val_if_fail (string != NULL, FALSE);

  spec         = bge_wdgt_spec_new ();
  macro_arrays = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
  macro_replacements = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, g_free);
  type_hints = g_hash_table_new_full (
      g_str_hash, g_str_equal, g_free, NULL);

#define RETURN_ERROR_UNLESS(_cond)     \
  G_STMT_START                         \
  {                                    \
    if (!(_cond))                      \
      {                                \
        g_set_error (                  \
            error,                     \
            G_IO_ERROR,                \
            G_IO_ERROR_UNKNOWN,        \
            "%s",                      \
            local_error != NULL        \
                ? local_error->message \
                : "???");              \
        return NULL;                   \
      }                                \
  }                                    \
  G_STMT_END

#define EXPECT_TOKEN(_string, _token)            \
  G_STMT_START                                   \
  {                                              \
    if (g_strcmp0 ((_string), (_token)) != 0)    \
      {                                          \
        g_set_error (                            \
            error,                               \
            G_IO_ERROR,                          \
            G_IO_ERROR_UNKNOWN,                  \
            "wdgt fmt parser error: "            \
            "expected token \"%s\", got \"%s\"", \
            (_token), (_string));                \
        return NULL;                             \
      }                                          \
  }                                              \
  G_STMT_END

#define UNEXPECTED_TOKEN(_token)              \
  G_STMT_START                                \
  {                                           \
    g_set_error (                             \
        error,                                \
        G_IO_ERROR,                           \
        G_IO_ERROR_UNKNOWN,                   \
        "Unexpected token \"%s\"", (_token)); \
    return NULL;                              \
  }                                           \
  G_STMT_END

  result = bge_wdgt_spec_add_measure_for_size_source_value (
      spec, "%for-size%", &local_error);
  RETURN_ERROR_UNLESS (result);
  g_hash_table_replace (
      type_hints,
      g_strdup ("%for-size%"),
      GSIZE_TO_POINTER (G_TYPE_INT));

  result = bge_wdgt_spec_add_widget_width_source_value (
      spec, "%width%", &local_error);
  RETURN_ERROR_UNLESS (result);
  g_hash_table_replace (
      type_hints,
      g_strdup ("%width%"),
      GSIZE_TO_POINTER (G_TYPE_INT));

  result = bge_wdgt_spec_add_widget_height_source_value (
      spec, "%height%", &local_error);
  RETURN_ERROR_UNLESS (result);
  g_hash_table_replace (
      type_hints,
      g_strdup ("%height%"),
      GSIZE_TO_POINTER (G_TYPE_INT));

  result = bge_wdgt_spec_add_tick_time_source_value (
      spec, "%tick%", &local_error);
  RETURN_ERROR_UNLESS (result);
  g_hash_table_replace (
      type_hints,
      g_strdup ("%tick%"),
      GSIZE_TO_POINTER (G_TYPE_DOUBLE));

  for (const char *p = string;
       !IS_EOF (p);)
    {
      g_autofree char *token = NULL;

      token = consume_token (
          &p,
          SINGLE_CHAR_TOKENS,
          TOKEN_PARSE_DEFAULT,
          NULL, NULL, NULL);
      if (token == NULL)
        break;

#define GET_TOKEN_FULL(_token_out, _flags, _single_chars, _was_quoted) \
  G_STMT_START                                                         \
  {                                                                    \
    g_clear_pointer ((_token_out), g_free);                            \
    *(_token_out) = consume_token (                                    \
        &p,                                                            \
        (_single_chars),                                               \
        (_flags),                                                      \
        (_was_quoted),                                                 \
        macro_replacements,                                            \
        &local_error);                                                 \
    RETURN_ERROR_UNLESS (*(_token_out) != NULL);                       \
  }                                                                    \
  G_STMT_END

#define GET_TOKEN(_token_out, _flags) \
  GET_TOKEN_FULL (_token_out, _flags, SINGLE_CHAR_TOKENS, NULL)

#define GET_TOKEN_EXPECT(_token_out, _flags, _expect) \
  G_STMT_START                                        \
  {                                                   \
    GET_TOKEN ((_token_out), (_flags));               \
    EXPECT_TOKEN (*(_token_out), (_expect));          \
  }                                                   \
  G_STMT_END

      if (g_strcmp0 (token, STR_DEFWIDGET) == 0)
        {
          g_autofree char *widget_name = NULL;

          GET_TOKEN (&widget_name, TOKEN_PARSE_QUOTED);
          bge_wdgt_spec_set_name (spec, widget_name);

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
          p = parse_widget_block (
              p, spec, macro_arrays, macro_replacements,
              &n_anon_vals, type_hints, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);
        }
      else if (g_strcmp0 (token, STR_DEFARRAY) == 0)
        {
          g_autofree char *array_name = NULL;
          guint            n_elements = 0;
          g_auto (GStrv) elements     = NULL;
          g_autoptr (GPtrArray) array = NULL;

          GET_TOKEN (&array_name, TOKEN_PARSE_DEFAULT);

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, NULL, NULL, macro_replacements,
                          &n_anon_vals, type_hints,
                          NULL, NULL, 0, &elements, NULL, &n_elements,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          array = g_ptr_array_new_take_null_terminated (
              (gpointer *) g_steal_pointer (&elements),
              g_free);
          g_hash_table_replace (
              macro_arrays,
              g_steal_pointer (&array_name),
              g_steal_pointer (&array));
        }
      else
        UNEXPECTED_TOKEN (token);
    }

  return g_steal_pointer (&spec);
}

static const char *
parse_widget_block (const char  *p,
                    BgeWdgtSpec *spec,
                    GHashTable  *macro_arrays,
                    GHashTable  *macro_replacements,
                    guint       *n_anon_vals,
                    GHashTable  *type_hints,
                    GError     **error)
{
  g_autoptr (GError) local_error = NULL;
  gboolean         result        = FALSE;
  g_autofree char *token         = NULL;

  for (;;)
    {
      GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
      if (g_strcmp0 (token, "}") == 0)
        return p;
      else if (g_strcmp0 (token, STR_FOREACH) == 0)
        {
          g_autofree char *var_name            = NULL;
          g_autofree char *iterator_name       = NULL;
          g_autofree char *iterator_paste_name = NULL;
          g_autofree char *array_name          = NULL;
          GPtrArray       *array               = NULL;
          const char      *fixed_p             = NULL;

          GET_TOKEN (&var_name, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ",");
          GET_TOKEN (&iterator_name, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ",");
          GET_TOKEN (&iterator_paste_name, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "IN");
          GET_TOKEN (&array_name, TOKEN_PARSE_DEFAULT);

          array = g_hash_table_lookup (macro_arrays, array_name);
          if (array == NULL)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "macro array \"%s\" is undefined",
                  array_name);
              return NULL;
            }

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
          fixed_p = p;
          for (guint i = 0; i < array->len; i++)
            {
              const char      *element        = NULL;
              g_autofree char *iterator       = NULL;
              g_autofree char *iterator_paste = NULL;
              GValue           iter_value     = G_VALUE_INIT;

              element = g_ptr_array_index (array, i);
              g_hash_table_replace (
                  macro_replacements,
                  g_strdup (var_name),
                  g_strdup (element));

              iterator = make_anon_name ((*n_anon_vals)++);
              g_value_set_uint (g_value_init (&iter_value, G_TYPE_UINT), i);
              result = bge_wdgt_spec_add_constant_source_value (
                  spec,
                  iterator,
                  &iter_value,
                  &local_error);
              g_value_unset (&iter_value);
              RETURN_ERROR_UNLESS (result);
              g_hash_table_replace (
                  macro_replacements,
                  g_strdup (iterator_name),
                  g_steal_pointer (&iterator));

              iterator_paste = g_strdup_printf ("%u", i);
              g_hash_table_replace (
                  macro_replacements,
                  g_strdup (iterator_paste_name),
                  g_steal_pointer (&iterator_paste));

              p = parse_widget_block (
                  fixed_p, spec, macro_arrays, macro_replacements,
                  n_anon_vals, type_hints, &local_error);
              RETURN_ERROR_UNLESS (p != NULL);
            }
          g_hash_table_remove (macro_replacements, var_name);
          g_hash_table_remove (macro_replacements, iterator_name);
          g_hash_table_remove (macro_replacements, iterator_paste_name);
        }
      else if (g_strcmp0 (token, STR_VARIABLE) == 0)
        {
          g_autofree char *name        = NULL;
          g_autofree char *type_string = NULL;
          GType            type        = 0;

          GET_TOKEN (&name, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ":");
          GET_TOKEN (&type_string, TOKEN_PARSE_QUOTED);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ";");

          type   = g_type_from_name (type_string);
          result = bge_wdgt_spec_add_variable_value (
              spec, type, name, &local_error);
          RETURN_ERROR_UNLESS (result);

          g_hash_table_replace (type_hints,
                                g_steal_pointer (&name),
                                GSIZE_TO_POINTER (type));
        }
      else if (g_strcmp0 (token, STR_REFERENCE) == 0)
        {
          g_autofree char *name        = NULL;
          g_autofree char *type_string = NULL;
          GType            type        = 0;

          GET_TOKEN (&name, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ":");
          GET_TOKEN (&type_string, TOKEN_PARSE_QUOTED);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ";");

          type   = g_type_from_name (type_string);
          result = bge_wdgt_spec_add_reference_object_value (
              spec, type, name, &local_error);
          RETURN_ERROR_UNLESS (result);

          g_hash_table_replace (type_hints,
                                g_steal_pointer (&name),
                                GSIZE_TO_POINTER (type));
        }
      else if (g_strcmp0 (token, STR_INIT) == 0 ||
               g_strcmp0 (token, STR_STATE) == 0 ||
               g_strcmp0 (token, STR_DEFAULT_STATE) == 0)
        {
          g_autofree char *state_name = NULL;

          if (g_strcmp0 (token, STR_INIT) != 0)
            {
              GET_TOKEN (&state_name, TOKEN_PARSE_QUOTED);

              result = bge_wdgt_spec_add_state (
                  spec,
                  state_name,
                  g_strcmp0 (token, STR_DEFAULT_STATE) == 0,
                  &local_error);
              /* TODO: allow states to be redefined for macros, so they are just
                 concatenated */
              g_clear_error (&local_error);
              // RETURN_ERROR_UNLESS (result);
            }

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
          for (;;)
            {
              GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
              if (g_strcmp0 (token, "}") == 0)
                break;
              else if (g_strcmp0 (token, STR_SNAPSHOT) == 0)
                {
                  GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
                  p = parse_snapshot_block (p, spec, state_name, macro_replacements, n_anon_vals,
                                            type_hints, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                }
              else if (g_strcmp0 (token, STR_SET) == 0)
                {
                  guint n_dest_values            = 0;
                  g_auto (GStrv) dest_values     = NULL;
                  g_autofree GType *dest_types   = NULL;
                  guint             n_src_values = 0;
                  g_auto (GStrv) src_values      = NULL;

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals,
                                  type_hints, NULL, NULL, 0, &dest_values,
                                  &dest_types, &n_dest_values, ARGS_PARSE_LEFT_ASSIGN,
                                  &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_dest_values == 0)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "Left assignment needs at least one argument");
                      return NULL;
                    }

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals,
                                  type_hints, (const char *const *) dest_values,
                                  dest_types, n_dest_values, &src_values, NULL,
                                  &n_src_values, ARGS_PARSE_RIGHT_ASSIGN, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_src_values != n_dest_values)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "Right assignment needs %d argument(s) "
                          "to match the left side",
                          n_dest_values);
                      return NULL;
                    }

                  for (guint i = 0; i < n_dest_values; i++)
                    {
                      result = bge_wdgt_spec_set_value (
                          spec,
                          state_name,
                          dest_values[i],
                          src_values[i],
                          &local_error);
                      RETURN_ERROR_UNLESS (result);
                    }
                }
              else if (g_strcmp0 (token, STR_TRANSITION) == 0)
                {
                  g_autofree char *transition_value = NULL;
                  guint            n_spec_values    = 0;
                  g_auto (GStrv) spec_values        = NULL;

                  GET_TOKEN (&transition_value, TOKEN_PARSE_DEFAULT);

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                                  (GType[]){ G_TYPE_DOUBLE, BGE_TYPE_EASING }, 2,
                                  &spec_values, NULL, &n_spec_values, ARGS_PARSE_RIGHT_ASSIGN, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_spec_values != 2)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "transition spec needs 2 arguments "
                          "(animation length in seconds, easing type), got %u",
                          n_spec_values);
                      return NULL;
                    }

                  result = bge_wdgt_spec_transition_value (
                      spec,
                      state_name,
                      transition_value,
                      spec_values[0],
                      spec_values[1],
                      &local_error);
                  RETURN_ERROR_UNLESS (result);
                }
              else if (g_strcmp0 (token, STR_TRANSITION_SPRING) == 0)
                {
                  g_autofree char *transition_value = NULL;
                  guint            n_spec_values    = 0;
                  g_auto (GStrv) spec_values        = NULL;

                  GET_TOKEN (&transition_value, TOKEN_PARSE_DEFAULT);

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                                  (GType[]){ G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE }, 3,
                                  &spec_values, NULL, &n_spec_values, ARGS_PARSE_RIGHT_ASSIGN, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_spec_values != 3)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "spring transition spec needs 3 arguments "
                          "(damping-ratio, mass, stiffness), got %u",
                          n_spec_values);
                      return NULL;
                    }

                  result = bge_wdgt_spec_transition_value_spring (
                      spec,
                      state_name,
                      transition_value,
                      spec_values[0],
                      spec_values[1],
                      spec_values[2],
                      &local_error);
                  RETURN_ERROR_UNLESS (result);
                }
              else if (g_strcmp0 (token, STR_ALLOCATE) == 0)
                {
                  g_autofree char *child_value         = NULL;
                  guint            n_allocation_values = 0;
                  g_auto (GStrv) allocation_values     = NULL;

                  GET_TOKEN (&child_value, TOKEN_PARSE_DEFAULT);

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                                  (GType[]){ G_TYPE_INT, G_TYPE_INT, GSK_TYPE_TRANSFORM }, 3,
                                  &allocation_values, NULL, &n_allocation_values,
                                  ARGS_PARSE_RIGHT_ASSIGN, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_allocation_values != 2 &&
                      n_allocation_values != 3)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "allocation needs 2 or 3 values, a width "
                          "and height and one transform, got %u",
                          n_allocation_values);
                      return NULL;
                    }

                  for (guint i = 0; i < n_allocation_values; i++)
                    {
                      g_autofree char *allocation_key = NULL;

                      allocation_key = make_widget_allocation_name (child_value, (*n_anon_vals)++);
                      switch (i)
                        {
                        case 0:
                          result = bge_wdgt_spec_add_allocation_width_value (
                              spec, allocation_key, child_value, &local_error);
                          break;
                        case 1:
                          result = bge_wdgt_spec_add_allocation_height_value (
                              spec, allocation_key, child_value, &local_error);
                          break;
                        case 2:
                          result = bge_wdgt_spec_add_allocation_transform_value (
                              spec, allocation_key, child_value, &local_error);
                          break;
                        default:
                          g_assert_not_reached ();
                        }
                      RETURN_ERROR_UNLESS (result);

                      result = bge_wdgt_spec_set_value (
                          spec,
                          state_name,
                          allocation_key,
                          allocation_values[i],
                          &local_error);
                      RETURN_ERROR_UNLESS (result);
                    }
                }
              else if (g_strcmp0 (token, STR_MEASURE) == 0)
                {
                  guint n_measurement_values        = 0;
                  g_auto (GStrv) measurement_values = NULL;

                  p = parse_args (p, spec, state_name, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                                  (GType[]){ G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT }, 4,
                                  &measurement_values, NULL, &n_measurement_values,
                                  ARGS_PARSE_RIGHT_ASSIGN, &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);
                  if (n_measurement_values != 4)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "measurement needs 4 values, a "
                          "minimum width, "
                          "natural width, "
                          "minimum height, "
                          "and "
                          "natural height, "
                          "got %u",
                          n_measurement_values);
                      return NULL;
                    }

                  for (guint i = 0; i < n_measurement_values; i++)
                    {
                      g_autofree char *measurement_key = NULL;

                      measurement_key = make_widget_measurement_name ((*n_anon_vals)++);
                      switch (i)
                        {
                        case 0:
                          result = bge_wdgt_spec_add_measure_value (
                              spec, measurement_key, BGE_WDGT_MEASURE_MINIMUM_WIDTH, &local_error);
                          break;
                        case 1:
                          result = bge_wdgt_spec_add_measure_value (
                              spec, measurement_key, BGE_WDGT_MEASURE_NATURAL_WIDTH, &local_error);
                          break;
                        case 2:
                          result = bge_wdgt_spec_add_measure_value (
                              spec, measurement_key, BGE_WDGT_MEASURE_MINIMUM_HEIGHT, &local_error);
                          break;
                        case 3:
                          result = bge_wdgt_spec_add_measure_value (
                              spec, measurement_key, BGE_WDGT_MEASURE_NATURAL_HEIGHT, &local_error);
                          break;
                        default:
                          g_assert_not_reached ();
                        }
                      RETURN_ERROR_UNLESS (result);

                      result = bge_wdgt_spec_set_value (
                          spec,
                          state_name,
                          measurement_key,
                          measurement_values[i],
                          &local_error);
                      RETURN_ERROR_UNLESS (result);
                    }
                }
              else
                UNEXPECTED_TOKEN (token);
            }
        }
      else
        UNEXPECTED_TOKEN (token);
    }

  return p;
}

static const char *
parse_snapshot_block (const char  *p,
                      BgeWdgtSpec *spec,
                      const char  *state,
                      GHashTable  *macro_replacements,
                      guint       *n_anon_vals,
                      GHashTable  *type_hints,
                      GError     **error)
{
  g_autoptr (GError) local_error = NULL;
  gboolean         result        = FALSE;
  g_autofree char *token         = NULL;

  for (;;)
    {
      g_autofree char         *action = NULL;
      BgeWdgtSnapshotInstrKind kind   = 0;
      g_autofree char         *instr  = NULL;
      guint                    n_args = 0;
      g_auto (GStrv) args             = NULL;
      guint n_pops                    = 0;

      GET_TOKEN (&action, TOKEN_PARSE_DEFAULT);
      if (g_strcmp0 (action, "}") == 0)
        break;
      else if (g_strcmp0 (action, "foreach") == 0)
        {
          g_autofree char *var                 = NULL;
          g_autofree char *element_type_string = NULL;
          g_autofree char *idx_var             = NULL;
          g_autofree char *model               = NULL;
          GType            element_type        = G_TYPE_INVALID;

          GET_TOKEN (&var, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ":");
          GET_TOKEN (&element_type_string, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ",");
          GET_TOKEN (&idx_var, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "in");
          GET_TOKEN (&model, TOKEN_PARSE_DEFAULT);

          element_type = g_type_from_name (element_type_string);

          result = bge_wdgt_spec_push_foreach (
              spec, model, var, idx_var, element_type, &local_error);
          RETURN_ERROR_UNLESS (result);

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
          p = parse_snapshot_block (p, spec, state, macro_replacements,
                                    n_anon_vals, type_hints, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          bge_wdgt_spec_pop_foreach (spec);
          continue;
        }
      else if (g_strcmp0 (action, "save") == 0)
        kind = BGE_WDGT_SNAPSHOT_INSTR_SAVE;
      else if (g_strcmp0 (action, "with") == 0)
        kind = BGE_WDGT_SNAPSHOT_INSTR_PUSH;
      else if (g_strcmp0 (action, "add") == 0)
        kind = BGE_WDGT_SNAPSHOT_INSTR_APPEND;
      else if (g_strcmp0 (action, "move") == 0)
        kind = BGE_WDGT_SNAPSHOT_INSTR_TRANSFORM;
      else if (g_strcmp0 (action, "do-child") == 0)
        kind = BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD;
      else
        UNEXPECTED_TOKEN (action);

      if (kind == BGE_WDGT_SNAPSHOT_INSTR_SAVE)
        {
          result = bge_wdgt_spec_append_snapshot_instr (
              spec, state, BGE_WDGT_SNAPSHOT_INSTR_SAVE,
              "save", NULL, 0, NULL, &local_error);
          RETURN_ERROR_UNLESS (result);
        }
      else if (kind == BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD)
        {
          p = parse_args (p, spec, state, NULL, macro_replacements,
                          n_anon_vals, type_hints,
                          NULL, NULL, 0, &args, NULL, &n_args,
                          ARGS_PARSE_RIGHT_ASSIGN, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          result = bge_wdgt_spec_append_snapshot_instr (
              spec, state, BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD,
              "do-child", (const char *const *) args, n_args, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);
        }
      else
        {
          GET_TOKEN (&instr, TOKEN_PARSE_DEFAULT);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements,
                          n_anon_vals, type_hints,
                          NULL, NULL, 0, &args, NULL, &n_args,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          result = bge_wdgt_spec_append_snapshot_instr (
              spec, state, kind,
              instr, (const char *const *) args, n_args,
              &n_pops, &local_error);
          RETURN_ERROR_UNLESS (result);
        }

      if (kind == BGE_WDGT_SNAPSHOT_INSTR_APPEND ||
          kind == BGE_WDGT_SNAPSHOT_INSTR_TRANSFORM)
        GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ";");
      else if (kind == BGE_WDGT_SNAPSHOT_INSTR_PUSH)
        {
          for (guint i = 0; i < n_pops; i++)
            {
              if (i > 0)
                GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "then");

              GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
              p = parse_snapshot_block (p, spec, state, macro_replacements,
                                        n_anon_vals, type_hints, &local_error);
              RETURN_ERROR_UNLESS (p != NULL);

              result = bge_wdgt_spec_append_snapshot_instr (
                  spec, state, BGE_WDGT_SNAPSHOT_INSTR_POP,
                  "pop", NULL, 0, NULL, &local_error);
              RETURN_ERROR_UNLESS (result);
            }
        }
      else if (kind == BGE_WDGT_SNAPSHOT_INSTR_SAVE)
        {
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "{");
          p = parse_snapshot_block (p, spec, state, macro_replacements,
                                    n_anon_vals, type_hints, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          result = bge_wdgt_spec_append_snapshot_instr (
              spec, state, BGE_WDGT_SNAPSHOT_INSTR_RESTORE,
              "restore", NULL, 0, NULL, &local_error);
          RETURN_ERROR_UNLESS (result);
        }
    }

  return p;
}

static gdouble
floor_closure (gpointer this,
               gdouble  x,
               gpointer data)
{
  return floor (x);
}

static gdouble
ceil_closure (gpointer this,
              gdouble  x,
              gpointer data)
{
  return ceil (x);
}

static gdouble
sin_closure (gpointer this,
             gdouble  x,
             gpointer data)
{
  return sin (x);
}

static gdouble
cos_closure (gpointer this,
             gdouble  x,
             gpointer data)
{
  return cos (x);
}

static const char *
parse_eval (const char  *p,
            BgeWdgtSpec *spec,
            const char  *state,
            GHashTable  *macro_replacements,
            guint       *n_anon_vals,
            GHashTable  *type_hints,
            char       **value_out,
            GError     **error)
{
  g_autoptr (GError) local_error   = NULL;
  gboolean         result          = FALSE;
  g_autofree char *token           = NULL;
  g_autoptr (GPtrArray) values     = NULL;
  g_autoptr (GArray) ops           = NULL;
  g_autoptr (GArray) value_types   = NULL;
  g_autofree char *key             = NULL;
  g_autoptr (EvalClosureData) data = NULL;

  values      = g_ptr_array_new_with_free_func (g_free);
  ops         = g_array_new (FALSE, FALSE, sizeof (EvalOperator));
  value_types = g_array_new (FALSE, FALSE, sizeof (GType));

#define GET_TOKEN_EVAL(_token_out, _flags) \
  GET_TOKEN_FULL (_token_out, _flags, EVAL_SINGLE_CHAR_TOKENS, NULL)

#define GET_TOKEN_EVAL_EXPECT(_token_out, _flags, _expect) \
  G_STMT_START                                             \
  {                                                        \
    GET_TOKEN_EVAL ((_token_out), (_flags));               \
    EXPECT_TOKEN (*(_token_out), (_expect));               \
  }                                                        \
  G_STMT_END

  for (gboolean apply_negative = FALSE;;)
    {
      g_autofree char *value          = NULL;
      Operator         op             = -1;
      gboolean         expected_value = FALSE;

      GET_TOKEN_EVAL (&token, TOKEN_PARSE_DEFAULT);
      if (g_strcmp0 (token, ")") == 0)
        break;
      else if (g_strcmp0 (token, "(") == 0)
        {
          p = parse_eval (p, spec, state, macro_replacements, n_anon_vals,
                          type_hints, &value, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);
        }
      else if (g_strcmp0 (token, "#") == 0)
        {
          g_auto (GStrv) escape_args = NULL;
          guint n_escape_args        = 0;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints,
                          NULL, (GType[]){ G_TYPE_DOUBLE }, 1, &escape_args,
                          NULL, &n_escape_args, ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);
          if (n_escape_args != 1)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "evaluation escape "
                  "needs a single argument");
              return NULL;
            }

          value = g_strdup (escape_args[0]);
        }
      else if (g_strcmp0 (token, "ceil") == 0 ||
               g_strcmp0 (token, "floor") == 0 ||
               g_strcmp0 (token, "sin") == 0 ||
               g_strcmp0 (token, "cos") == 0)
        {
          GCallback        cb            = NULL;
          g_autofree char *arg           = NULL;
          g_autofree char *math_func_key = NULL;

          if (g_strcmp0 (token, "ceil") == 0)
            cb = G_CALLBACK (ceil_closure);
          else if (g_strcmp0 (token, "floor") == 0)
            cb = G_CALLBACK (floor_closure);
          else if (g_strcmp0 (token, "sin") == 0)
            cb = G_CALLBACK (sin_closure);
          else if (g_strcmp0 (token, "cos") == 0)
            cb = G_CALLBACK (cos_closure);

          GET_TOKEN_EVAL_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_eval (p, spec, state, macro_replacements, n_anon_vals,
                          type_hints, &arg, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          math_func_key = make_anon_name ((*n_anon_vals)++);
          result        = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              math_func_key,
              G_TYPE_DOUBLE,
              bge_marshal_DOUBLE__DOUBLE,
              cb,
              (const char *const[]){ arg },
              (GType[]){ G_TYPE_DOUBLE },
              1,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          value = g_steal_pointer (&math_func_key);
        }
      else if (g_strcmp0 (token, "+") == 0)
        op = OPERATOR_ADD;
      else if (g_strcmp0 (token, "-") == 0)
        op = OPERATOR_SUBTRACT;
      else if (g_strcmp0 (token, "*") == 0)
        op = OPERATOR_MULTIPLY;
      else if (g_strcmp0 (token, "/") == 0)
        op = OPERATOR_DIVIDE;
      else if (g_strcmp0 (token, "%") == 0)
        op = OPERATOR_MODULUS;
      else if (g_strcmp0 (token, "^") == 0)
        op = OPERATOR_POWER;
      else
        {
          value = parse_token_fundamental (
              token, spec, n_anon_vals, &local_error);
          RETURN_ERROR_UNLESS (value != NULL);
        }

      expected_value = values->len == ops->len;
      if (value != NULL)
        {
          GType type_double = G_TYPE_DOUBLE;

          if (!expected_value)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "Expected operator, got \"%s\"", token);
              return NULL;
            }

          if (apply_negative)
            {
              GValue           gvalue       = G_VALUE_INIT;
              g_autofree char *negative_key = NULL;
              EvalOperator     append       = { 0 };

              g_value_set_double (g_value_init (&gvalue, G_TYPE_DOUBLE), -1.0);
              negative_key = make_anon_name ((*n_anon_vals)++);
              result       = bge_wdgt_spec_add_constant_source_value (
                  spec,
                  negative_key,
                  &gvalue,
                  &local_error);
              g_value_unset (&gvalue);
              RETURN_ERROR_UNLESS (result);

              g_ptr_array_add (values, g_steal_pointer (&negative_key));
              g_array_append_val (value_types, type_double);

              append.op  = OPERATOR_MULTIPLY;
              append.pos = ops->len;
              g_array_append_val (ops, append);

              apply_negative = FALSE;
            }

          g_ptr_array_add (values, g_steal_pointer (&value));
          g_array_append_val (value_types, type_double);
        }
      else if (op >= 0)
        {
          EvalOperator append = { 0 };

          if (expected_value)
            {
              if (op == OPERATOR_SUBTRACT)
                apply_negative = !apply_negative;
              else if (op != OPERATOR_ADD)
                /* Allow adding `+` in front of numbers for alignment with
                   negatives etc */
                {
                  g_set_error (
                      error,
                      G_IO_ERROR,
                      G_IO_ERROR_UNKNOWN,
                      "Expected value, got \"%s\"", token);
                  return NULL;
                }
            }
          else
            {
              append.op  = op;
              append.pos = ops->len;
              g_array_append_val (ops, append);
            }
        }
    }

  if (values->len == 0)
    {
      g_set_error (
          error,
          G_IO_ERROR,
          G_IO_ERROR_UNKNOWN,
          "Empty evaluation block");
      return NULL;
    }
  if (ops->len != values->len - 1)
    {
      g_set_error (
          error,
          G_IO_ERROR,
          G_IO_ERROR_UNKNOWN,
          "Invalid syntax in evaluation block");
      return NULL;
    }

  if (values->len == 1)
    {
      *value_out = g_ptr_array_steal_index (values, 0);
      return p;
    }

  g_array_sort (ops, (GCompareFunc) cmp_operator);

  data           = eval_closure_data_new ();
  data->ops      = g_array_ref (ops);
  data->workbuf0 = g_malloc0_n (values->len, sizeof (gdouble));
  data->workbuf1 = g_malloc0_n (values->len, sizeof (gdouble));

  key    = make_anon_name ((*n_anon_vals)++);
  result = bge_wdgt_spec_add_cclosure_source_value (
      spec, key, G_TYPE_DOUBLE,
      _marshal_DOUBLE__ARGS_DIRECT,
      G_CALLBACK (eval_closure),
      (const char *const *) values->pdata,
      (const GType *) (void *) value_types->data,
      values->len,
      eval_closure_data_ref (data),
      eval_closure_data_unref,
      &local_error);
  RETURN_ERROR_UNLESS (result);

  *value_out = g_steal_pointer (&key);
  return p;
}

static gboolean
not_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  for (guint i = 0; i < n_param_values; i++)
    {
      if (!g_value_get_boolean (&param_values[i]))
        return TRUE;
    }
  return FALSE;
}

static gboolean
and_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  for (guint i = 0; i < n_param_values; i++)
    {
      if (!g_value_get_boolean (&param_values[i]))
        return FALSE;
    }
  return TRUE;
}

static gboolean
or_closure (gpointer         this,
            guint            n_param_values,
            const GValue    *param_values,
            EvalClosureData *data)
{
  for (guint i = 0; i < n_param_values; i++)
    {
      if (g_value_get_boolean (&param_values[i]))
        return TRUE;
    }
  return FALSE;
}

static gboolean
lt_closure (gpointer         this,
            guint            n_param_values,
            const GValue    *param_values,
            EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with < g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
gt_closure (gpointer         this,
            guint            n_param_values,
            const GValue    *param_values,
            EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with > g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
lte_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with <= g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
gte_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with >= g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
eq_closure (gpointer         this,
            guint            n_param_values,
            const GValue    *param_values,
            EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with == g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
neq_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(cmp_with != g_value_get_double (&param_values[i])))
        return FALSE;
    }
  return TRUE;
}

static gboolean
aeq_closure (gpointer         this,
             guint            n_param_values,
             const GValue    *param_values,
             EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(G_APPROX_VALUE (cmp_with, g_value_get_double (&param_values[i]), 0.00001)))
        return FALSE;
    }
  return TRUE;
}

static gboolean
aneq_closure (gpointer         this,
              guint            n_param_values,
              const GValue    *param_values,
              EvalClosureData *data)
{
  gdouble cmp_with = 0.0;

  cmp_with = g_value_get_double (param_values);
  for (guint i = 1; i < n_param_values; i++)
    {
      if (!(!G_APPROX_VALUE (cmp_with, g_value_get_double (&param_values[i]), 0.00001)))
        return FALSE;
    }
  return TRUE;
}

static void
ifelse_closure (gpointer      this,
                GValue       *return_value,
                guint         n_param_values,
                const GValue *param_values,
                gpointer      dest_type_ptr)
{
  if (g_value_get_boolean (param_values))
    g_value_copy (&param_values[1], return_value);
  else
    g_value_copy (&param_values[2], return_value);
}

static void
measure_path_closure (gpointer      this,
                      GValue       *return_value,
                      guint         n_param_values,
                      const GValue *param_values,
                      gpointer      dest_type_ptr)
{
  GskPathMeasure *measure = NULL;

  measure = gsk_path_measure_new (
      g_value_get_boxed (param_values));
  g_value_take_boxed (return_value, measure);
}

static void
path_length_closure (gpointer      this,
                     GValue       *return_value,
                     guint         n_param_values,
                     const GValue *param_values,
                     gpointer      dest_type_ptr)
{
  gdouble length = 0.0;

  length = gsk_path_measure_get_length (
      g_value_get_boxed (param_values));
  g_value_set_double (return_value, length);
}

static void
path_point_closure (gpointer      this,
                    GValue       *return_value,
                    guint         n_param_values,
                    const GValue *param_values,
                    gpointer      dest_type_ptr)
{
  gboolean     result = FALSE;
  GskPathPoint point  = { 0 };

  result = gsk_path_measure_get_point (
      g_value_get_boxed (&param_values[0]),
      g_value_get_double (&param_values[1]),
      &point);
  if (!result)
    return;

  g_value_set_boxed (return_value, &point);
}

static const char *
parse_args (const char        *p,
            BgeWdgtSpec       *spec,
            const char        *state,
            const char        *enclosing_object,
            GHashTable        *macro_replacements,
            guint             *n_anon_vals,
            GHashTable        *type_hints,
            const char *const *destinations,
            GType              destinations_types[],
            guint              n_destinations,
            char            ***values_out,
            GType            **types_out,
            guint             *n_out,
            ArgsParseKind      parse_kind,
            GError           **error)
{
  g_autoptr (GError) local_error   = NULL;
  gboolean         result          = FALSE;
  g_autofree char *token           = NULL;
  guint            n_args          = 0;
  g_autoptr (GStrvBuilder) builder = NULL;
  g_autoptr (GArray) types_array   = NULL;

  builder     = g_strv_builder_new ();
  types_array = g_array_new (FALSE, TRUE, sizeof (GType));

  for (gboolean need_comma = FALSE,
                get_token  = TRUE,
                was_quoted = FALSE;
       ;)
    {
      if (get_token)
        GET_TOKEN_FULL (&token, TOKEN_PARSE_DEFAULT, SINGLE_CHAR_TOKENS, &was_quoted);
      get_token = TRUE;
      if (was_quoted)
        {
          g_autofree char *tmp_token = NULL;
          g_autofree char *key       = NULL;
          GValue           value     = { 0 };

          g_value_set_string (g_value_init (&value, G_TYPE_STRING), token);

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_constant_source_value (
              spec, key, &value, &local_error);
          g_value_unset (&value);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;

          need_comma = TRUE;
        }
      else if ((parse_kind == ARGS_PARSE_LEFT_ASSIGN && g_strcmp0 (token, "=") == 0) ||
               (parse_kind == ARGS_PARSE_RIGHT_ASSIGN && g_strcmp0 (token, ";") == 0) ||
               (parse_kind == ARGS_PARSE_PARENS && g_strcmp0 (token, ")") == 0))
        break;
      else if (need_comma)
        {
          if (g_strcmp0 (token, ",") == 0)
            {
              need_comma = FALSE;
              continue;
            }
          else
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "Arguments must be comma-separated");
              return NULL;
            }
        }
      else if (g_strcmp0 (token, "_") == 0)
        /* gettext translations */
        {
          g_autofree char *source_text = NULL;
          GValue           value       = G_VALUE_INIT;
          g_autofree char *key         = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          GET_TOKEN (&source_text, TOKEN_PARSE_QUOTED);
          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");

          g_value_set_string (g_value_init (&value, G_TYPE_STRING),
                              gettext (source_text));

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_constant_source_value (
              spec, key, &value, &local_error);
          g_value_unset (&value);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#eval") == 0)
        {
          g_autofree char *key = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_eval (p, spec, state, macro_replacements, n_anon_vals,
                          type_hints, &key, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#not") == 0 ||
               g_strcmp0 (token, "#!") == 0 ||
               g_strcmp0 (token, "#and") == 0 ||
               g_strcmp0 (token, "#or") == 0 ||
               g_strcmp0 (token, "#<") == 0 ||
               g_strcmp0 (token, "#>") == 0 ||
               g_strcmp0 (token, "#<eq") == 0 ||
               g_strcmp0 (token, "#>eq") == 0 ||
               g_strcmp0 (token, "#eq") == 0 ||
               g_strcmp0 (token, "#neq") == 0 ||
               g_strcmp0 (token, "#~eq") == 0 ||
               g_strcmp0 (token, "#~neq") == 0)
        {
          g_autofree char *key                  = NULL;
          GCallback        cb                   = NULL;
          GType            all_type             = G_TYPE_INVALID;
          guint            require_n_cmp_values = 0;
          guint            n_cmp_values         = 0;
          g_auto (GStrv) cmp_values             = NULL;
          g_autofree GType *all_types           = NULL;

          if (g_strcmp0 (token, "#not") == 0 ||
              g_strcmp0 (token, "#!") == 0)
            {
              cb                   = G_CALLBACK (not_closure);
              all_type             = G_TYPE_BOOLEAN;
              require_n_cmp_values = 1;
            }
          else if (g_strcmp0 (token, "#and") == 0)
            {
              cb                   = G_CALLBACK (and_closure);
              all_type             = G_TYPE_BOOLEAN;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#or") == 0)
            {
              cb                   = G_CALLBACK (or_closure);
              all_type             = G_TYPE_BOOLEAN;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#<") == 0)
            {
              cb                   = G_CALLBACK (lt_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#>") == 0)
            {
              cb                   = G_CALLBACK (gt_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#<eq") == 0)
            {
              cb                   = G_CALLBACK (lte_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#>eq") == 0)
            {
              cb                   = G_CALLBACK (gte_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#eq") == 0)
            {
              cb                   = G_CALLBACK (eq_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#neq") == 0)
            {
              cb                   = G_CALLBACK (neq_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#~eq") == 0)
            {
              cb                   = G_CALLBACK (aeq_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else if (g_strcmp0 (token, "#~neq") == 0)
            {
              cb                   = G_CALLBACK (aneq_closure);
              all_type             = G_TYPE_DOUBLE;
              require_n_cmp_values = 2;
            }
          else
            g_assert_not_reached ();

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          NULL, 0, &cmp_values, NULL, &n_cmp_values, ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          if (n_cmp_values < require_n_cmp_values)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "%s() needs at least %u argument(s), "
                  "got %u",
                  token, require_n_cmp_values, n_cmp_values);
              return NULL;
            }

          all_types = g_new (GType, n_cmp_values);
          for (guint i = 0; i < n_cmp_values; i++)
            {
              all_types[i] = all_type;
            }

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              key,
              G_TYPE_BOOLEAN,
              _marshal_BOOL__ARGS_DIRECT,
              cb, (const char *const *) cmp_values,
              all_types,
              n_cmp_values,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#ifelse") == 0)
        {
          g_autofree char *key           = NULL;
          GType            type_hint     = G_TYPE_INVALID;
          guint            n_expr_values = 0;
          g_auto (GStrv) expr_values     = NULL;
          g_autofree GType *expr_types   = NULL;

          if (n_args < n_destinations)
            {
              if (destinations != NULL)
                type_hint = GPOINTER_TO_SIZE (g_hash_table_lookup (
                    type_hints, destinations[n_args]));
              if (type_hint == G_TYPE_INVALID &&
                  destinations_types != NULL)
                type_hint = destinations_types[n_args];
            }

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          (GType[]){ G_TYPE_BOOLEAN }, 1,
                          &expr_values, &expr_types, &n_expr_values,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          if (n_expr_values != 3)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "#ifelse() needs exactly 3 argument(s), "
                  "got %u",
                  n_expr_values);
              return NULL;
            }

          if (type_hint == G_TYPE_INVALID)
            /* Use the return type of the TRUE value and coerce the FALSE
               value to this if necessary */
            type_hint = expr_types[1];

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              key,
              type_hint,
              _marshal_DIRECT__ARGS_DIRECT,
              G_CALLBACK (ifelse_closure),
              (const char *const *) expr_values,
              (GType[]){ G_TYPE_BOOLEAN, type_hint, type_hint },
              n_expr_values,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#measure-path") == 0)
        {
          g_autofree char *key           = NULL;
          guint            n_expr_values = 0;
          g_auto (GStrv) expr_values     = NULL;
          g_autofree GType *expr_types   = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          (GType[]){ GSK_TYPE_PATH }, 1,
                          &expr_values, &expr_types, &n_expr_values,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          if (n_expr_values != 1)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "#measure-path() needs exactly 1 argument of type %s,"
                  "got %u",
                  g_type_name (GSK_TYPE_PATH),
                  n_expr_values);
              return NULL;
            }

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              key,
              GSK_TYPE_PATH_MEASURE,
              _marshal_DIRECT__ARGS_DIRECT,
              G_CALLBACK (measure_path_closure),
              (const char *const *) expr_values,
              (GType[]){ GSK_TYPE_PATH },
              n_expr_values,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#path-length") == 0)
        {
          g_autofree char *key           = NULL;
          guint            n_expr_values = 0;
          g_auto (GStrv) expr_values     = NULL;
          g_autofree GType *expr_types   = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          (GType[]){ GSK_TYPE_PATH_MEASURE }, 1,
                          &expr_values, &expr_types, &n_expr_values,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          if (n_expr_values != 1)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "#path-length() needs exactly 1 argument of type %s,"
                  "got %u",
                  g_type_name (GSK_TYPE_PATH_MEASURE),
                  n_expr_values);
              return NULL;
            }

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              key,
              G_TYPE_DOUBLE,
              _marshal_DIRECT__ARGS_DIRECT,
              G_CALLBACK (path_length_closure),
              (const char *const *) expr_values,
              (GType[]){ GSK_TYPE_PATH_MEASURE },
              n_expr_values,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#path-point") == 0)
        {
          g_autofree char *key           = NULL;
          guint            n_expr_values = 0;
          g_auto (GStrv) expr_values     = NULL;
          g_autofree GType *expr_types   = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          (GType[]){ GSK_TYPE_PATH_MEASURE, G_TYPE_DOUBLE }, 2,
                          &expr_values, &expr_types, &n_expr_values,
                          ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);

          if (n_expr_values != 2)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "#path-point() needs exactly 2 arguments "
                  "of types %s and %s,"
                  "got %u",
                  g_type_name (GSK_TYPE_PATH_MEASURE),
                  g_type_name (G_TYPE_DOUBLE),
                  n_expr_values);
              return NULL;
            }

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_cclosure_source_value (
              spec,
              key,
              GSK_TYPE_PATH_POINT,
              _marshal_DIRECT__ARGS_DIRECT,
              G_CALLBACK (path_point_closure),
              (const char *const *) expr_values,
              (GType[]){ GSK_TYPE_PATH_MEASURE, G_TYPE_DOUBLE },
              n_expr_values,
              NULL, NULL,
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else if (g_strcmp0 (token, "#transition") == 0)
        {
          g_autofree char *key           = NULL;
          guint            n_spec_values = 0;
          g_auto (GStrv) spec_values     = NULL;

          GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
          p = parse_args (p, spec, state, NULL, macro_replacements, n_anon_vals, type_hints, NULL,
                          (GType[]){ G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE }, 4,
                          &spec_values, NULL, &n_spec_values, ARGS_PARSE_PARENS, &local_error);
          RETURN_ERROR_UNLESS (p != NULL);
          if (n_spec_values != 4)
            {
              g_set_error (
                  error,
                  G_IO_ERROR,
                  G_IO_ERROR_UNKNOWN,
                  "#transition() needs 4 arguments "
                  "(variable, damping-ratio, mass, stiffness), "
                  "got %u",
                  n_spec_values);
              return NULL;
            }

          key    = make_anon_name ((*n_anon_vals)++);
          result = bge_wdgt_spec_add_track_transition_source_value (
              spec,
              key,
              spec_values[0],
              spec_values[1],
              spec_values[2],
              spec_values[3],
              &local_error);
          RETURN_ERROR_UNLESS (result);

          g_strv_builder_take (builder, g_steal_pointer (&key));
          n_args++;
          need_comma = TRUE;
        }
      else
        {
          g_autofree char *key  = NULL;
          GType            type = G_TYPE_INVALID;

          if (g_hash_table_contains (type_hints, token) ||
              /* A macro placed this here */
              g_utf8_strchr (token, -1, '@') != NULL)
            key = g_steal_pointer (&token);
          else
            {
              GType    type_hint            = G_TYPE_INVALID;
              gboolean expect_closing_paren = FALSE;
              gboolean is_child             = FALSE;
              gboolean constant             = FALSE;
              GValue   value                = G_VALUE_INIT;

              if (n_args < n_destinations)
                {
                  if (destinations != NULL)
                    type_hint = GPOINTER_TO_SIZE (g_hash_table_lookup (
                        type_hints, destinations[n_args]));
                  if (type_hint == G_TYPE_INVALID &&
                      destinations_types != NULL)
                    type_hint = destinations_types[n_args];
                }

              if (g_str_has_prefix (token, "#"))
                {
                  const char *type_name = token + 1;

                  if (g_str_has_prefix (type_name, "child/"))
                    {
                      is_child = TRUE;
                      type_name += strlen ("child/");
                    }

                  if (*type_name == '\0')
                    {
                      if (type_hint == G_TYPE_INVALID)
                        {
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "Unable to guess type for value");
                          return NULL;
                        }
                      type = type_hint;
                    }
                  else
                    {
                      type = g_type_from_name (type_name);
                      if (type == G_TYPE_INVALID)
                        {
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "Unknown type name \"%s\"",
                              type_name);
                          return NULL;
                        }
                    }

                  GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
                  expect_closing_paren = TRUE;
                }
              else
                type = type_hint;

              if (is_child &&
                  !g_type_is_a (type, GTK_TYPE_WIDGET))
                {
                  g_set_error (
                      error,
                      G_IO_ERROR,
                      G_IO_ERROR_UNKNOWN,
                      "Children must be a widget type");
                  return NULL;
                }

              if (type == G_TYPE_INVALID)
                {
                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
                  key = parse_token_fundamental (token, spec, n_anon_vals, &local_error);
                  RETURN_ERROR_UNLESS (key != NULL);
                }
              else if (type == G_TYPE_STRING)
                {
                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);

                  g_value_set_string (g_value_init (&value, G_TYPE_STRING),
                                      token);
                  constant = TRUE;

                  if (expect_closing_paren)
                    GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");
                }
              else if (type == G_TYPE_BOOLEAN)
                {
                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);

                  if (g_strcmp0 (token, "true") == 0)
                    g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN),
                                         TRUE);
                  else if (g_strcmp0 (token, "false") == 0)
                    g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN),
                                         FALSE);
                  else
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "a boolean value must be 'true' "
                          "or 'false', got \"%s\"",
                          token);
                      return NULL;
                    }
                  constant = TRUE;

                  if (expect_closing_paren)
                    GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");
                }
              else if (type == G_TYPE_INT ||
                       type == G_TYPE_INT64 ||
                       type == G_TYPE_UINT ||
                       type == G_TYPE_UINT64 ||
                       type == G_TYPE_FLOAT ||
                       type == G_TYPE_DOUBLE)
                {
                  const GVariantType *variant_type = NULL;
                  g_autoptr (GVariant) variant     = NULL;

                  if (type == G_TYPE_INT)
                    variant_type = G_VARIANT_TYPE_INT32;
                  else if (type == G_TYPE_INT64)
                    variant_type = G_VARIANT_TYPE_INT64;
                  else if (type == G_TYPE_UINT)
                    variant_type = G_VARIANT_TYPE_UINT32;
                  else if (type == G_TYPE_UINT64)
                    variant_type = G_VARIANT_TYPE_UINT64;
                  else if (type == G_TYPE_FLOAT ||
                           type == G_TYPE_DOUBLE)
                    variant_type = G_VARIANT_TYPE_DOUBLE;

                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
                  variant = g_variant_parse (variant_type, token, NULL, NULL, &local_error);
                  RETURN_ERROR_UNLESS (variant != NULL);

                  if (type == G_TYPE_INT)
                    g_value_set_int (g_value_init (&value, G_TYPE_INT),
                                     g_variant_get_int32 (variant));
                  else if (type == G_TYPE_INT64)
                    g_value_set_int64 (g_value_init (&value, G_TYPE_INT64),
                                       g_variant_get_int64 (variant));
                  else if (type == G_TYPE_UINT)
                    g_value_set_uint (g_value_init (&value, G_TYPE_UINT),
                                      g_variant_get_uint32 (variant));
                  else if (type == G_TYPE_UINT64)
                    g_value_set_uint64 (g_value_init (&value, G_TYPE_UINT64),
                                        g_variant_get_uint64 (variant));
                  else if (type == G_TYPE_FLOAT)
                    g_value_set_float (g_value_init (&value, G_TYPE_FLOAT),
                                       g_variant_get_double (variant));
                  else if (type == G_TYPE_DOUBLE)
                    g_value_set_double (g_value_init (&value, G_TYPE_DOUBLE),
                                        g_variant_get_double (variant));
                  constant = TRUE;

                  if (expect_closing_paren)
                    GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");
                }
              else if (g_type_is_a (type, G_TYPE_ENUM))
                {
                  g_autoptr (GEnumClass) enum_class = NULL;
                  GEnumValue *enum_value            = NULL;

                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);

                  enum_class = g_type_class_ref (type);
                  enum_value = g_enum_get_value_by_nick (enum_class, token);
                  if (enum_value == NULL)
                    enum_value = g_enum_get_value_by_name (enum_class, token);
                  if (enum_value == NULL)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "\"%s\" not found in enum type %s",
                          token, g_type_name (type));
                      return NULL;
                    }

                  g_value_set_enum (g_value_init (&value, type),
                                    enum_value->value);
                  constant = TRUE;

                  if (expect_closing_paren)
                    GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");
                }
              else if (type == GDK_TYPE_RGBA)
                {
                  GdkRGBA rgba = { 0 };

                  if (expect_closing_paren)
                    GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
                  result = gdk_rgba_parse (&rgba, token);
                  if (!result)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "#color() specifier failed to "
                          "parse color from string");
                      return NULL;
                    }
                  g_value_set_boxed (g_value_init (&value, GDK_TYPE_RGBA), &rgba);
                  constant = TRUE;

                  if (expect_closing_paren)
                    GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ")");
                }
              else if (type == GRAPHENE_TYPE_POINT ||
                       type == GRAPHENE_TYPE_SIZE ||
                       type == GRAPHENE_TYPE_RECT)
                {
                  g_auto (GStrv) component_args = NULL;
                  guint n_component_args        = 0;
                  GType component_type          = 0;

                  if (!expect_closing_paren)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "type %s must be wrapped in #(...)",
                          g_type_name (type));
                      return NULL;
                    }

                  p = parse_args (p, spec, state, enclosing_object,
                                  macro_replacements,
                                  n_anon_vals, type_hints, NULL,
                                  (GType[]){ G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE }, 4,
                                  &component_args, NULL, &n_component_args, ARGS_PARSE_PARENS,
                                  &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);

                  if (type == GRAPHENE_TYPE_POINT)
                    {
                      switch (n_component_args)
                        {
                        case 2:
                          component_type = GRAPHENE_TYPE_POINT;
                          break;
                        case 3:
                          component_type = GRAPHENE_TYPE_POINT3D;
                          break;
                        default:
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "#point() specifier can have 2 or 3 arguments, got %u",
                              n_component_args);
                          return NULL;
                        }
                    }
                  else if (type == GRAPHENE_TYPE_SIZE)
                    {
                      switch (n_component_args)
                        {
                        case 2:
                          component_type = GRAPHENE_TYPE_SIZE;
                          break;
                        default:
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "#size() specifier can have 2 arguments, got %u",
                              n_component_args);
                          return NULL;
                        }
                    }
                  else if (type == GRAPHENE_TYPE_RECT)
                    {
                      switch (n_component_args)
                        {
                        case 4:
                          component_type = GRAPHENE_TYPE_RECT;
                          break;
                        default:
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "#rect() specifier can have 4 arguments, got %u",
                              n_component_args);
                          return NULL;
                        }
                    }

                  key    = make_anon_name ((*n_anon_vals)++);
                  result = bge_wdgt_spec_add_component_source_value (
                      spec, key, component_type,
                      (const char *const *) component_args,
                      n_component_args, &local_error);
                  RETURN_ERROR_UNLESS (result);
                }
              else if (g_type_is_a (type, G_TYPE_OBJECT))
                {
                  if (!expect_closing_paren)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "type %s must be wrapped in #(...)",
                          g_type_name (type));
                      return NULL;
                    }

                  key = make_anon_name ((*n_anon_vals)++);
                  if (is_child)
                    {
                      g_autofree char *builder_type     = NULL;
                      g_autoptr (GPtrArray) css_classes = NULL;

                      GET_TOKEN (&builder_type, TOKEN_PARSE_QUOTED);
                      if (*builder_type == '\0')
                        g_clear_pointer (&builder_type, g_free);

                      css_classes = g_ptr_array_new_with_free_func (g_free);
                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
                      for (;;)
                        {
                          GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
                          if (g_strcmp0 (token, ")") == 0)
                            break;
                          g_ptr_array_add (css_classes, g_steal_pointer (&token));
                        }

                      result = bge_wdgt_spec_add_child_source_value (
                          spec, key, type, enclosing_object, builder_type,
                          (const char *const *) css_classes->pdata, css_classes->len,
                          &local_error);
                      RETURN_ERROR_UNLESS (result);
                    }
                  else
                    {
                      result = bge_wdgt_spec_add_instance_source_value (
                          spec, key, type, &local_error);
                      RETURN_ERROR_UNLESS (result);
                    }

                  for (;;)
                    {
                      g_autofree char *property_name = NULL;
                      g_autofree char *set_key       = NULL;
                      GType            prop_type     = G_TYPE_INVALID;
                      g_auto (GStrv) value_args      = NULL;
                      guint n_value_args             = 0;

                      GET_TOKEN (&property_name, TOKEN_PARSE_DEFAULT);
                      if (g_strcmp0 (property_name, ")") == 0)
                        break;
                      else if (g_strcmp0 (property_name, "%set") == 0)
                        {
                          GET_TOKEN (&set_key, TOKEN_PARSE_DEFAULT);
                          prop_type = GPOINTER_TO_SIZE (g_hash_table_lookup (type_hints, set_key));
                        }
                      else if (g_strcmp0 (property_name, "_") != 0)
                        {
                          set_key = make_object_property_name (key, property_name, (*n_anon_vals)++);
                          result  = bge_wdgt_spec_add_property_value (
                              spec, set_key, key, property_name, &prop_type, &local_error);
                          RETURN_ERROR_UNLESS (result);
                        }
                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "=");

                      p = parse_args (p, spec, state, key, macro_replacements,
                                      n_anon_vals, type_hints,
                                      NULL, (GType[]){ prop_type }, 1, &value_args,
                                      NULL, &n_value_args, ARGS_PARSE_RIGHT_ASSIGN,
                                      &local_error);
                      RETURN_ERROR_UNLESS (p != NULL);

                      if (n_value_args != 1)
                        {
                          g_set_error (
                              error,
                              G_IO_ERROR,
                              G_IO_ERROR_UNKNOWN,
                              "property/child assignment "
                              "needs a single argument, got %u",
                              n_value_args);
                          return NULL;
                        }

                      if (set_key != NULL)
                        {
                          result = bge_wdgt_spec_set_value (spec, state, set_key,
                                                            value_args[0], &local_error);
                          RETURN_ERROR_UNLESS (result);
                        }
                    }
                }
              else if (type == GSK_TYPE_TRANSFORM)
                {
                  g_autofree char *last_key = NULL;

                  if (!expect_closing_paren)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "type %s must be wrapped in #(...)",
                          g_type_name (type));
                      return NULL;
                    }

                  last_key = make_anon_name ((*n_anon_vals)++);
                  g_value_take_boxed (g_value_init (&value, GSK_TYPE_TRANSFORM),
                                      gsk_transform_new ());
                  result = bge_wdgt_spec_add_constant_source_value (
                      spec, last_key, &value, error);
                  g_value_unset (&value);
                  if (!result)
                    return NULL;

                  for (;;)
                    {
                      g_autofree char *instr        = NULL;
                      g_auto (GStrv) value_args     = NULL;
                      guint            n_value_args = 0;
                      g_autofree char *tmp_key      = NULL;

                      GET_TOKEN (&instr, TOKEN_PARSE_DEFAULT);
                      if (g_strcmp0 (instr, ")") == 0)
                        break;

                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
                      p = parse_args (p, spec, state, enclosing_object,
                                      macro_replacements, n_anon_vals,
                                      type_hints, NULL, NULL, 0, &value_args, NULL,
                                      &n_value_args, ARGS_PARSE_PARENS, &local_error);
                      RETURN_ERROR_UNLESS (p != NULL);
                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ";");

                      tmp_key = make_anon_name ((*n_anon_vals)++);
                      result  = bge_wdgt_spec_add_transform_source_value (
                          spec, tmp_key, last_key, instr,
                          (const char *const *) value_args,
                          n_value_args, &local_error);
                      RETURN_ERROR_UNLESS (result);

                      g_clear_pointer (&last_key, g_free);
                      last_key = g_steal_pointer (&tmp_key);
                    }

                  key = g_steal_pointer (&last_key);
                }
              else if (type == GSK_TYPE_PATH)
                {
                  g_autoptr (GPtrArray) instrs = NULL;
                  g_autoptr (GPtrArray) argss  = NULL;
                  g_autoptr (GArray) n_argss   = NULL;

                  instrs  = g_ptr_array_new_with_free_func (g_free);
                  argss   = g_ptr_array_new_with_free_func ((GDestroyNotify) g_strfreev);
                  n_argss = g_array_new (FALSE, FALSE, sizeof (guint));

                  if (!expect_closing_paren)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "type %s must be wrapped in #(...)",
                          g_type_name (type));
                      return NULL;
                    }

                  for (;;)
                    {
                      g_autofree char *instr   = NULL;
                      g_auto (GStrv) call_args = NULL;
                      guint n_call_args        = 0;

                      GET_TOKEN (&instr, TOKEN_PARSE_DEFAULT);
                      if (g_strcmp0 (instr, ")") == 0)
                        break;

                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, "(");
                      p = parse_args (p, spec, state, enclosing_object,
                                      macro_replacements, n_anon_vals,
                                      type_hints, NULL, NULL, 0, &call_args, NULL,
                                      &n_call_args, ARGS_PARSE_PARENS, &local_error);
                      RETURN_ERROR_UNLESS (p != NULL);
                      GET_TOKEN_EXPECT (&token, TOKEN_PARSE_DEFAULT, ";");

                      g_ptr_array_add (instrs, g_steal_pointer (&instr));
                      g_ptr_array_add (argss, g_steal_pointer (&call_args));
                      g_array_append_val (n_argss, n_call_args);
                    }

                  if (instrs->len == 0)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "not enough path builder instructions");
                      return NULL;
                    }

                  key    = make_anon_name ((*n_anon_vals)++);
                  result = bge_wdgt_spec_add_path_source_value (
                      spec, key,
                      (const char *const *) instrs->pdata,
                      (const char *const *const *) argss->pdata,
                      (const guint *) (void *) n_argss->data,
                      instrs->len,
                      &local_error);
                  RETURN_ERROR_UNLESS (result);
                }
              else if (type == GSK_TYPE_STROKE)
                {
                  g_auto (GStrv) component_args = NULL;
                  guint n_component_args        = 0;

                  GType types[] = {
                    G_TYPE_DOUBLE, /* line width */
                    GSK_TYPE_LINE_CAP,
                    GSK_TYPE_LINE_JOIN,
                    G_TYPE_DOUBLE, /* miter limit */
                  };

                  if (!expect_closing_paren)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "type %s must be wrapped in #(...)",
                          g_type_name (type));
                      return NULL;
                    }

                  p = parse_args (p, spec, state, enclosing_object,
                                  macro_replacements,
                                  n_anon_vals, type_hints, NULL,
                                  types, G_N_ELEMENTS (types),
                                  &component_args, NULL, &n_component_args, ARGS_PARSE_PARENS,
                                  &local_error);
                  RETURN_ERROR_UNLESS (p != NULL);

                  if (n_component_args != G_N_ELEMENTS (types))
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "%s type initializer can have %d arguments, got %u",
                          g_type_name (GSK_TYPE_STROKE),
                          (int) G_N_ELEMENTS (types),
                          n_component_args);
                      return NULL;
                    }

                  key    = make_anon_name ((*n_anon_vals)++);
                  result = bge_wdgt_spec_add_component_source_value (
                      spec, key, GSK_TYPE_STROKE,
                      (const char *const *) component_args,
                      n_component_args, &local_error);
                  RETURN_ERROR_UNLESS (result);
                }
              else
                {
                  g_set_error (
                      error,
                      G_IO_ERROR,
                      G_IO_ERROR_UNKNOWN,
                      "Can't parse type %s",
                      g_type_name (type));
                  return NULL;
                }

              if (constant)
                {
                  key    = make_anon_name ((*n_anon_vals)++);
                  result = bge_wdgt_spec_add_constant_source_value (
                      spec, key, &value, &local_error);
                  g_value_unset (&value);
                  RETURN_ERROR_UNLESS (result);
                }
            }

          for (;;)
            {
              GET_TOKEN (&token, TOKEN_PARSE_DEFAULT);
              if (g_strcmp0 (token, ":") == 0)
                {
                  g_autofree char *property = NULL;
                  g_autofree char *name     = NULL;

                  GET_TOKEN (&property, TOKEN_PARSE_DEFAULT);
                  name = make_object_property_name (key, property, (*n_anon_vals)++);

                  result = bge_wdgt_spec_add_property_value (
                      spec, name, key, property, &type, &local_error);
                  RETURN_ERROR_UNLESS (result);

                  g_clear_pointer (&key, g_free);
                  key = g_steal_pointer (&name);
                }
              else
                break;
            }
          get_token = FALSE;

          g_strv_builder_add (builder, key);
          g_array_append_val (types_array, type);
          n_args++;
          need_comma = TRUE;
        }
    }

  if (n_out != NULL)
    *n_out = n_args;
  if (values_out != NULL)
    *values_out = g_strv_builder_end (builder);
  if (types_out != NULL)
    *types_out = g_array_steal (types_array, NULL);

  return p;
}

#undef GET_TOKEN_EXPECT
#undef GET_TOKEN
#undef UNEXPECTED_TOKEN
#undef EXPECT_TOKEN
#undef RETURN_ERROR_UNLESS

static char *
parse_token_fundamental (const char  *token,
                         BgeWdgtSpec *spec,
                         guint       *n_anon_vals,
                         GError     **error)
{
  g_autoptr (GError) local_error = NULL;
  gboolean         result        = FALSE;
  gunichar         ch            = 0;
  GValue           value         = { 0 };
  g_autofree char *key           = NULL;

  ch = g_utf8_get_char (token);
  if (ch == '-' ||
      g_unichar_isdigit (ch))
    {
      gboolean is_double           = FALSE;
      g_autoptr (GVariant) variant = NULL;

      is_double = g_utf8_strchr (token, -1, '.') != NULL;
      if (is_double)
        variant = g_variant_parse (G_VARIANT_TYPE_DOUBLE, token,
                                   NULL, NULL, &local_error);
      else
        variant = g_variant_parse (G_VARIANT_TYPE_INT32, token,
                                   NULL, NULL, &local_error);

      if (variant == NULL)
        {
          g_set_error (
              error,
              G_IO_ERROR,
              G_IO_ERROR_UNKNOWN,
              "Unable to parse number value '%s': %s",
              token, local_error->message);
          return NULL;
        }

      if (is_double)
        g_value_set_double (g_value_init (&value, G_TYPE_DOUBLE),
                            g_variant_get_double (variant));
      else
        g_value_set_int (g_value_init (&value, G_TYPE_INT),
                         g_variant_get_int32 (variant));
    }
  else if (g_strcmp0 (token, "true") == 0)
    g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN),
                         TRUE);
  else if (g_strcmp0 (token, "false") == 0)
    g_value_set_boolean (g_value_init (&value, G_TYPE_BOOLEAN),
                         FALSE);
  else
    return g_strdup (token);

  if (key == NULL)
    key = make_anon_name ((*n_anon_vals)++);
  result = bge_wdgt_spec_add_constant_source_value (
      spec, key, &value, error);
  g_value_unset (&value);
  if (!result)
    return NULL;

  return g_steal_pointer (&key);
}

static char *
consume_token (const char    **pp,
               const char     *single_chars,
               TokenParseFlags flags,
               gboolean       *was_quoted,
               GHashTable     *macro_replacements,
               GError        **error)
{
  g_autofree char *token = NULL;

  token = consume_token_inner (pp, single_chars, flags, was_quoted, error);
  if (token == NULL)
    return NULL;

  if (macro_replacements != NULL)
    {
      const char *replacement = NULL;

      replacement = g_hash_table_lookup (macro_replacements, token);
      if (replacement != NULL)
        return g_strdup (replacement);
      else
        {
          const char *replace_start = NULL;

          replace_start = g_utf8_strchr (token, -1, '@');
          if (replace_start != NULL)
            {
              const char *last_end       = token;
              g_autoptr (GString) string = NULL;

              string = g_string_new (NULL);
              for (;;)
                {
                  g_autofree char *replace = NULL;
                  const char      *with    = NULL;

                  if (replace_start - last_end > 0)
                    g_string_append_len (string, last_end, replace_start - last_end);
                  replace_start++;

                  last_end = g_utf8_strchr (replace_start, -1, '@');
                  if (last_end == NULL)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "error in macro expansion: "
                          "unterminated replacement");
                      return NULL;
                    }

                  replace = g_strndup (replace_start, last_end - replace_start);
                  with    = g_hash_table_lookup (macro_replacements, replace);
                  if (with == NULL)
                    {
                      g_set_error (
                          error,
                          G_IO_ERROR,
                          G_IO_ERROR_UNKNOWN,
                          "error in macro expansion: "
                          "undefined string \"%s\"",
                          replace);
                      return NULL;
                    }

                  g_string_append (string, with);

                  last_end++;
                  replace_start = g_utf8_strchr (last_end, '@', -1);
                  if (replace_start == NULL)
                    break;
                }

              if (*last_end != '\0')
                g_string_append (string, last_end);

              return g_string_free_and_steal (g_steal_pointer (&string));
            }
        }
    }

  return g_steal_pointer (&token);
}

static char *
consume_token_inner (const char    **pp,
                     const char     *single_chars,
                     TokenParseFlags flags,
                     gboolean       *was_quoted,
                     GError        **error)
{
  const char *p             = *pp;
  gboolean    hit_non_space = FALSE;

  if (was_quoted != NULL)
    *was_quoted = FALSE;

#define UNEXPECTED_EOF      \
  G_STMT_START              \
  {                         \
    g_set_error (           \
        error,              \
        G_IO_ERROR,         \
        G_IO_ERROR_UNKNOWN, \
        "Unexpected EOF");  \
    return NULL;            \
  }                         \
  G_STMT_END

#define RETURN_TOKEN                 \
  G_STMT_START                       \
  {                                  \
    g_autofree char *_ret = NULL;    \
                                     \
    _ret = g_strndup (*pp, p - *pp); \
    *pp  = p;                        \
    return g_steal_pointer (&_ret);  \
  }                                  \
  G_STMT_END

#define RETURN_TOKEN_ADJUST_NEXT_CHAR \
  G_STMT_START                        \
  {                                   \
    g_autofree char *_ret = NULL;     \
                                      \
    _ret = g_strndup (*pp, p - *pp);  \
    *pp  = g_utf8_next_char (p);      \
    return g_steal_pointer (&_ret);   \
  }                                   \
  G_STMT_END

  if (IS_EOF (p))
    UNEXPECTED_EOF;

  for (; !IS_EOF (p); p = g_utf8_next_char (p))
    {
      gunichar ch            = 0;
      gboolean is_whitespace = FALSE;
      gboolean is_quotes     = FALSE;

      ch            = g_utf8_get_char (p);
      is_whitespace = ch == '\n' || g_unichar_isspace (ch);
      is_quotes     = ch == '"';

      if (is_whitespace)
        {
          if (!(flags & TOKEN_PARSE_QUOTED) &&
              hit_non_space)
            RETURN_TOKEN;
        }
      else
        {
          if (is_quotes)
            {
              if (hit_non_space)
                {
                  if (was_quoted != NULL)
                    *was_quoted = TRUE;

                  if (flags & TOKEN_PARSE_QUOTED)
                    RETURN_TOKEN_ADJUST_NEXT_CHAR;
                  else
                    RETURN_TOKEN;
                }
              else
                {
                  flags |= TOKEN_PARSE_QUOTED;
                  *pp = g_utf8_next_char (p);
                  if (IS_EOF (*pp))
                    UNEXPECTED_EOF;
                  hit_non_space = TRUE;
                }
            }
          else if (flags & TOKEN_PARSE_QUOTED)
            {
              if (!hit_non_space)
                {
                  g_set_error (
                      error,
                      G_IO_ERROR,
                      G_IO_ERROR_UNKNOWN,
                      "Expected quote");
                  return NULL;
                }
            }
          else if (single_chars != NULL &&
                   g_utf8_strchr (single_chars, -1, ch) != NULL)
            {
              if (hit_non_space)
                RETURN_TOKEN;
              else
                {
                  char buf[16] = { 0 };

                  g_unichar_to_utf8 (ch, buf);
                  *pp = g_utf8_next_char (p);
                  return g_strdup (buf);
                }
            }

          if (!hit_non_space)
            {
              *pp           = p;
              hit_non_space = TRUE;
            }
        }
    }

  if (!(flags & TOKEN_PARSE_QUOTED) && hit_non_space)
    RETURN_TOKEN;

  UNEXPECTED_EOF;
#undef RETURN_TOKEN_ADJUST_NEXT_CHAR
#undef RETURN_TOKEN
#undef UNEXPECTED_EOF
}

static gdouble
eval_closure (gpointer         this,
              guint            n_param_values,
              const GValue    *param_values,
              EvalClosureData *data)
{
  GArray  *ops      = data->ops;
  gdouble *workbuf0 = data->workbuf0;
  gdouble *workbuf1 = data->workbuf1;
  gdouble  result   = 0.0;

  for (guint i = 0; i < n_param_values; i++)
    {
      workbuf0[i] = g_value_get_double (&param_values[i]);
      workbuf1[i] = 1.0;
    }

  for (guint i = 0; i < ops->len; i++)
    {
      EvalOperator *op        = NULL;
      guint         left_idx  = 0;
      guint         right_idx = 0;
      gdouble       left      = 0.0;
      gdouble       right     = 0.0;

      op = &g_array_index (ops, EvalOperator, i);

      left_idx = op->pos;
      while (workbuf1[left_idx] < 0.0)
        {
          left_idx--;
        }

      right_idx = op->pos + 1;
      while (workbuf1[right_idx] < 0.0)
        {
          right_idx++;
        }

      left  = workbuf0[left_idx];
      right = workbuf0[right_idx];

      switch (op->op)
        {
        case OPERATOR_ADD:
          result = left + right;
          break;
        case OPERATOR_SUBTRACT:
          result = left - right;
          break;
        case OPERATOR_MULTIPLY:
          result = left * right;
          break;
        case OPERATOR_DIVIDE:
          result = left / right;
          break;
        case OPERATOR_MODULUS:
          result = fmod (left, right);
          break;
        case OPERATOR_POWER:
          result = pow (left, right);
          break;
        default:
          g_assert_not_reached ();
        }

      workbuf0[left_idx]  = result;
      workbuf1[left_idx]  = 1.0;
      workbuf1[right_idx] = -1.0;
    }

  return result;
}

static char *
make_object_property_name (const char *object,
                           const char *property,
                           guint       n)
{
  return g_strdup_printf ("prop@%u(%s).%s", n, object, property);
}

static char *
make_widget_allocation_name (const char *widget,
                             guint       n)
{
  return g_strdup_printf ("allocation@%u(%s)", n, widget);
}

static char *
make_widget_measurement_name (guint n)
{
  return g_strdup_printf ("measurement@%u", n);
}

static char *
make_anon_name (guint n)
{
  return g_strdup_printf ("anon@%u", n);
}

static gint
cmp_operator (EvalOperator *a,
              EvalOperator *b)
{
  int a_prec = 0;
  int b_prec = 0;

  a_prec = operator_precedence[a->op];
  b_prec = operator_precedence[b->op];

  return a_prec > b_prec ? -1 : 1;
}

static void
_marshal_DIRECT__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data)
{
  typedef void (*GMarshalFunc_DIRECT__ARGS_DIRECT) (gpointer      data1,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      data2);
  GCClosure                       *cc = (GCClosure *) closure;
  gpointer                         data1, data2;
  GMarshalFunc_DIRECT__ARGS_DIRECT callback;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values >= 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_DIRECT__ARGS_DIRECT) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            return_value,
            n_param_values - 1,
            param_values + 1,
            data2);
}

static void
_marshal_BOOL__ARGS_DIRECT (GClosure                *closure,
                            GValue                  *return_value,
                            guint                    n_param_values,
                            const GValue            *param_values,
                            gpointer invocation_hint G_GNUC_UNUSED,
                            gpointer                 marshal_data)
{
  typedef gboolean (*GMarshalFunc_BOOL__ARGS_DIRECT) (gpointer      data1,
                                                      guint         n_param_values,
                                                      const GValue *param_values,
                                                      gpointer      data2);
  GCClosure                     *cc = (GCClosure *) closure;
  gpointer                       data1, data2;
  GMarshalFunc_BOOL__ARGS_DIRECT callback;
  gboolean                       v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values >= 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_BOOL__ARGS_DIRECT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
                       n_param_values - 1,
                       param_values + 1,
                       data2);

  g_value_set_boolean (return_value, v_return);
}

static void
_marshal_DOUBLE__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data)
{
  typedef gdouble (*GMarshalFunc_DOUBLE__ARGS_DIRECT) (gpointer      data1,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      data2);
  GCClosure                       *cc = (GCClosure *) closure;
  gpointer                         data1, data2;
  GMarshalFunc_DOUBLE__ARGS_DIRECT callback;
  gdouble                          v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values >= 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_DOUBLE__ARGS_DIRECT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
                       n_param_values - 1,
                       param_values + 1,
                       data2);

  g_value_set_double (return_value, v_return);
}
