/* bz-search-engine.c
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

#define G_LOG_DOMAIN "BAZAAR::SEARCH-ENGINE"

#include "bz-search-engine.h"
#include "bz-entry-group.h"
#include "env.h"
#include "bz-finished-search-query.h"
#include "bz-search-result.h"
#include "util.h"

struct _BzSearchEngine
{
  GObject parent_instance;

  GListModel *model;
  GListModel *biases;

  GPtrArray *biases_mirror;
};

G_DEFINE_FINAL_TYPE (BzSearchEngine, bz_search_engine, G_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_MODEL,
  PROP_BIASES,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
biases_changed (BzSearchEngine *self,
                guint           position,
                guint           removed,
                guint           added,
                GListModel     *model);

static double
test_strings (const char *query,
              const char *against,
              gssize      accept_min_size);

typedef struct
{
  guint  idx;
  double val;
} Score;

static gint
cmp_scores (Score *a,
            Score *b);

enum
{
  LINEAR,
  EXPONENTIAL,
};

BZ_DEFINE_DATA (
    bias,
    Bias,
    {
      gboolean    invalid;
      GRegex     *regex;
      char       *convert_to;
      GHashTable *boost;
      int         boost_kind;
      union
      {
        struct
        {
          double slope;
          double y_intercept;
        } linear_boost;
        struct
        {
          double factor;
          double y_intercept;
        } exponential_boost;
      };
    },
    BZ_RELEASE_DATA (regex, g_regex_unref);
    BZ_RELEASE_DATA (convert_to, g_free);
    BZ_RELEASE_DATA (boost, g_hash_table_unref));

BZ_DEFINE_DATA (
    query_task,
    QueryTask,
    {
      char     **terms;
      GPtrArray *snapshot;
      GPtrArray *biases;
    },
    BZ_RELEASE_DATA (terms, g_strfreev);
    BZ_RELEASE_DATA (snapshot, g_ptr_array_unref);
    BZ_RELEASE_DATA (biases, g_ptr_array_unref))
static DexFuture *
query_task_fiber (QueryTaskData *data);

BZ_DEFINE_DATA (
    query_sub_task,
    QuerySubTask,
    {
      char      *query_utf8;
      GPtrArray *shallow_mirror;
      double     threshold;
      guint      work_offset;
      guint      work_length;
      GPtrArray *active_biases;
    },
    BZ_RELEASE_DATA (query_utf8, g_free);
    BZ_RELEASE_DATA (shallow_mirror, g_ptr_array_unref);
    BZ_RELEASE_DATA (active_biases, g_ptr_array_unref));
static DexFuture *
query_sub_task_fiber (QuerySubTaskData *data);

static inline GUnicodeType
utf8_char_class (const char *s,
                 gunichar   *ch_out);

static inline const char *
utf8_skip_to_next_of_class (const char **s,
                            GUnicodeType class,
                            gsize       *read_utf8);

static void
bz_search_engine_dispose (GObject *object)
{
  BzSearchEngine *self = BZ_SEARCH_ENGINE (object);

  if (self->biases != NULL)
    g_signal_handlers_disconnect_by_func (self->biases, biases_changed, self);

  g_clear_object (&self->model);
  g_clear_object (&self->biases);

  g_clear_pointer (&self->biases_mirror, g_ptr_array_unref);

  G_OBJECT_CLASS (bz_search_engine_parent_class)->dispose (object);
}

static void
bz_search_engine_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BzSearchEngine *self = BZ_SEARCH_ENGINE (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, bz_search_engine_get_model (self));
      break;
    case PROP_BIASES:
      g_value_set_object (value, bz_search_engine_get_biases (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_engine_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BzSearchEngine *self = BZ_SEARCH_ENGINE (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      bz_search_engine_set_model (self, g_value_get_object (value));
      break;
    case PROP_BIASES:
      bz_search_engine_set_biases (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_engine_class_init (BzSearchEngineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_search_engine_set_property;
  object_class->get_property = bz_search_engine_get_property;
  object_class->dispose      = bz_search_engine_dispose;

  props[PROP_MODEL] =
      g_param_spec_object (
          "model",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_BIASES] =
      g_param_spec_object (
          "biases",
          NULL, NULL,
          G_TYPE_LIST_MODEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_search_engine_init (BzSearchEngine *self)
{
  self->biases_mirror = g_ptr_array_new_with_free_func (bias_data_unref);
}

BzSearchEngine *
bz_search_engine_new (void)
{
  return g_object_new (BZ_TYPE_SEARCH_ENGINE, NULL);
}

GListModel *
bz_search_engine_get_biases (BzSearchEngine *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_ENGINE (self), NULL);
  return self->biases;
}

void
bz_search_engine_set_biases (BzSearchEngine *self,
                             GListModel     *biases)
{
  g_return_if_fail (BZ_IS_SEARCH_ENGINE (self));
  g_return_if_fail (biases == NULL || G_IS_LIST_MODEL (biases));

  if (self->biases != NULL)
    g_signal_handlers_disconnect_by_func (self->biases, biases_changed, self);
  g_clear_object (&self->biases);
  g_ptr_array_set_size (self->biases_mirror, 0);

  if (biases != NULL)
    {
      guint n_biases = 0;

      self->biases = g_object_ref (biases);

      n_biases = g_list_model_get_n_items (biases);
      biases_changed (self, 0, 0, n_biases, biases);

      g_signal_connect_swapped (
          biases, "items-changed",
          G_CALLBACK (biases_changed), self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BIASES]);
}

GListModel *
bz_search_engine_get_model (BzSearchEngine *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_ENGINE (self), NULL);
  return self->model;
}

void
bz_search_engine_set_model (BzSearchEngine *self,
                            GListModel     *model)
{
  g_return_if_fail (BZ_IS_SEARCH_ENGINE (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  g_clear_object (&self->model);
  if (model != NULL)
    self->model = g_object_ref (model);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODEL]);
}

DexFuture *
bz_search_engine_query (BzSearchEngine    *self,
                        const char *const *terms)
{
  guint n_groups = 0;

  dex_return_error_if_fail (BZ_IS_SEARCH_ENGINE (self));
  dex_return_error_if_fail (terms != NULL && *terms != NULL);

  if (self->model != NULL)
    n_groups = g_list_model_get_n_items (self->model);

  if (self->model == NULL ||
      n_groups == 0 ||
      **terms == '\0')
    {
      g_autoptr (GPtrArray) results              = NULL;
      g_autoptr (BzFinishedSearchQuery) finished = NULL;

      results = g_ptr_array_new_with_free_func (g_object_unref);
      g_ptr_array_set_size (results, n_groups);

      for (guint i = 0; i < results->len; i++)
        {
          g_autoptr (BzEntryGroup) group    = NULL;
          g_autoptr (BzSearchResult) result = NULL;

          group = g_list_model_get_item (self->model, i);

          result = bz_search_result_new ();
          bz_search_result_set_group (result, group);
          bz_search_result_set_original_index (result, i);
          g_ptr_array_index (results, i) = g_steal_pointer (&result);
        }

      finished = bz_finished_search_query_new ();
      bz_finished_search_query_set_interpreted_query (finished, "");
      bz_finished_search_query_set_results (finished, results);
      bz_finished_search_query_set_n_results (finished, n_groups);
      bz_finished_search_query_set_elapsed (finished, 0.0);

      return dex_future_new_for_object (finished);
    }
  else
    {
      g_autoptr (GPtrArray) snapshot = NULL;
      g_autoptr (QueryTaskData) data = NULL;

      snapshot = g_ptr_array_new_with_free_func (g_object_unref);
      g_ptr_array_set_size (snapshot, n_groups);

      for (guint i = 0; i < snapshot->len; i++)
        g_ptr_array_index (snapshot, i) = g_list_model_get_item (self->model, i);

      data           = query_task_data_new ();
      data->terms    = g_strdupv ((gchar **) terms);
      data->snapshot = g_steal_pointer (&snapshot);
      data->biases   = g_ptr_array_ref (self->biases_mirror);

      return dex_scheduler_spawn (
          dex_thread_pool_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) query_task_fiber,
          query_task_data_ref (data), query_task_data_unref);
    }
}

static void
biases_changed (BzSearchEngine *self,
                guint           position,
                guint           removed,
                guint           added,
                GListModel     *model)
{
  g_autoptr (GPtrArray) new_mirror = NULL;

  /* Since `self->biases_mirror` is used in other threads, it should be
     immutable after its construction; so we have to make a new mirror upon
     every change */
  new_mirror = g_ptr_array_new_with_free_func (bias_data_unref);
  g_ptr_array_set_size (new_mirror, self->biases_mirror->len + added - removed);

#define COPY_OVER(_old_idx, _new_idx, _len)                                   \
  for (guint _i = 0; _i < (_len); _i++)                                       \
    {                                                                         \
      BiasData  *_data = NULL;                                                \
      BiasData **_loc  = NULL;                                                \
                                                                              \
      _data = g_ptr_array_index (self->biases_mirror, (_old_idx) + _i);       \
      _loc  = (BiasData **) &g_ptr_array_index (new_mirror, (_new_idx) + _i); \
      *_loc = bias_data_ref (_data);                                          \
    }

  COPY_OVER (0, 0, position);
  COPY_OVER (position + removed,
             position + added,
             self->biases_mirror->len - (position + removed));

#undef COPY_OVER

  for (guint i = 0; i < added; i++)
    {
      g_autoptr (GError) local_error              = NULL;
      BiasData **loc                              = NULL;
      g_autoptr (BzSearchBias) bias               = NULL;
      const char            *regex_string         = NULL;
      const char            *convert_to           = NULL;
      GListModel            *boost_appids         = NULL;
      BzLinearFunction      *linear_function      = NULL;
      BzExponentialFunction *exponential_function = NULL;
      g_autoptr (GRegex) regex                    = NULL;
      g_autoptr (GHashTable) boost                = NULL;
      g_autoptr (BiasData) data                   = NULL;

      loc = (BiasData **) &g_ptr_array_index (new_mirror, position + i);

      bias                 = g_list_model_get_item (model, position + i);
      regex_string         = bz_search_bias_get_regex (bias);
      convert_to           = bz_search_bias_get_convert_to (bias);
      boost_appids         = bz_search_bias_get_boost_appids (bias);
      linear_function      = bz_search_bias_get_linear_boost (bias);
      exponential_function = bz_search_bias_get_exponential_boost (bias);

#define SKIP()                              \
  G_STMT_START                              \
  {                                         \
    g_autoptr (BiasData) _data = NULL;      \
                                            \
    _data          = bias_data_new ();      \
    _data->invalid = TRUE;                  \
    *loc           = bias_data_ref (_data); \
  }                                         \
  G_STMT_END

      if (regex_string == NULL ||
          (convert_to == NULL &&
           (boost_appids == NULL ||
            (linear_function == NULL &&
             exponential_function == NULL))))
        {
          g_critical ("Bias is incomplete! Skipping...");
          SKIP ();
          continue;
        }
      if (linear_function != NULL &&
          exponential_function != NULL)
        {
          g_critical ("Search bias can only have one boost function! Skipping...");
          SKIP ();
          continue;
        }

      regex = g_regex_new (
          regex_string,
          G_REGEX_OPTIMIZE,
          G_REGEX_MATCH_DEFAULT,
          &local_error);
      if (regex == NULL)
        {
          g_critical ("Bias regex \"%s\" is invalid: %s",
                      regex_string, local_error->message);
          SKIP ();
          continue;
        }

#undef SKIP

      if (boost_appids != NULL)
        {
          guint n_appids = 0;

          n_appids = g_list_model_get_n_items (boost_appids);
          if (n_appids > 0)
            {
              boost = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

              for (guint j = 0; j < n_appids; j++)
                {
                  g_autoptr (GtkStringObject) string = NULL;
                  const char *appid                  = NULL;

                  string = g_list_model_get_item (boost_appids, j);
                  appid  = gtk_string_object_get_string (string);
                  g_hash_table_replace (boost, g_strdup (appid), NULL);
                }
            }
        }

      data             = bias_data_new ();
      data->regex      = g_regex_ref (regex);
      data->convert_to = bz_maybe_strdup (convert_to);
      data->boost      = bz_maybe_ref (boost, g_hash_table_ref);

      if (linear_function != NULL)
        {
          data->boost_kind               = LINEAR;
          data->linear_boost.slope       = bz_linear_function_get_slope (linear_function);
          data->linear_boost.y_intercept = bz_linear_function_get_y_intercept (linear_function);
        }
      if (exponential_function != NULL)
        {
          data->boost_kind                    = EXPONENTIAL;
          data->exponential_boost.factor      = bz_exponential_function_get_factor (exponential_function);
          data->exponential_boost.y_intercept = bz_exponential_function_get_y_intercept (exponential_function);
        }

      *loc = bias_data_ref (data);
    }

  g_clear_pointer (&self->biases_mirror, g_ptr_array_unref);
  self->biases_mirror = g_steal_pointer (&new_mirror);
}

static DexFuture *
query_task_fiber (QueryTaskData *data)
{
  char     **terms                           = data->terms;
  GPtrArray *shallow_mirror                  = data->snapshot;
  GPtrArray *biases                          = data->biases;
  g_autoptr (GError) local_error             = NULL;
  gboolean result                            = FALSE;
  g_autoptr (GTimer) timer                   = NULL;
  g_autofree char *query_utf8                = NULL;
  guint            n_sub_tasks               = 0;
  guint            scores_per_task           = 0;
  g_autoptr (GPtrArray) active_biases        = NULL;
  g_autoptr (GPtrArray) sub_futures          = NULL;
  g_autoptr (GArray) scores                  = NULL;
  g_autoptr (GPtrArray) results              = NULL;
  g_autoptr (BzFinishedSearchQuery) finished = NULL;

  timer = g_timer_new ();

  query_utf8      = g_strjoinv (" ", terms);
  n_sub_tasks     = MAX (1, MIN (shallow_mirror->len / 512, g_get_num_processors ()));
  scores_per_task = shallow_mirror->len / n_sub_tasks;

  active_biases = g_ptr_array_new_with_free_func (bias_data_unref);
  for (guint i = 0; i < biases->len; i++)
    {
      BiasData *bias = NULL;

      bias = g_ptr_array_index (biases, i);
      if (bias->invalid)
        continue;

      if (!g_regex_match (bias->regex, query_utf8, G_REGEX_MATCH_DEFAULT, NULL))
        continue;

      if (bias->convert_to != NULL)
        {
          g_autofree char *tmp = NULL;

          tmp = g_regex_replace (
              bias->regex, query_utf8,
              -1, 0, bias->convert_to,
              G_REGEX_MATCH_DEFAULT, NULL);
          if (tmp != NULL)
            {
              g_clear_pointer (&query_utf8, g_free);
              query_utf8 = g_steal_pointer (&tmp);
            }
        }

      g_ptr_array_add (active_biases, bias_data_ref (bias));
    }

  sub_futures = g_ptr_array_new_with_free_func (dex_unref);
  for (guint i = 0; i < n_sub_tasks; i++)
    {
      g_autoptr (QuerySubTaskData) sub_data = NULL;
      g_autoptr (DexFuture) future          = NULL;

      sub_data                 = query_sub_task_data_new ();
      sub_data->query_utf8     = g_strdup (query_utf8);
      sub_data->shallow_mirror = g_ptr_array_ref (shallow_mirror);
      sub_data->threshold      = 1.0;
      sub_data->work_offset    = i * scores_per_task;
      sub_data->work_length    = scores_per_task;
      sub_data->active_biases  = g_ptr_array_ref (active_biases);

      if (i >= n_sub_tasks - 1)
        sub_data->work_length += shallow_mirror->len % n_sub_tasks;

      future = dex_scheduler_spawn (
          dex_thread_pool_scheduler_get_default (),
          bz_get_dex_stack_size (),
          (DexFiberFunc) query_sub_task_fiber,
          query_sub_task_data_ref (sub_data),
          query_sub_task_data_unref);

      g_ptr_array_add (sub_futures, g_steal_pointer (&future));
    }

  result = dex_await (dex_future_allv (
                          (DexFuture *const *) sub_futures->pdata, sub_futures->len),
                      &local_error);
  if (!result)
    return dex_future_new_for_error (g_steal_pointer (&local_error));

  scores = g_array_new (FALSE, FALSE, sizeof (Score));
  for (guint i = 0; i < sub_futures->len; i++)
    {
      DexFuture *future     = NULL;
      GArray    *scores_out = NULL;

      future     = g_ptr_array_index (sub_futures, i);
      scores_out = g_value_get_boxed (dex_future_get_value (future, NULL));

      if (scores_out->len > 0)
        g_array_append_vals (scores, scores_out->data, scores_out->len);
    }
  if (scores->len > 0)
    g_array_sort (scores, (GCompareFunc) cmp_scores);

  results = g_ptr_array_new_with_free_func (g_object_unref);
  g_ptr_array_set_size (results, scores->len);
  for (guint i = 0; i < scores->len; i++)
    {
      Score        *score                      = NULL;
      BzEntryGroup *group                      = NULL;
      g_autoptr (BzSearchResult) search_result = NULL;

      score = &g_array_index (scores, Score, i);
      group = g_ptr_array_index (shallow_mirror, score->idx);

      search_result = bz_search_result_new ();
      bz_search_result_set_group (search_result, group);
      bz_search_result_set_original_index (search_result, score->idx);
      bz_search_result_set_score (search_result, score->val);

      g_ptr_array_index (results, i) = g_steal_pointer (&search_result);
    }

  finished = bz_finished_search_query_new ();
  bz_finished_search_query_set_interpreted_query (finished, query_utf8);
  bz_finished_search_query_set_results (finished, results);
  bz_finished_search_query_set_n_results (finished, results->len);
  bz_finished_search_query_set_elapsed (finished, g_timer_elapsed (timer, NULL));

  return dex_future_new_for_object (finished);
}

static DexFuture *
query_sub_task_fiber (QuerySubTaskData *data)
{
  GPtrArray *shallow_mirror     = data->shallow_mirror;
  char      *query_utf8         = data->query_utf8;
  double     threshold          = data->threshold;
  guint      work_offset        = data->work_offset;
  guint      work_length        = data->work_length;
  GPtrArray *active_biases      = data->active_biases;
  g_autoptr (GArray) scores_out = NULL;

  scores_out = g_array_new (FALSE, FALSE, sizeof (Score));

  for (guint i = 0; i < work_length; i++)
    {
      g_autoptr (GMutexLocker) locker = NULL;
      BzEntryGroup *group             = NULL;
      const char   *id                = NULL;
      const char   *title             = NULL;
      double        score             = 0.0;

      group  = g_ptr_array_index (shallow_mirror, work_offset + i);
      locker = bz_entry_group_lock (group);

      if (!bz_entry_group_is_searchable (group))
        continue;

      id    = bz_entry_group_get_id (group);
      title = bz_entry_group_get_title (group);
      if ((id != NULL && g_strcmp0 (query_utf8, id) == 0) ||
          (title != NULL && strcasecmp (query_utf8, title) == 0))
        score = (double) G_MAXINT;
      else
        {
          const char *developer     = NULL;
          const char *description   = NULL;
          const char *search_tokens = NULL;

          developer     = bz_entry_group_get_developer (group);
          description   = bz_entry_group_get_description (group);
          search_tokens = bz_entry_group_get_search_tokens (group);

#define EVALUATE_STRING(_s, _accept_min_size)                  \
  ((_s) != NULL                                                \
       ? (test_strings (query_utf8, (_s), (_accept_min_size))) \
       : 0.0)

          score += EVALUATE_STRING (title, 2) * 2.0;
          score += EVALUATE_STRING (developer, 2) * 1.0;
          score += EVALUATE_STRING (description, 3) * 1.0;
          score += EVALUATE_STRING (search_tokens, -1) * 1.5;

#undef EVALUATE_STRING
        }

      for (guint j = 0; j < active_biases->len; j++)
        {
          BiasData *bias = NULL;

          bias = g_ptr_array_index (active_biases, j);
          if (bias->boost == NULL)
            continue;

          if (!g_hash_table_contains (bias->boost, id))
            continue;

          switch (bias->boost_kind)
            {
            case LINEAR:
              score = bias->linear_boost.slope * score + bias->linear_boost.y_intercept;
              break;
            case EXPONENTIAL:
              score = pow (bias->exponential_boost.factor, score) * bias->exponential_boost.y_intercept;
              break;
            default:
              break;
            }
        }

      if (score > threshold)
        {
          Score append = { 0 };

          append.idx = work_offset + i;
          append.val = score;
          g_array_append_val (scores_out, append);
        }
    }

  return dex_future_new_take_boxed (G_TYPE_ARRAY, g_steal_pointer (&scores_out));
}

#define UTF8_FOREACH_FORWARD(_var, _s) \
  for (const char *_var = (_s);        \
       _var != NULL && *_var != '\0';  \
       _var = g_utf8_next_char (_var))

#define UTF8_FOREACH_FORWARD_WITH_END(_var, _s, _end)  \
  for (const char *_var = (_s);                        \
       _var != NULL && *_var != '\0' && _var < (_end); \
       _var = g_utf8_next_char (_var))

#define UTF8_FOREACH_BACKWARD(_var, _s, _start) \
  for (const char *_var = (_s);                 \
       _var != NULL && _var >= (_start);        \
       _var = g_utf8_prev_char (_var))

#define UTF8_FOREACH_TOKEN_FORWARDS(_start_var, _end_var, _s, _token_len)                                                            \
  for (const char *_start_var = (_s), *_end_var = utf8_skip_to_next_of_class (&_start_var, G_UNICODE_SPACE_SEPARATOR, (_token_len)); \
       _start_var != NULL && *_start_var != '\0';                                                                                    \
       _start_var = _end_var, _end_var = utf8_skip_to_next_of_class (&_start_var, G_UNICODE_SPACE_SEPARATOR, (_token_len)))

static double
test_strings (const char *query,
              const char *against,
              gssize      accept_min_size)
{
  double score                = 0.0;
  gsize  query_tok_utf8_len   = 0;
  gsize  against_tok_utf8_len = 0;

  UTF8_FOREACH_TOKEN_FORWARDS (query_tok_start, query_tok_end, query, &query_tok_utf8_len)
  {
    gboolean query_token_has_match = FALSE;

    UTF8_FOREACH_TOKEN_FORWARDS (against_tok_start, against_tok_end, against, &against_tok_utf8_len)
    {
      gboolean match    = FALSE;
      gsize    consumed = 0;

      if (accept_min_size > 0 &&
          against_tok_utf8_len < accept_min_size)
        continue;

      UTF8_FOREACH_FORWARD_WITH_END (against_ptr, against_tok_start, against_tok_end)
      {
        const char *against_check_ptr = NULL;
        gunichar    against_ch        = 0;

        if (query_tok_utf8_len > against_tok_utf8_len - consumed)
          break;

        match = TRUE;

        against_check_ptr = against_ptr;
        against_ch        = g_unichar_tolower (g_utf8_get_char (against_ptr));
        UTF8_FOREACH_FORWARD_WITH_END (query_ptr, query_tok_start, query_tok_end)
        {
          gunichar query_ch = 0;

          query_ch = g_unichar_tolower (g_utf8_get_char (query_ptr));
          if (against_ch != query_ch)
            {
              match = FALSE;
              break;
            }

          against_check_ptr = g_utf8_next_char (against_check_ptr);
          against_ch        = g_unichar_tolower (g_utf8_get_char (against_check_ptr));
        }

        if (match)
          break;
        else
          consumed++;
      }

      if (match)
        {
          score += (double) (query_tok_utf8_len * query_tok_utf8_len) / (double) against_tok_utf8_len;
          query_token_has_match = TRUE;
        }
    }

    if (!query_token_has_match)
      {
        score = 0.0;
        break;
      }
  }

  return score;
}

static inline GUnicodeType
utf8_char_class (const char *s,
                 gunichar   *ch_out)
{
  gunichar     ch = 0;
  GUnicodeType cl = 0;

  ch = g_utf8_get_char (s);
  cl = g_unichar_type (ch);

  if (ch_out != NULL)
    *ch_out = ch;
  return cl;
}

static inline const char *
utf8_skip_to_next_of_class (const char **s,
                            GUnicodeType class,
                            gsize       *read_utf8)
{
  gboolean skipped = FALSE;

  if (read_utf8 != NULL)
    *read_utf8 = 0;

  UTF8_FOREACH_FORWARD (p, *s)
  {
    if (utf8_char_class (p, NULL) == class)
      {
        if (skipped)
          return p;
      }
    else
      {
        if (!skipped)
          {
            *s      = p;
            skipped = TRUE;
          }
        if (read_utf8 != NULL)
          (*read_utf8)++;
      }
  }
  /* return the end of the string if nothing was found */
  return *s + strlen (*s);
}

static gint
cmp_scores (Score *a,
            Score *b)
{
  return (b->val - a->val < 0.0) ? -1 : 1;
}

/* End of bz-search-engine.c */
