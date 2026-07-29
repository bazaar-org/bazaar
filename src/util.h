/* util.h
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

#pragma once

#include <libdex.h>

#define bz_maybe(_ptr, _func)     ((_ptr) != NULL ? (_func) ((_ptr)) : NULL)
#define bz_maybe_strdup(_ptr)     bz_maybe (_ptr, g_strdup)
#define bz_maybe_ref(_ptr, _ref)  ((typeof (_ptr)) bz_maybe (_ptr, _ref))
#define bz_object_maybe_ref(_obj) bz_maybe_ref ((_obj), g_object_ref)
#define bz_dex_maybe_ref(_obj)    bz_maybe_ref ((_obj), dex_ref)

#define BZ_RELEASE_DATA(name, unref)          \
  if ((unref) != NULL)                        \
    {                                         \
      g_clear_pointer (&self->name, (unref)); \
    }

#define BZ_RELEASE_UTAG(name, remove)            \
  if ((remove) != NULL)                          \
    {                                            \
      g_clear_handle_id (&self->name, (remove)); \
    }

/* va args = releases */
#define BZ_DEFINE_DATA(name, Name, layout, ...)    \
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

/* Be careful with deadlocks */
typedef DexFuture BzGuard;
static inline void
bz_guard_destroy (BzGuard *guard)
{
  if (dex_future_is_pending (guard))
    dex_promise_resolve_boolean (DEX_PROMISE (guard), TRUE);
  dex_unref (guard);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (BzGuard, bz_guard_destroy);
#define bz_clear_guard(_pp) g_clear_pointer (_pp, bz_guard_destroy)

#define BZ_BEGIN_GUARD_WITH_CONTEXT(_guard, _mutex, _gate) \
  G_STMT_START                                             \
  {                                                        \
    g_autoptr (GMutexLocker) _locker = NULL;               \
    g_autoptr (DexFuture) _wait      = NULL;               \
                                                           \
    _locker = g_mutex_locker_new (_mutex);                 \
    if (*(_guard) == NULL)                                 \
      *(_guard) = (DexFuture *) dex_promise_new ();        \
    if (*(_gate) != NULL)                                  \
      {                                                    \
        if (dex_future_is_pending (*(_gate)))              \
          _wait = g_steal_pointer (_gate);                 \
        else                                               \
          dex_clear (_gate);                               \
      }                                                    \
    *(_gate) = dex_ref (*(_guard));                        \
    g_clear_pointer (&_locker, g_mutex_locker_free);       \
                                                           \
    if (_wait != NULL)                                     \
      dex_await (g_steal_pointer (&_wait), NULL);          \
  }                                                        \
  G_STMT_END

#define BZ_BEGIN_GUARD(_guard)                             \
  G_STMT_START                                             \
  {                                                        \
    static GMutex   _mutex = { 0 };                        \
    static BzGuard *_gate  = NULL;                         \
    BZ_BEGIN_GUARD_WITH_CONTEXT (_guard, &_mutex, &_gate); \
  }                                                        \
  G_STMT_END

/* Use with dex_scheduler_spawn */
G_GNUC_UNUSED
static GWeakRef *
bz_track_weak (gpointer object)
{
  GWeakRef *wr = NULL;

  if (object == NULL)
    return NULL;

  wr = g_new0 (typeof (*wr), 1);
  g_weak_ref_init (wr, object);
  return wr;
}

G_GNUC_UNUSED
static void
bz_weak_release (gpointer ptr)
{
  GWeakRef *wr = ptr;

  g_weak_ref_clear (wr);
  g_free (wr);
}

#define bz_weak_get_or_return(self, wr) \
  G_STMT_START                          \
  {                                     \
    (self) = g_weak_ref_get (wr);       \
    if ((self) == NULL)                 \
      return;                           \
  }                                     \
  G_STMT_END

#define bz_weak_get_or_return_reject(self, wr) \
  G_STMT_START                                 \
  {                                            \
    (self) = g_weak_ref_get (wr);              \
    if ((self) == NULL)                        \
      return dex_future_new_reject (           \
          G_IO_ERROR,                          \
          G_IO_ERROR_CANCELLED,                \
          "Object was discarded");             \
  }                                            \
  G_STMT_END

G_GNUC_UNUSED
static void
_bz_debug_print_when_disposed_cb (gpointer ptr);

BZ_DEFINE_DATA (
    _bz_debug_dispose_cb,
    _BzDebugDisposeCb,
    {
      GType       type;
      const char *loc;
      guint64     time;
    },
    _bz_debug_print_when_disposed_cb (self);)

G_GNUC_UNUSED
static void
_bz_debug_print_when_disposed_cb (gpointer ptr)
{
  _BzDebugDisposeCbData *data = ptr;

  g_print ("%zu OBJECT DISPOSE: type %s; from %s at %zu\n",
           g_get_monotonic_time (),
           g_type_name (data->type),
           data->loc,
           data->time);
}

#define BZ_DEBUG_PRINT_WHEN_DISPOSED(_object)       \
  G_STMT_START                                      \
  {                                                 \
    g_autoptr (_BzDebugDisposeCbData) _data = NULL; \
                                                    \
    _data       = _bz_debug_dispose_cb_data_new (); \
    _data->type = G_OBJECT_TYPE (_object);          \
    _data->loc  = G_STRLOC;                         \
    _data->time = g_get_monotonic_time ();          \
                                                    \
    g_object_set_data_full (                        \
        G_OBJECT (_object),                         \
        "BZ_DEBUG_PRINT_WHEN_DISPOSED",             \
        _bz_debug_dispose_cb_data_ref (_data),      \
        _bz_debug_dispose_cb_data_unref);           \
  }                                                 \
  G_STMT_END
