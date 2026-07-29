/* search-index-write.c
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

#define G_LOG_DOMAIN  "BAZAAR::SEARCH-INDEX-WRITE"
#define BAZAAR_MODULE "search-index-writer"

#include "bz-entry-group.h"
#include "search-index-write.h"

#define INDEX_MAGIC         "BZSI"
#define INDEX_VERSION       1
#define DESCRIPTION_MAX_LEN 200

static gboolean    write_string (GDataOutputStream *out, const char *str, gsize max_len, GError **error);
static gboolean    entry_group_is_eligible (BzEntryGroup *group);
static const char *entry_group_icon_path (BzEntryGroup *group);

gboolean
bz_write_search_index (GListModel *groups,
                       const char *out_path,
                       GError    **error)
{
  g_autofree char *tmp_path          = NULL;
  g_autoptr (GFile) tmp_file         = NULL;
  g_autoptr (GFile) final_file       = NULL;
  g_autoptr (GOutputStream) base_out = NULL;
  g_autoptr (GDataOutputStream) out  = NULL;
  guint    n_groups                  = 0;
  guint32  count                     = 0;
  gboolean result                    = FALSE;

  g_return_val_if_fail (G_IS_LIST_MODEL (groups), FALSE);
  g_return_val_if_fail (out_path != NULL, FALSE);

  tmp_path = g_strdup_printf ("%s.tmp", out_path);
  tmp_file = g_file_new_for_path (tmp_path);

  base_out = G_OUTPUT_STREAM (g_file_replace (
      tmp_file, NULL, FALSE,
      G_FILE_CREATE_REPLACE_DESTINATION, NULL, error));
  if (base_out == NULL)
    return FALSE;

  out = g_data_output_stream_new (base_out);
  g_data_output_stream_set_byte_order (out, G_DATA_STREAM_BYTE_ORDER_LITTLE_ENDIAN);

  if (!g_output_stream_write_all (G_OUTPUT_STREAM (out), INDEX_MAGIC, 4, NULL, NULL, error))
    return FALSE;
  if (!g_data_output_stream_put_uint32 (out, INDEX_VERSION, NULL, error))
    return FALSE;

  n_groups = g_list_model_get_n_items (groups);
  for (guint i = 0; i < n_groups; i++)
    {
      g_autoptr (BzEntryGroup) group = g_list_model_get_item (groups, i);
      if (entry_group_is_eligible (group))
        count++;
    }

  if (!g_data_output_stream_put_uint32 (out, count, NULL, error))
    return FALSE;

  for (guint i = 0; i < n_groups; i++)
    {
      g_autoptr (BzEntryGroup) group = g_list_model_get_item (groups, i);
      const char *icon_path          = NULL;

      if (!entry_group_is_eligible (group))
        continue;

      icon_path = entry_group_icon_path (group);

      if (!write_string (out, bz_entry_group_get_id (group), 0, error) ||
          !write_string (out, bz_entry_group_get_title (group), 0, error) ||
          !write_string (out, bz_entry_group_get_developer (group), 0, error) ||
          !write_string (out, bz_entry_group_get_description (group), DESCRIPTION_MAX_LEN, error) ||
          !write_string (out, bz_entry_group_get_search_tokens (group), 0, error) ||
          !write_string (out, icon_path, 0, error))
        return FALSE;
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (out), NULL, error))
    return FALSE;

  final_file = g_file_new_for_path (out_path);
  result     = g_file_move (
      tmp_file, final_file,
      G_FILE_COPY_OVERWRITE,
      NULL, NULL, NULL, error);

  return result;
}

static gboolean
write_string (GDataOutputStream *out,
              const char        *str,
              gsize              max_len,
              GError           **error)
{
  gsize len = 0;

  if (str != NULL)
    {
      len = strlen (str);
      if (max_len > 0 && len > max_len)
        len = max_len;
    }

  if (!g_data_output_stream_put_uint32 (out, (guint32) len, NULL, error))
    return FALSE;

  if (len > 0)
    return g_output_stream_write_all (G_OUTPUT_STREAM (out), str, len, NULL, NULL, error);

  return TRUE;
}

static gboolean
entry_group_is_eligible (BzEntryGroup *group)
{
  return bz_entry_group_is_searchable (group) &&
         !bz_entry_group_is_addon (group) &&
         bz_entry_group_get_removable (group) == 0 &&
         bz_entry_group_get_eol (group) == NULL;
}

static const char *
entry_group_icon_path (BzEntryGroup *group)
{
  GIcon *icon = NULL;

  icon = bz_entry_group_get_mini_icon (group);
  if (icon == NULL || !G_IS_FILE_ICON (icon))
    return NULL;

  return g_file_peek_path (g_file_icon_get_file (G_FILE_ICON (icon)));
}
