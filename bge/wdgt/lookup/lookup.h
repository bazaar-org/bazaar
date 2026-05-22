/* lookup.c
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

#include <graphene-gobject.h>

#include "bge.h"

G_BEGIN_DECLS

#define ARGBUF_SIZE 128

typedef GskTransform *(*TransformCallFunc) (GskTransform *next,
                                            const GValue  args[]);
typedef struct
{
  const char       *name;
  guint             n_args;
  GType             args[16];
  gpointer          func;
  TransformCallFunc call;
} TransformInstr;
gboolean
lookup_transform_instr (const char     *lookup_name,
                        TransformInstr *out);

typedef void (*PathBuilderCallFunc) (GskPathBuilder *builder,
                                     const GValue    args[]);
typedef struct
{
  const char         *name;
  guint               n_args;
  GType               args[16];
  gpointer            func;
  PathBuilderCallFunc call;
} PathBuilderInstr;
gboolean
lookup_path_builder_instr (const char       *lookup_name,
                           PathBuilderInstr *out);

typedef void (*SnapshotCallFunc) (GtkSnapshot *snapshot,
                                  const GValue args[],
                                  const GValue rest[],
                                  guint        n_rest);
typedef struct
{
  const char      *name;
  guint            n_args;
  guint            n_rest;
  GType            args[16];
  gpointer         func;
  SnapshotCallFunc call;
  /* for push instrs only */
  guint n_pops;
} SnapshotInstr;
gboolean
lookup_snapshot_push_instr (const char    *lookup_name,
                            SnapshotInstr *out);
gboolean
lookup_snapshot_transform_instr (const char    *lookup_name,
                                 SnapshotInstr *out);
gboolean
lookup_snapshot_append_instr (const char    *lookup_name,
                              SnapshotInstr *out);

G_END_DECLS
