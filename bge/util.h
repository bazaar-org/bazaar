/* util.h
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

#pragma once

#define BGE_RELEASE_DATA(name, unref)         \
  if ((unref) != NULL)                        \
    {                                         \
      g_clear_pointer (&self->name, (unref)); \
    }

#define BGE_RELEASE_UTAG(name, remove)           \
  if ((remove) != NULL)                          \
    {                                            \
      g_clear_handle_id (&self->name, (remove)); \
    }

/* va args = releases */
#define BGE_DEFINE_DATA(name, Name, layout, ...)   \
  typedef struct _##Name##Data Name##Data;         \
  struct _##Name##Data                             \
  {                                                \
    gatomicrefcount rc;                            \
    struct layout;                                 \
  };                                               \
  G_GNUC_UNUSED                                    \
  static inline Name##Data *                       \
  name##_data_new (void)                           \
  {                                                \
    Name##Data *data = NULL;                       \
    data             = g_new0 (typeof (*data), 1); \
    g_atomic_ref_count_init (&data->rc);           \
    return data;                                   \
  }                                                \
  G_GNUC_UNUSED                                    \
  static inline Name##Data *                       \
  name##_data_ref (gpointer ptr)                   \
  {                                                \
    Name##Data *self = ptr;                        \
    g_atomic_ref_count_inc (&self->rc);            \
    return self;                                   \
  }                                                \
  G_GNUC_UNUSED                                    \
  static void                                      \
  name##_data_unref (gpointer ptr);                \
  G_GNUC_UNUSED                                    \
  static void                                      \
  name##_data_deinit (gpointer ptr)                \
  {                                                \
    Name##Data *self = ptr;                        \
    __VA_ARGS__                                    \
  }                                                \
  G_GNUC_UNUSED                                    \
  static void                                      \
  name##_data_unref (gpointer ptr)                 \
  {                                                \
    Name##Data *self = ptr;                        \
    if (g_atomic_ref_count_dec (&self->rc))        \
      {                                            \
        name##_data_deinit (self);                 \
        g_free (self);                             \
        self = NULL;                               \
      }                                            \
  }                                                \
  G_GNUC_UNUSED                                    \
  static void                                      \
  name##_data_unref_closure (gpointer  data,       \
                             GClosure *closure)    \
  {                                                \
    name##_data_unref (data);                      \
  }                                                \
  G_DEFINE_AUTOPTR_CLEANUP_FUNC (Name##Data, name##_data_unref);
