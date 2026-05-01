/* bge-wdgt-spec-private.h
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

#include "bge.h"

G_BEGIN_DECLS

typedef enum
{
  BGE_WDGT_SNAPSHOT_INSTR_APPEND = 0,
  BGE_WDGT_SNAPSHOT_INSTR_PUSH,
  BGE_WDGT_SNAPSHOT_INSTR_POP,
  BGE_WDGT_SNAPSHOT_INSTR_SAVE,
  BGE_WDGT_SNAPSHOT_INSTR_TRANSFORM,
  BGE_WDGT_SNAPSHOT_INSTR_RESTORE,
  BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD,
} BgeWdgtSnapshotInstrKind;

typedef enum
{
  BGE_WDGT_MEASURE_MINIMUM_WIDTH,
  BGE_WDGT_MEASURE_NATURAL_WIDTH,
  BGE_WDGT_MEASURE_MINIMUM_HEIGHT,
  BGE_WDGT_MEASURE_NATURAL_HEIGHT,
} BgeWdgtMeasureKind;

gboolean
bge_wdgt_spec_add_constant_source_value (BgeWdgtSpec  *self,
                                         const char   *name,
                                         const GValue *constant,
                                         GError      **error);

gboolean
bge_wdgt_spec_add_component_source_value (BgeWdgtSpec       *self,
                                          const char        *name,
                                          GType              type,
                                          const char *const *components,
                                          guint              n_components,
                                          GError           **error);

gboolean
bge_wdgt_spec_add_transform_source_value (BgeWdgtSpec       *self,
                                          const char        *name,
                                          const char        *next,
                                          const char        *instruction,
                                          const char *const *args,
                                          guint              n_args,
                                          GError           **error);

gboolean
bge_wdgt_spec_add_path_source_value (BgeWdgtSpec              *self,
                                     const char               *name,
                                     const char *const        *instructions,
                                     const char *const *const *argss,
                                     const guint              *n_argss,
                                     guint                     n_args,
                                     GError                  **error);

gboolean
bge_wdgt_spec_add_instance_source_value (BgeWdgtSpec *self,
                                         const char  *name,
                                         GType        type,
                                         GError     **error);

gboolean
bge_wdgt_spec_add_child_source_value (BgeWdgtSpec       *self,
                                      const char        *name,
                                      GType              type,
                                      const char        *parent_widget,
                                      const char        *builder_type,
                                      const char *const *css_classes,
                                      guint              n_css_classes,
                                      GError           **error);

gboolean
bge_wdgt_spec_add_cclosure_source_value (BgeWdgtSpec       *self,
                                         const char        *name,
                                         GType              type,
                                         GClosureMarshal    marshal,
                                         GCallback          func,
                                         const char *const *args,
                                         const GType       *arg_types,
                                         guint              n_args,
                                         gpointer           user_data,
                                         GDestroyNotify     destroy_user_data,
                                         GError           **error);

gboolean
bge_wdgt_spec_add_measure_for_size_source_value (BgeWdgtSpec *self,
                                                 const char  *name,
                                                 GError     **error);

gboolean
bge_wdgt_spec_add_widget_width_source_value (BgeWdgtSpec *self,
                                             const char  *name,
                                             GError     **error);

gboolean
bge_wdgt_spec_add_widget_height_source_value (BgeWdgtSpec *self,
                                              const char  *name,
                                              GError     **error);

gboolean
bge_wdgt_spec_add_tick_time_source_value (BgeWdgtSpec *self,
                                          const char  *name,
                                          GError     **error);

gboolean
bge_wdgt_spec_add_track_transition_source_value (BgeWdgtSpec *self,
                                                 const char  *name,
                                                 const char  *src,
                                                 const char  *damping_ratio,
                                                 const char  *mass,
                                                 const char  *stiffness,
                                                 GError     **error);

gboolean
bge_wdgt_spec_add_variable_value (BgeWdgtSpec *self,
                                  GType        type,
                                  const char  *name,
                                  GError     **error);

gboolean
bge_wdgt_spec_add_reference_object_value (BgeWdgtSpec *self,
                                          GType        type,
                                          const char  *name,
                                          GError     **error);

gboolean
bge_wdgt_spec_add_property_value (BgeWdgtSpec *self,
                                  const char  *name,
                                  const char  *object,
                                  const char  *property,
                                  GType       *type_out,
                                  GError     **error);

gboolean
bge_wdgt_spec_add_allocation_width_value (BgeWdgtSpec *self,
                                          const char  *name,
                                          const char  *child,
                                          GError     **error);

gboolean
bge_wdgt_spec_add_allocation_height_value (BgeWdgtSpec *self,
                                           const char  *name,
                                           const char  *child,
                                           GError     **error);

gboolean
bge_wdgt_spec_add_allocation_transform_value (BgeWdgtSpec *self,
                                              const char  *name,
                                              const char  *child,
                                              GError     **error);

gboolean
bge_wdgt_spec_add_measure_value (BgeWdgtSpec       *self,
                                 const char        *name,
                                 BgeWdgtMeasureKind kind,
                                 GError           **error);

gboolean
bge_wdgt_spec_add_state (BgeWdgtSpec *self,
                         const char  *name,
                         gboolean     default_state,
                         GError     **error);

gboolean
bge_wdgt_spec_set_value (BgeWdgtSpec *self,
                         const char  *state,
                         const char  *dest_value,
                         const char  *src_value,
                         GError     **error);

gboolean
bge_wdgt_spec_transition_value (BgeWdgtSpec *self,
                                const char  *state,
                                const char  *value,
                                const char  *seconds,
                                const char  *easing,
                                GError     **error);

gboolean
bge_wdgt_spec_transition_value_spring (BgeWdgtSpec *self,
                                       const char  *state,
                                       const char  *value,
                                       const char  *damping_ratio,
                                       const char  *mass,
                                       const char  *stiffness,
                                       GError     **error);

gboolean
bge_wdgt_spec_append_snapshot_instr (BgeWdgtSpec             *self,
                                     const char              *state,
                                     BgeWdgtSnapshotInstrKind kind,
                                     const char              *instr,
                                     const char *const       *args,
                                     guint                    n_args,
                                     guint                   *n_pops_out,
                                     GError                 **error);

gboolean
bge_wdgt_spec_push_foreach (BgeWdgtSpec *self,
                            const char  *model,
                            const char  *value_iterator,
                            const char  *index_iterator,
                            GType        iterator_type,
                            GError     **error);

gboolean
bge_wdgt_spec_pop_foreach (BgeWdgtSpec *self);

G_END_DECLS

/* End of bge-wdgt-spec-private.h */
