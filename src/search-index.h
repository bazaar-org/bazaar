/* search-index.h
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

#pragma once

#include <stddef.h>
#include <time.h>

typedef struct
{
  char *id;
  char *title;
  char *developer;
  char *description;
  char *search_tokens;
  char *icon_path;
} SearchIndexEntry;

typedef struct
{
  char             *path;
  time_t            mtime;
  SearchIndexEntry *entries;
  unsigned int      count;
} SearchIndex;

typedef struct
{
  const SearchIndexEntry *entry;
  double                  score;
} SearchIndexMatch;

SearchIndex *
search_index_open (const char *path);

void
search_index_close (SearchIndex *idx);

int
search_index_reload_if_stale (SearchIndex **idx);

size_t
search_index_query (SearchIndex       *idx,
                    const char *const *terms,
                    int                n_terms,
                    SearchIndexMatch  *out,
                    size_t             max_results);

const SearchIndexEntry *
search_index_find (SearchIndex *idx, const char *id);
