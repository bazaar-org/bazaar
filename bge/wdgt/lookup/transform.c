/* transform.c
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

#include "lookup.h"

static GskTransform *
transform_instr_transform (GskTransform *next,
                           const GValue  args[])
{
  return gsk_transform_transform (
      next,
      g_value_get_boxed (&args[0]));
}

static GskTransform *
transform_instr_invert (GskTransform *next,
                        const GValue  args[])
{
  return gsk_transform_transform (
      next,
      g_value_get_boxed (&args[0]));
}

static GskTransform *
transform_instr_matrix (GskTransform *next,
                        const GValue  args[])
{
  return gsk_transform_matrix (
      next,
      g_value_get_boxed (&args[0]));
}

static GskTransform *
transform_instr_matrix_2d (GskTransform *next,
                           const GValue  args[])
{
  return gsk_transform_matrix_2d (
      next,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]),
      g_value_get_double (&args[5]));
}

static GskTransform *
transform_instr_translate (GskTransform *next,
                           const GValue  args[])
{
  return gsk_transform_translate (
      next,
      g_value_get_boxed (&args[0]));
}

static GskTransform *
transform_instr_translate_3d (GskTransform *next,
                              const GValue  args[])
{
  return gsk_transform_translate_3d (
      next,
      g_value_get_boxed (&args[0]));
}

static GskTransform *
transform_instr_skew (GskTransform *next,
                      const GValue  args[])
{
  return gsk_transform_skew (
      next,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static GskTransform *
transform_instr_rotate (GskTransform *next,
                        const GValue  args[])
{
  return gsk_transform_rotate (
      next,
      g_value_get_double (&args[0]));
}

static GskTransform *
transform_instr_rotate_3d (GskTransform *next,
                           const GValue  args[])
{
  return gsk_transform_rotate_3d (
      next,
      g_value_get_double (&args[0]),
      g_value_get_boxed (&args[1]));
}

static GskTransform *
transform_instr_scale (GskTransform *next,
                       const GValue  args[])
{
  return gsk_transform_scale (
      next,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static GskTransform *
transform_instr_scale_3d (GskTransform *next,
                          const GValue  args[])
{
  return gsk_transform_scale_3d (
      next,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]));
}

static GskTransform *
transform_instr_perspective (GskTransform *next,
                             const GValue  args[])
{
  return gsk_transform_perspective (
      next,
      g_value_get_double (&args[0]));
}

gboolean
lookup_transform_instr (const char     *lookup_name,
                        TransformInstr *out)
{
  TransformInstr instrs[] = {
    {
     "transform",
     1,
     {
     GSK_TYPE_TRANSFORM,
     },
     gsk_transform_transform,
     transform_instr_transform,
     },
    {
     "invert",
     0,
     {},
     gsk_transform_invert,
     transform_instr_invert,
     },
    {
     "matrix",
     1,
     {
     GRAPHENE_TYPE_MATRIX,
     },
     gsk_transform_matrix,
     transform_instr_matrix,
     },
    {
     "matrix-2d",
     6,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_transform_matrix_2d,
     transform_instr_matrix_2d,
     },
    {
     "translate",
     1,
     {
     GRAPHENE_TYPE_POINT,
     },
     gsk_transform_translate,
     transform_instr_translate,
     },
    {
     "translate-3d",
     1,
     {
     GRAPHENE_TYPE_POINT3D,
     },
     gsk_transform_translate_3d,
     transform_instr_translate_3d,
     },
    {
     "skew",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_transform_skew,
     transform_instr_skew,
     },
    {
     "rotate",
     1,
     {
     G_TYPE_DOUBLE,
     },
     gsk_transform_rotate,
     transform_instr_rotate,
     },
    {
     "rotate-3d",
     2,
     {
     G_TYPE_DOUBLE,
     GRAPHENE_TYPE_VEC3,
     },
     gsk_transform_rotate_3d,
     transform_instr_rotate_3d,
     },
    {
     "scale",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_transform_scale,
     transform_instr_scale,
     },
    {
     "scale-3d",
     3,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_transform_scale_3d,
     transform_instr_scale_3d,
     },
    {
     "perspective",
     1,
     {
     G_TYPE_DOUBLE,
     },
     gsk_transform_perspective,
     transform_instr_perspective,
     },
  };

  for (guint i = 0; i < G_N_ELEMENTS (instrs); i++)
    {
      if (g_strcmp0 (lookup_name, instrs[i].name) == 0)
        {
          *out = instrs[i];
          return TRUE;
        }
    }
  return FALSE;
}
