/* search-index.c
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

#include "search-index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define INDEX_MAGIC "BZSI"

static int    read_u32 (FILE *f, unsigned int *out);
static char  *read_field (FILE *f);
static void   free_entries (SearchIndexEntry *entries, unsigned int count);
static double score_field (const char *term, const char *field, double weight);
static double score_entry (const SearchIndexEntry *e, const char *const *terms, int n_terms);
static int    cmp_matches (const void *a, const void *b);

SearchIndex *
search_index_open (const char *path)
{
  FILE             *f        = NULL;
  char              magic[4] = { 0 };
  unsigned int      version  = 0;
  unsigned int      count    = 0;
  SearchIndexEntry *entries  = NULL;
  SearchIndex      *idx      = NULL;
  struct stat       st       = { 0 };
  unsigned int      i        = 0;

  f = fopen (path, "rb");
  if (f == NULL)
    return NULL;

  if (fread (magic, 1, 4, f) != 4 || memcmp (magic, INDEX_MAGIC, 4) != 0)
    {
      fclose (f);
      return NULL;
    }

  if (read_u32 (f, &version) < 0 || read_u32 (f, &count) < 0)
    {
      fclose (f);
      return NULL;
    }

  entries = calloc (count, sizeof (SearchIndexEntry));
  if (entries == NULL)
    {
      fclose (f);
      return NULL;
    }

  for (i = 0; i < count; i++)
    {
      entries[i].id            = read_field (f);
      entries[i].title         = read_field (f);
      entries[i].developer     = read_field (f);
      entries[i].description   = read_field (f);
      entries[i].search_tokens = read_field (f);
      entries[i].icon_path     = read_field (f);

      if (entries[i].id == NULL || entries[i].title == NULL)
        {
          free_entries (entries, count);
          fclose (f);
          return NULL;
        }
    }

  fclose (f);

  idx = calloc (1, sizeof (SearchIndex));
  if (idx == NULL)
    {
      free_entries (entries, count);
      return NULL;
    }

  idx->path    = strdup (path);
  idx->entries = entries;
  idx->count   = count;

  if (stat (path, &st) == 0)
    idx->mtime = st.st_mtime;

  return idx;
}

void
search_index_close (SearchIndex *idx)
{
  if (idx == NULL)
    return;

  free_entries (idx->entries, idx->count);
  free (idx->path);
  free (idx);
}

int
search_index_reload_if_stale (SearchIndex **idx_ptr)
{
  struct stat  st    = { 0 };
  SearchIndex *idx   = NULL;
  SearchIndex *fresh = NULL;

  idx = *idx_ptr;

  if (idx == NULL)
    return 0;

  if (stat (idx->path, &st) != 0)
    return 0;

  if (st.st_mtime == idx->mtime)
    return 0;

  fresh = search_index_open (idx->path);
  if (fresh == NULL)
    return 0;

  search_index_close (idx);
  *idx_ptr = fresh;
  return 1;
}

size_t
search_index_query (SearchIndex       *idx,
                    const char *const *terms,
                    int                n_terms,
                    SearchIndexMatch  *out,
                    size_t             max_results)
{
  size_t       n_matches = 0;
  unsigned int i         = 0;
  double       score     = 0.0;

  if (idx == NULL || n_terms <= 0)
    return 0;

  for (i = 0; i < idx->count && n_matches < max_results * 8; i++)
    {
      score = score_entry (&idx->entries[i], terms, n_terms);

      if (score > 0.0 && n_matches < max_results * 8)
        {
          if (n_matches < max_results)
            {
              out[n_matches].entry = &idx->entries[i];
              out[n_matches].score = score;
            }
          n_matches++;
        }
    }

  if (n_matches > max_results)
    n_matches = max_results;

  qsort (out, n_matches, sizeof (SearchIndexMatch), cmp_matches);

  return n_matches;
}

const SearchIndexEntry *
search_index_find (SearchIndex *idx,
                   const char  *id)
{
  unsigned int i = 0;

  if (idx == NULL || id == NULL)
    return NULL;

  for (i = 0; i < idx->count; i++)
    {
      if (strcmp (idx->entries[i].id, id) == 0)
        return &idx->entries[i];
    }

  return NULL;
}

static int
read_u32 (FILE         *f,
          unsigned int *out)
{
  unsigned char b[4] = { 0 };

  if (fread (b, 1, 4, f) != 4)
    return -1;

  *out = (unsigned int) b[0] |
         ((unsigned int) b[1] << 8) |
         ((unsigned int) b[2] << 16) |
         ((unsigned int) b[3] << 24);
  return 0;
}

static char *
read_field (FILE *f)
{
  unsigned int len = 0;
  char        *str = NULL;

  if (read_u32 (f, &len) < 0)
    return NULL;

  if (len == 0)
    return NULL;

  str = malloc (len + 1);
  if (str == NULL)
    return NULL;

  if (fread (str, 1, len, f) != len)
    {
      free (str);
      return NULL;
    }

  str[len] = '\0';
  return str;
}

static void
free_entries (SearchIndexEntry *entries,
              unsigned int      count)
{
  unsigned int i = 0;

  if (entries == NULL)
    return;

  for (i = 0; i < count; i++)
    {
      free (entries[i].id);
      free (entries[i].title);
      free (entries[i].developer);
      free (entries[i].description);
      free (entries[i].search_tokens);
      free (entries[i].icon_path);
    }

  free (entries);
}

static double
score_field (const char *term,
             const char *field,
             double      weight)
{
  const char *hit       = NULL;
  size_t      term_len  = 0;
  size_t      field_len = 0;

  if (field == NULL || term == NULL || *term == '\0')
    return 0.0;

  hit = strcasestr (field, term);
  if (hit == NULL)
    return 0.0;

  term_len  = strlen (term);
  field_len = strlen (field);
  if (field_len == 0)
    return 0.0;

  return weight * ((double) (term_len * term_len) / (double) field_len);
}

static double
score_entry (const SearchIndexEntry *e,
             const char *const      *terms,
             int                     n_terms)
{
  double      total      = 0.0;
  int         i          = 0;
  const char *term       = NULL;
  double      term_score = 0.0;

  for (i = 0; i < n_terms; i++)
    {
      term       = terms[i];
      term_score = 0.0;

      if (term == NULL || *term == '\0')
        continue;

      term_score += score_field (term, e->title, 2.0);
      term_score += score_field (term, e->developer, 1.0);
      term_score += score_field (term, e->description, 1.0);
      term_score += score_field (term, e->search_tokens, 1.5);

      if (term_score <= 0.0)
        return 0.0;

      total += term_score;
    }

  return total;
}

static int
cmp_matches (const void *a,
             const void *b)
{
  const SearchIndexMatch *ma = a;
  const SearchIndexMatch *mb = b;

  if (mb->score > ma->score)
    return 1;
  if (mb->score < ma->score)
    return -1;
  return 0;
}
