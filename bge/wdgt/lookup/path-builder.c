/* path-builder.c
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

static void
path_builder_instr_add_circle (GskPathBuilder *builder,
                               const GValue    args[])
{
  gsk_path_builder_add_circle (
      builder,
      g_value_get_boxed (&args[0]),
      g_value_get_double (&args[1]));
}

static void
path_builder_instr_add_layout (GskPathBuilder *builder,
                               const GValue    args[])
{
  gsk_path_builder_add_layout (
      builder,
      g_value_get_object (&args[0]));
}

static void
path_builder_instr_add_path (GskPathBuilder *builder,
                             const GValue    args[])
{
  gsk_path_builder_add_path (
      builder,
      g_value_get_boxed (&args[0]));
}

static void
path_builder_instr_add_rect (GskPathBuilder *builder,
                             const GValue    args[])
{
  gsk_path_builder_add_rect (
      builder,
      g_value_get_boxed (&args[0]));
}

static void
path_builder_instr_add_reverse_path (GskPathBuilder *builder,
                                     const GValue    args[])
{
  gsk_path_builder_add_reverse_path (
      builder,
      g_value_get_boxed (&args[0]));
}

static void
path_builder_instr_add_rounded_rect (GskPathBuilder *builder,
                                     const GValue    args[])
{
  GskRoundedRect rrect = { 0 };

  rrect.bounds    = *(graphene_rect_t *) g_value_get_boxed (&args[0]);
  rrect.corner[0] = *(graphene_size_t *) g_value_get_boxed (&args[1]);
  rrect.corner[1] = *(graphene_size_t *) g_value_get_boxed (&args[2]);
  rrect.corner[2] = *(graphene_size_t *) g_value_get_boxed (&args[3]);
  rrect.corner[3] = *(graphene_size_t *) g_value_get_boxed (&args[4]);

  gsk_path_builder_add_rounded_rect (
      builder,
      &rrect);
}

static void
path_builder_instr_add_segment (GskPathBuilder *builder,
                                const GValue    args[])
{
  gsk_path_builder_add_segment (
      builder,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_boxed (&args[2]));
}

static void
path_builder_instr_arc_to (GskPathBuilder *builder,
                           const GValue    args[])
{
  gsk_path_builder_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]));
}

static void
path_builder_instr_close (GskPathBuilder *builder,
                          const GValue    args[])
{
  gsk_path_builder_close (builder);
}

static void
path_builder_instr_conic_to (GskPathBuilder *builder,
                             const GValue    args[])
{
  gsk_path_builder_conic_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]));
}

static void
path_builder_instr_cubic_to (GskPathBuilder *builder,
                             const GValue    args[])
{
  gsk_path_builder_cubic_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]),
      g_value_get_double (&args[5]));
}

static void
path_builder_instr_rel_arc_to (GskPathBuilder *builder,
                               const GValue    args[])
{
  gsk_path_builder_rel_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]));
}

static void
path_builder_instr_html_arc_to (GskPathBuilder *builder,
                                const GValue    args[])
{
  gsk_path_builder_html_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]));
}

static void
path_builder_instr_line_to (GskPathBuilder *builder,
                            const GValue    args[])
{
  gsk_path_builder_line_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static void
path_builder_instr_move_to (GskPathBuilder *builder,
                            const GValue    args[])
{
  gsk_path_builder_move_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static void
path_builder_instr_quad_to (GskPathBuilder *builder,
                            const GValue    args[])
{
  gsk_path_builder_quad_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]));
}

static void
path_builder_instr_rel_conic_to (GskPathBuilder *builder,
                                 const GValue    args[])
{
  gsk_path_builder_rel_conic_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]));
}

static void
path_builder_instr_rel_cubic_to (GskPathBuilder *builder,
                                 const GValue    args[])
{
  gsk_path_builder_rel_cubic_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]),
      g_value_get_double (&args[5]));
}

static void
path_builder_instr_rel_html_arc_to (GskPathBuilder *builder,
                                    const GValue    args[])
{
  gsk_path_builder_rel_html_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]));
}

static void
path_builder_instr_rel_line_to (GskPathBuilder *builder,
                                const GValue    args[])
{
  gsk_path_builder_rel_line_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static void
path_builder_instr_rel_move_to (GskPathBuilder *builder,
                                const GValue    args[])
{
  gsk_path_builder_rel_move_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static void
path_builder_instr_rel_quad_to (GskPathBuilder *builder,
                                const GValue    args[])
{
  gsk_path_builder_rel_quad_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]));
}

static void
path_builder_instr_svg_arc_to (GskPathBuilder *builder,
                               const GValue    args[])
{
  gsk_path_builder_svg_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_boolean (&args[3]),
      g_value_get_boolean (&args[4]),
      g_value_get_double (&args[5]),
      g_value_get_double (&args[6]));
}

static void
path_builder_instr_rel_svg_arc_to (GskPathBuilder *builder,
                                   const GValue    args[])
{
  gsk_path_builder_rel_svg_arc_to (
      builder,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_boolean (&args[3]),
      g_value_get_boolean (&args[4]),
      g_value_get_double (&args[5]),
      g_value_get_double (&args[6]));
}

gboolean
lookup_path_builder_instr (const char       *lookup_name,
                           PathBuilderInstr *out)
{
  PathBuilderInstr instrs[] = {
    {
     "add-circle",
     2,
     {
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_add_circle,
     path_builder_instr_add_circle,
     },
    {
     "add-layout",
     1,
     {
     PANGO_TYPE_LAYOUT,
     },
     gsk_path_builder_add_layout,
     path_builder_instr_add_layout,
     },
    {
     "add-path",
     1,
     {
     GSK_TYPE_PATH,
     },
     gsk_path_builder_add_path,
     path_builder_instr_add_path,
     },
    {
     "add-rect",
     1,
     {
     GRAPHENE_TYPE_RECT,
     },
     gsk_path_builder_add_rect,
     path_builder_instr_add_rect,
     },
    {
     "add-reverse-path",
     1,
     {
     GSK_TYPE_PATH,
     },
     gsk_path_builder_add_reverse_path,
     path_builder_instr_add_reverse_path,
     },
    {
     "add-rounded-rect",
     5,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     },
     gsk_path_builder_add_rounded_rect,
     path_builder_instr_add_rounded_rect,
     },
    {
     "add-segment",
     3,
     {
     GSK_TYPE_PATH,
     GSK_TYPE_PATH_POINT,
     GSK_TYPE_PATH_POINT,
     },
     gsk_path_builder_add_segment,
     path_builder_instr_add_segment,
     },
    {
     "arc-to",
     4,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_arc_to,
     path_builder_instr_arc_to,
     },
    {
     "close",
     0,
     {},
     gsk_path_builder_close,
     path_builder_instr_close,
     },
    {
     "conic-to",
     5,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_conic_to,
     path_builder_instr_conic_to,
     },
    {
     "cubic-to",
     6,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_cubic_to,
     path_builder_instr_cubic_to,
     },
    {
     "html-arc-to",
     5,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_html_arc_to,
     path_builder_instr_html_arc_to,
     },
    {
     "line-to",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_line_to,
     path_builder_instr_line_to,
     },
    {
     "move-to",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_move_to,
     path_builder_instr_move_to,
     },
    {
     "quad-to",
     4,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_quad_to,
     path_builder_instr_quad_to,
     },
    {
     "rel-arc-to",
     4,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_arc_to,
     path_builder_instr_rel_arc_to,
     },
    {
     "rel-conic-to",
     5,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_conic_to,
     path_builder_instr_rel_conic_to,
     },
    {
     "rel-cubic-to",
     6,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_cubic_to,
     path_builder_instr_rel_cubic_to,
     },
    {
     "rel-html-arc-to",
     5,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_html_arc_to,
     path_builder_instr_rel_html_arc_to,
     },
    {
     "rel-line-to",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_line_to,
     path_builder_instr_rel_line_to,
     },
    {
     "rel-move-to",
     2,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_move_to,
     path_builder_instr_rel_move_to,
     },
    {
     "rel-quad-to",
     4,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_quad_to,
     path_builder_instr_rel_quad_to,
     },
    {
     "rel-svg-arc-to",
     7,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_BOOLEAN,
     G_TYPE_BOOLEAN,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_rel_svg_arc_to,
     path_builder_instr_rel_svg_arc_to,
     },
    {
     "svg-arc-to",
     7,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_BOOLEAN,
     G_TYPE_BOOLEAN,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gsk_path_builder_svg_arc_to,
     path_builder_instr_svg_arc_to,
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
