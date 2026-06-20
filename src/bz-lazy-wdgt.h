/* bz-lazy-wdgt.h
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

#include <adwaita.h>
#include <bge.h>

G_BEGIN_DECLS

#define BZ_TYPE_LAZY_WDGT (bz_lazy_wdgt_get_type ())
G_DECLARE_FINAL_TYPE (BzLazyWdgt, bz_lazy_wdgt, BZ, LAZY_WDGT, AdwBin)

BzLazyWdgt *
bz_lazy_wdgt_new (void);

BgeWdgtSpec *
bz_lazy_wdgt_get_wdgt_spec (BzLazyWdgt *self);

const char *
bz_lazy_wdgt_get_wdgt_resource (BzLazyWdgt *self);

const char *
bz_lazy_wdgt_get_wdgt_state (BzLazyWdgt *self);

GObject *
bz_lazy_wdgt_get_wdgt_reference (BzLazyWdgt *self);

gboolean
bz_lazy_wdgt_get_activated (BzLazyWdgt *self);

void
bz_lazy_wdgt_set_wdgt_spec (BzLazyWdgt  *self,
                            BgeWdgtSpec *wdgt_spec);

void
bz_lazy_wdgt_set_wdgt_resource (BzLazyWdgt *self,
                                const char *wdgt_resource);

void
bz_lazy_wdgt_set_wdgt_state (BzLazyWdgt *self,
                             const char *wdgt_state);

void
bz_lazy_wdgt_set_wdgt_reference (BzLazyWdgt *self,
                                 GObject    *wdgt_reference);

void
bz_lazy_wdgt_set_wdgt_resource_take (BzLazyWdgt *self,
                                     char       *wdgt_resource);

#define bz_lazy_wdgt_set_wdgt_resource_take_printf(self, ...) bz_lazy_wdgt_set_wdgt_resource_take (self, g_strdup_printf (__VA_ARGS__))

void
bz_lazy_wdgt_set_wdgt_state_take (BzLazyWdgt *self,
                                  char       *wdgt_state);

#define bz_lazy_wdgt_set_wdgt_state_take_printf(self, ...) bz_lazy_wdgt_set_wdgt_state_take (self, g_strdup_printf (__VA_ARGS__))

G_END_DECLS

/* End of bz-lazy-wdgt.h */
