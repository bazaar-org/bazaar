/* snapshot.c
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
snapshot_push_instr_opacity (GtkSnapshot *snapshot,
                             const GValue args[],
                             const GValue rest[],
                             guint        n_rest)
{
  gtk_snapshot_push_opacity (
      snapshot,
      g_value_get_double (&args[0]));
}

static void
snapshot_push_instr_isolation (GtkSnapshot *snapshot,
                               const GValue args[],
                               const GValue rest[],
                               guint        n_rest)
{
  gtk_snapshot_push_isolation (
      snapshot,
      g_value_get_flags (&args[0]));
}

static void
snapshot_push_instr_blur (GtkSnapshot *snapshot,
                          const GValue args[],
                          const GValue rest[],
                          guint        n_rest)
{
  gtk_snapshot_push_blur (
      snapshot,
      g_value_get_double (&args[0]));
}

static void
snapshot_push_instr_color_matrix (GtkSnapshot *snapshot,
                                  const GValue args[],
                                  const GValue rest[],
                                  guint        n_rest)
{
  gtk_snapshot_push_color_matrix (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_push_instr_component_transfer (GtkSnapshot *snapshot,
                                        const GValue args[],
                                        const GValue rest[],
                                        guint        n_rest)
{
  gtk_snapshot_push_component_transfer (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_boxed (&args[2]),
      g_value_get_boxed (&args[3]));
}

static void
snapshot_push_instr_repeat (GtkSnapshot *snapshot,
                            const GValue args[],
                            const GValue rest[],
                            guint        n_rest)
{
  gtk_snapshot_push_repeat (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_push_instr_clip (GtkSnapshot *snapshot,
                          const GValue args[],
                          const GValue rest[],
                          guint        n_rest)
{
  gtk_snapshot_push_clip (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_push_instr_rounded_clip (GtkSnapshot *snapshot,
                                  const GValue args[],
                                  const GValue rest[],
                                  guint        n_rest)
{
  GskRoundedRect rrect = { 0 };

  rrect.bounds    = *(graphene_rect_t *) g_value_get_boxed (&args[0]);
  rrect.corner[0] = *(graphene_size_t *) g_value_get_boxed (&args[1]);
  rrect.corner[1] = *(graphene_size_t *) g_value_get_boxed (&args[2]);
  rrect.corner[2] = *(graphene_size_t *) g_value_get_boxed (&args[3]);
  rrect.corner[3] = *(graphene_size_t *) g_value_get_boxed (&args[4]);

  gtk_snapshot_push_rounded_clip (
      snapshot,
      &rrect);
}

static void
snapshot_push_instr_fill (GtkSnapshot *snapshot,
                          const GValue args[],
                          const GValue rest[],
                          guint        n_rest)
{
  gtk_snapshot_push_fill (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_enum (&args[1]));
}

static void
snapshot_push_instr_stroke (GtkSnapshot *snapshot,
                            const GValue args[],
                            const GValue rest[],
                            guint        n_rest)
{
  gtk_snapshot_push_stroke (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_push_instr_shadow (GtkSnapshot *snapshot,
                            const GValue args[],
                            const GValue rest[],
                            guint        n_rest)
{
  guint     n_shadows            = 0;
  GskShadow shadows[ARGBUF_SIZE] = { 0 };

  n_shadows = MIN (n_rest / 4, G_N_ELEMENTS (shadows));

  for (guint i = 0; i < n_shadows; i++)
    {
      shadows[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 4 + 0]);
      shadows[i].dx     = g_value_get_double (&rest[i * 4 + 1]);
      shadows[i].dy     = g_value_get_double (&rest[i * 4 + 2]);
      shadows[i].radius = g_value_get_double (&rest[i * 4 + 3]);
    }

  gtk_snapshot_push_shadow (
      snapshot,
      shadows,
      n_shadows);
}

static void
snapshot_push_instr_blend (GtkSnapshot *snapshot,
                           const GValue args[],
                           const GValue rest[],
                           guint        n_rest)
{
  gtk_snapshot_push_blend (
      snapshot,
      g_value_get_enum (&args[0]));
}

static void
snapshot_push_instr_mask (GtkSnapshot *snapshot,
                          const GValue args[],
                          const GValue rest[],
                          guint        n_rest)
{
  gtk_snapshot_push_mask (
      snapshot,
      g_value_get_enum (&args[0]));
}

static void
snapshot_push_instr_copy (GtkSnapshot *snapshot,
                          const GValue args[],
                          const GValue rest[],
                          guint        n_rest)
{
  gtk_snapshot_push_copy (
      snapshot);
}

static void
snapshot_push_instr_composite (GtkSnapshot *snapshot,
                               const GValue args[],
                               const GValue rest[],
                               guint        n_rest)
{
  gtk_snapshot_push_composite (
      snapshot,
      g_value_get_enum (&args[0]));
}

static void
snapshot_push_instr_cross_fade (GtkSnapshot *snapshot,
                                const GValue args[],
                                const GValue rest[],
                                guint        n_rest)
{
  gtk_snapshot_push_cross_fade (
      snapshot,
      g_value_get_double (&args[0]));
}

gboolean
lookup_snapshot_push_instr (const char    *lookup_name,
                            SnapshotInstr *out)
{
  SnapshotInstr instrs[] = {
    {
     "opacity",
     1,
     0,
     {
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_push_opacity,
     snapshot_push_instr_opacity,
     1,
     },
    {
     "isolation",
     1,
     0,
     {
     GSK_TYPE_ISOLATION,
     },
     gtk_snapshot_push_isolation,
     snapshot_push_instr_isolation,
     1,
     },
    {
     "blur",
     1,
     0,
     {
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_push_blur,
     snapshot_push_instr_blur,
     1,
     },
    {
     "color-matrix",
     2,
     0,
     {
     GRAPHENE_TYPE_MATRIX,
     GRAPHENE_TYPE_VEC4,
     },
     gtk_snapshot_push_color_matrix,
     snapshot_push_instr_color_matrix,
     2,
     },
    {
     "component-transfer",
     4,
     0,
     {
     GSK_TYPE_COMPONENT_TRANSFER,
     GSK_TYPE_COMPONENT_TRANSFER,
     GSK_TYPE_COMPONENT_TRANSFER,
     GSK_TYPE_COMPONENT_TRANSFER,
     },
     gtk_snapshot_push_component_transfer,
     snapshot_push_instr_component_transfer,
     2,
     },
    {
     "repeat",
     2,
     0,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_RECT,
     },
     gtk_snapshot_push_repeat,
     snapshot_push_instr_repeat,
     2,
     },
    {
     "clip",
     1,
     0,
     {
     GRAPHENE_TYPE_RECT,
     },
     gtk_snapshot_push_clip,
     snapshot_push_instr_clip,
     1,
     },
    {
     "rounded-clip",
     5,
     0,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     },
     gtk_snapshot_push_rounded_clip,
     snapshot_push_instr_rounded_clip,
     1,
     },
    {
     "fill",
     2,
     0,
     {
     GSK_TYPE_PATH,
     GSK_TYPE_FILL_RULE,
     },
     gtk_snapshot_push_fill,
     snapshot_push_instr_fill,
     1,
     },
    {
     "stroke",
     2,
     0,
     {
     GSK_TYPE_PATH,
     GSK_TYPE_STROKE,
     },
     gtk_snapshot_push_stroke,
     snapshot_push_instr_stroke,
     1,
     },
    {
     "shadow",
     4,
     4,
     {
     GDK_TYPE_RGBA,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_push_shadow,
     snapshot_push_instr_shadow,
     1,
     },
    {
     "blend",
     1,
     0,
     {
     GSK_TYPE_BLEND_MODE,
     },
     gtk_snapshot_push_blend,
     snapshot_push_instr_blend,
     2,
     },
    {
     "mask",
     1,
     0,
     {
     GSK_TYPE_MASK_MODE,
     },
     gtk_snapshot_push_mask,
     snapshot_push_instr_mask,
     2,
     },
    {
     "copy",
     0,
     0,
     {},
     gtk_snapshot_push_copy,
     snapshot_push_instr_copy,
     1,
     },
    {
     "composite",
     1,
     0,
     {
     GSK_TYPE_PORTER_DUFF,
     },
     gtk_snapshot_push_composite,
     snapshot_push_instr_composite,
     2,
     },
    {
     "cross-fade",
     1,
     0,
     {
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_push_cross_fade,
     snapshot_push_instr_cross_fade,
     2,
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

static void
snapshot_transform_instr_transform (GtkSnapshot *snapshot,
                                    const GValue args[],
                                    const GValue rest[],
                                    guint        n_rest)
{
  gtk_snapshot_transform (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_transform_instr_transform_matrix (GtkSnapshot *snapshot,
                                           const GValue args[],
                                           const GValue rest[],
                                           guint        n_rest)
{
  gtk_snapshot_transform_matrix (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_transform_instr_translate (GtkSnapshot *snapshot,
                                    const GValue args[],
                                    const GValue rest[],
                                    guint        n_rest)
{
  gtk_snapshot_translate (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_transform_instr_translate_3d (GtkSnapshot *snapshot,
                                       const GValue args[],
                                       const GValue rest[],
                                       guint        n_rest)
{
  gtk_snapshot_translate_3d (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_transform_instr_rotate (GtkSnapshot *snapshot,
                                 const GValue args[],
                                 const GValue rest[],
                                 guint        n_rest)
{
  gtk_snapshot_rotate (
      snapshot,
      g_value_get_double (&args[0]));
}

static void
snapshot_transform_instr_rotate_3d (GtkSnapshot *snapshot,
                                    const GValue args[],
                                    const GValue rest[],
                                    guint        n_rest)
{
  gtk_snapshot_rotate_3d (
      snapshot,
      g_value_get_double (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_transform_instr_scale (GtkSnapshot *snapshot,
                                const GValue args[],
                                const GValue rest[],
                                guint        n_rest)
{
  gtk_snapshot_scale (
      snapshot,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]));
}

static void
snapshot_transform_instr_scale_3d (GtkSnapshot *snapshot,
                                   const GValue args[],
                                   const GValue rest[],
                                   guint        n_rest)
{
  gtk_snapshot_scale_3d (
      snapshot,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]));
}

static void
snapshot_transform_instr_perspective (GtkSnapshot *snapshot,
                                      const GValue args[],
                                      const GValue rest[],
                                      guint        n_rest)
{
  gtk_snapshot_scale_3d (
      snapshot,
      g_value_get_double (&args[0]),
      g_value_get_double (&args[1]),
      g_value_get_double (&args[2]));
}

gboolean
lookup_snapshot_transform_instr (const char    *lookup_name,
                                 SnapshotInstr *out)
{
  SnapshotInstr instrs[] = {
    {
     "transform",
     1,
     0,
     {
     GSK_TYPE_TRANSFORM,
     },
     gtk_snapshot_transform,
     snapshot_transform_instr_transform,
     },
    {
     "transform-matrix",
     1,
     0,
     {
     GRAPHENE_TYPE_MATRIX,
     },
     gtk_snapshot_transform_matrix,
     snapshot_transform_instr_transform_matrix,
     },
    {
     "translate",
     1,
     0,
     {
     GRAPHENE_TYPE_POINT,
     },
     gtk_snapshot_translate,
     snapshot_transform_instr_translate,
     },
    {
     "translate-3d",
     1,
     0,
     {
     GRAPHENE_TYPE_POINT3D,
     },
     gtk_snapshot_translate_3d,
     snapshot_transform_instr_translate_3d,
     },
    {
     "rotate",
     1,
     0,
     {
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_rotate,
     snapshot_transform_instr_rotate,
     },
    {
     "rotate-3d",
     2,
     0,
     {
     G_TYPE_DOUBLE,
     GRAPHENE_TYPE_VEC3,
     },
     gtk_snapshot_rotate_3d,
     snapshot_transform_instr_rotate_3d,
     },
    {
     "scale",
     2,
     0,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_scale,
     snapshot_transform_instr_scale,
     },
    {
     "scale-3d",
     3,
     0,
     {
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_scale_3d,
     snapshot_transform_instr_scale_3d,
     },
    {
     "perspective",
     1,
     0,
     {
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_perspective,
     snapshot_transform_instr_perspective,
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

static void
snapshot_append_instr_node (GtkSnapshot *snapshot,
                            const GValue args[],
                            const GValue rest[],
                            guint        n_rest)
{
  gtk_snapshot_append_node (
      snapshot,
      g_value_get_boxed (&args[0]));
}

static void
snapshot_append_instr_texture (GtkSnapshot *snapshot,
                               const GValue args[],
                               const GValue rest[],
                               guint        n_rest)
{
  gtk_snapshot_append_texture (
      snapshot,
      g_value_get_object (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_append_instr_scaled_texture (GtkSnapshot *snapshot,
                                      const GValue args[],
                                      const GValue rest[],
                                      guint        n_rest)
{
  gtk_snapshot_append_scaled_texture (
      snapshot,
      g_value_get_object (&args[0]),
      g_value_get_enum (&args[1]),
      g_value_get_boxed (&args[2]));
}

static void
snapshot_append_instr_color (GtkSnapshot *snapshot,
                             const GValue args[],
                             const GValue rest[],
                             guint        n_rest)
{
  gtk_snapshot_append_color (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_append_instr_linear_gradient (GtkSnapshot *snapshot,
                                       const GValue args[],
                                       const GValue rest[],
                                       guint        n_rest)
{
  guint        n_stops            = 0;
  GskColorStop stops[ARGBUF_SIZE] = { 0 };

  n_stops = MIN (n_rest / 2, G_N_ELEMENTS (stops));
  for (guint i = 0; i < n_stops; i++)
    {
      stops[i].offset = g_value_get_double (&rest[i * 2 + 0]);
      stops[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 2 + 1]);
    }

  gtk_snapshot_append_linear_gradient (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_boxed (&args[2]),
      stops,
      n_stops);
}

static void
snapshot_append_instr_repeating_linear_gradient (GtkSnapshot *snapshot,
                                                 const GValue args[],
                                                 const GValue rest[],
                                                 guint        n_rest)
{
  guint        n_stops            = 0;
  GskColorStop stops[ARGBUF_SIZE] = { 0 };

  n_stops = MIN (n_rest / 2, G_N_ELEMENTS (stops));
  for (guint i = 0; i < n_stops; i++)
    {
      stops[i].offset = g_value_get_double (&rest[i * 2 + 0]);
      stops[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 2 + 1]);
    }

  gtk_snapshot_append_repeating_linear_gradient (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_boxed (&args[2]),
      stops,
      n_stops);
}

static void
snapshot_append_instr_radial_gradient (GtkSnapshot *snapshot,
                                       const GValue args[],
                                       const GValue rest[],
                                       guint        n_rest)
{
  guint        n_stops            = 0;
  GskColorStop stops[ARGBUF_SIZE] = { 0 };

  n_stops = MIN (n_rest / 2, G_N_ELEMENTS (stops));
  for (guint i = 0; i < n_stops; i++)
    {
      stops[i].offset = g_value_get_double (&rest[i * 2 + 0]);
      stops[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 2 + 1]);
    }

  gtk_snapshot_append_radial_gradient (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]),
      g_value_get_double (&args[5]),
      stops,
      n_stops);
}

static void
snapshot_append_instr_repeating_radial_gradient (GtkSnapshot *snapshot,
                                                 const GValue args[],
                                                 const GValue rest[],
                                                 guint        n_rest)
{
  guint        n_stops            = 0;
  GskColorStop stops[ARGBUF_SIZE] = { 0 };

  n_stops = MIN (n_rest / 2, G_N_ELEMENTS (stops));
  for (guint i = 0; i < n_stops; i++)
    {
      stops[i].offset = g_value_get_double (&rest[i * 2 + 0]);
      stops[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 2 + 1]);
    }

  gtk_snapshot_append_repeating_radial_gradient (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_double (&args[2]),
      g_value_get_double (&args[3]),
      g_value_get_double (&args[4]),
      g_value_get_double (&args[5]),
      stops,
      n_stops);
}

static void
snapshot_append_instr_conic_gradient (GtkSnapshot *snapshot,
                                      const GValue args[],
                                      const GValue rest[],
                                      guint        n_rest)
{
  guint        n_stops            = 0;
  GskColorStop stops[ARGBUF_SIZE] = { 0 };

  n_stops = MIN (n_rest / 2, G_N_ELEMENTS (stops));
  for (guint i = 0; i < n_stops; i++)
    {
      stops[i].offset = g_value_get_double (&rest[i * 2 + 0]);
      stops[i].color  = *(GdkRGBA *) g_value_get_boxed (&rest[i * 2 + 1]);
    }

  gtk_snapshot_append_conic_gradient (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_double (&args[2]),
      stops,
      n_stops);
}

static void
snapshot_append_instr_border (GtkSnapshot *snapshot,
                              const GValue args[],
                              const GValue rest[],
                              guint        n_rest)
{
  GskRoundedRect rrect           = { 0 };
  float          border_width[4] = { 0 };
  GdkRGBA        border_color[4] = { 0 };

  rrect.bounds    = *(graphene_rect_t *) g_value_get_boxed (&args[0]);
  rrect.corner[0] = *(graphene_size_t *) g_value_get_boxed (&args[1]);
  rrect.corner[1] = *(graphene_size_t *) g_value_get_boxed (&args[2]);
  rrect.corner[2] = *(graphene_size_t *) g_value_get_boxed (&args[3]);
  rrect.corner[3] = *(graphene_size_t *) g_value_get_boxed (&args[4]);

  border_width[0] = g_value_get_double (&args[5]);
  border_width[1] = g_value_get_double (&args[6]);
  border_width[2] = g_value_get_double (&args[7]);
  border_width[3] = g_value_get_double (&args[8]);

  border_color[0] = *(GdkRGBA *) g_value_get_boxed (&args[9]);
  border_color[1] = *(GdkRGBA *) g_value_get_boxed (&args[10]);
  border_color[2] = *(GdkRGBA *) g_value_get_boxed (&args[11]);
  border_color[3] = *(GdkRGBA *) g_value_get_boxed (&args[12]);

  gtk_snapshot_append_border (
      snapshot,
      &rrect,
      border_width,
      border_color);
}

static void
snapshot_append_instr_inset_shadow (GtkSnapshot *snapshot,
                                    const GValue args[],
                                    const GValue rest[],
                                    guint        n_rest)
{
  GskRoundedRect rrect = { 0 };

  rrect.bounds    = *(graphene_rect_t *) g_value_get_boxed (&args[0]);
  rrect.corner[0] = *(graphene_size_t *) g_value_get_boxed (&args[1]);
  rrect.corner[1] = *(graphene_size_t *) g_value_get_boxed (&args[2]);
  rrect.corner[2] = *(graphene_size_t *) g_value_get_boxed (&args[3]);
  rrect.corner[3] = *(graphene_size_t *) g_value_get_boxed (&args[4]);

  gtk_snapshot_append_inset_shadow (
      snapshot,
      &rrect,
      g_value_get_boxed (&args[5]),
      g_value_get_double (&args[6]),
      g_value_get_double (&args[7]),
      g_value_get_double (&args[8]),
      g_value_get_double (&args[9]));
}

static void
snapshot_append_instr_outset_shadow (GtkSnapshot *snapshot,
                                     const GValue args[],
                                     const GValue rest[],
                                     guint        n_rest)
{
  GskRoundedRect rrect = { 0 };

  rrect.bounds    = *(graphene_rect_t *) g_value_get_boxed (&args[0]);
  rrect.corner[0] = *(graphene_size_t *) g_value_get_boxed (&args[1]);
  rrect.corner[1] = *(graphene_size_t *) g_value_get_boxed (&args[2]);
  rrect.corner[2] = *(graphene_size_t *) g_value_get_boxed (&args[3]);
  rrect.corner[3] = *(graphene_size_t *) g_value_get_boxed (&args[4]);

  gtk_snapshot_append_outset_shadow (
      snapshot,
      &rrect,
      g_value_get_boxed (&args[5]),
      g_value_get_double (&args[6]),
      g_value_get_double (&args[7]),
      g_value_get_double (&args[8]),
      g_value_get_double (&args[9]));
}

static void
snapshot_append_instr_layout (GtkSnapshot *snapshot,
                              const GValue args[],
                              const GValue rest[],
                              guint        n_rest)
{
  gtk_snapshot_append_layout (
      snapshot,
      g_value_get_object (&args[0]),
      g_value_get_boxed (&args[1]));
}

static void
snapshot_append_instr_fill (GtkSnapshot *snapshot,
                            const GValue args[],
                            const GValue rest[],
                            guint        n_rest)
{
  gtk_snapshot_append_fill (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_enum (&args[1]),
      g_value_get_boxed (&args[2]));
}

static void
snapshot_append_instr_stroke (GtkSnapshot *snapshot,
                              const GValue args[],
                              const GValue rest[],
                              guint        n_rest)
{
  gtk_snapshot_append_stroke (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_boxed (&args[1]),
      g_value_get_boxed (&args[2]));
}

static void
snapshot_append_instr_paste (GtkSnapshot *snapshot,
                             const GValue args[],
                             const GValue rest[],
                             guint        n_rest)
{
  gtk_snapshot_append_paste (
      snapshot,
      g_value_get_boxed (&args[0]),
      g_value_get_uint64 (&args[1]));
}

static void
snapshot_append_instr_paintable (GtkSnapshot *snapshot,
                                 const GValue args[],
                                 const GValue rest[],
                                 guint        n_rest)
{
  graphene_rect_t rect = { 0 };

  rect = *(graphene_rect_t *) g_value_get_boxed (&args[1]);

  gtk_snapshot_save (snapshot);
  gtk_snapshot_translate (snapshot, &rect.origin);
  gdk_paintable_snapshot (
      g_value_get_object (&args[0]),
      snapshot,
      rect.size.width,
      rect.size.height);
  gtk_snapshot_restore (snapshot);
}

gboolean
lookup_snapshot_append_instr (const char    *lookup_name,
                              SnapshotInstr *out)
{
  SnapshotInstr instrs[] = {
    {
     "node",
     1,
     0,
     {
     GSK_TYPE_RENDER_NODE,
     },
     gtk_snapshot_append_node,
     snapshot_append_instr_node,
     },
    {
     "texture",
     2,
     0,
     {
     GDK_TYPE_TEXTURE,
     GRAPHENE_TYPE_RECT,
     },
     gtk_snapshot_append_texture,
     snapshot_append_instr_texture,
     },
    {
     "scaled-texture",
     3,
     0,
     {
     GDK_TYPE_TEXTURE,
     GSK_TYPE_SCALING_FILTER,
     GRAPHENE_TYPE_RECT,
     },
     gtk_snapshot_append_scaled_texture,
     snapshot_append_instr_scaled_texture,
     },
    {
     "color",
     2,
     0,
     {
     GDK_TYPE_RGBA,
     GRAPHENE_TYPE_RECT,
     },
     gtk_snapshot_append_color,
     snapshot_append_instr_color,
     },
    {
     "linear-gradient",
     5,
     2,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_POINT,
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_linear_gradient,
     snapshot_append_instr_linear_gradient,
     },
    {
     "repeating-linear-gradient",
     5,
     2,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_POINT,
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_repeating_linear_gradient,
     snapshot_append_instr_repeating_linear_gradient,
     },
    {
     "radial-gradient",
     8,
     2,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_radial_gradient,
     snapshot_append_instr_radial_gradient,
     },
    {
     "repeating-radial-gradient",
     8,
     2,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_repeating_radial_gradient,
     snapshot_append_instr_repeating_radial_gradient,
     },
    {
     "conic-gradient",
     5,
     2,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_POINT,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_conic_gradient,
     snapshot_append_instr_conic_gradient,
     },
    {
     "border",
     13,
     0,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     GDK_TYPE_RGBA,
     GDK_TYPE_RGBA,
     GDK_TYPE_RGBA,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_border,
     snapshot_append_instr_border,
     },
    {
     "inset-shadow",
     10,
     0,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GDK_TYPE_RGBA,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_append_inset_shadow,
     snapshot_append_instr_inset_shadow,
     },
    {
     "outset-shadow",
     10,
     0,
     {
     GRAPHENE_TYPE_RECT,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GRAPHENE_TYPE_SIZE,
     GDK_TYPE_RGBA,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     G_TYPE_DOUBLE,
     },
     gtk_snapshot_append_outset_shadow,
     snapshot_append_instr_outset_shadow,
     },
    {
     "layout",
     2,
     0,
     {
     PANGO_TYPE_LAYOUT,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_layout,
     snapshot_append_instr_layout,
     },
    {
     "fill",
     3,
     0,
     {
     GSK_TYPE_PATH,
     GSK_TYPE_FILL_RULE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_fill,
     snapshot_append_instr_fill,
     },
    {
     "stroke",
     3,
     0,
     {
     GSK_TYPE_PATH,
     GSK_TYPE_STROKE,
     GDK_TYPE_RGBA,
     },
     gtk_snapshot_append_stroke,
     snapshot_append_instr_stroke,
     },
    {
     "paste",
     2,
     0,
     {
     GRAPHENE_TYPE_RECT,
     G_TYPE_UINT64,
     },
     gtk_snapshot_append_paste,
     snapshot_append_instr_paste,
     },
    {
     "paintable",
     2,
     0,
     {
     GDK_TYPE_PAINTABLE,
     GRAPHENE_TYPE_RECT,
     },
     gdk_paintable_snapshot,
     snapshot_append_instr_paintable,
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
