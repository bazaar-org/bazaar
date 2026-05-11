/* bge-markdown-render.h
 *
 * Copyright 2025 Eva M
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

#ifndef BGE_INSIDE
#error "Only <bge.h> can be included directly."
#endif

G_BEGIN_DECLS

#define BGE_TYPE_MARKDOWN_RENDER (bge_markdown_render_get_type ())
G_DECLARE_FINAL_TYPE (BgeMarkdownRender, bge_markdown_render, BGE, MARKDOWN_RENDER, GtkWidget)

GtkWidget *
bge_markdown_render_new (void);

const char *
bge_markdown_render_get_markdown (BgeMarkdownRender *self);

gboolean
bge_markdown_render_get_dark (BgeMarkdownRender *self);

void
bge_markdown_render_set_markdown (BgeMarkdownRender *self,
                                  const char        *markdown);

void
bge_markdown_render_set_dark (BgeMarkdownRender *self,
                              gboolean           dark);

G_END_DECLS

/* End of bge-markdown-render.h */
