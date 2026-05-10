/* bge-wdgt-spec.c
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

#define G_LOG_DOMAIN "BGE::WDGT"

#include "../bge-animation-private.h"
#include "bge-marshalers.h"
#include "bge-wdgt-spec-private.h"
#include "bge.h"
#include "fmt/parser.h"
#include "graphene-gobject.h"
#include "lookup/lookup.h"
#include "util.h"

static void
_marshal_DIRECT__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data);

static void
_marshal_BOXED__ARGS_DIRECT (GClosure                *closure,
                             GValue                  *return_value,
                             guint                    n_param_values,
                             const GValue            *param_values,
                             gpointer invocation_hint G_GNUC_UNUSED,
                             gpointer                 marshal_data);

typedef enum
{
  VALUE_ALLOCATION_HEIGHT = 0,
  VALUE_ALLOCATION_TRANSFORM,
  VALUE_ALLOCATION_WIDTH,
  VALUE_CHILD,
  VALUE_CLOSURE,
  VALUE_COERCION,
  VALUE_COMPONENT,
  VALUE_CONSTANT,
  VALUE_ITERATOR,
  VALUE_MEASURE_FOR_SIZE,
  VALUE_MEASURE_MINIMUM_HEIGHT,
  VALUE_MEASURE_MINIMUM_WIDTH,
  VALUE_MEASURE_NATURAL_HEIGHT,
  VALUE_MEASURE_NATURAL_WIDTH,
  VALUE_OBJECT,
  VALUE_PATH,
  VALUE_PROPERTY,
  VALUE_REFERENCE_OBJECT,
  VALUE_TICK_TIME,
  VALUE_TRACK_TRANSITION,
  VALUE_TRANSFORM,
  VALUE_VARIABLE,
  VALUE_WIDGET_HEIGHT,
  VALUE_WIDGET_WIDTH,
} ValueKind;

typedef enum
{
  TRANSITION_EASE = 0,
  TRANSITION_SPRING,
} TransitionKind;

typedef struct _ForeachData ForeachData;
static void
deinit_value (gpointer ptr);
BGE_DEFINE_DATA (
    value,
    Value,
    {
      char     *name;
      GType     type;
      ValueKind kind;
      union
      {
        struct
        {
          GValue value;
        } constant;
        struct
        {
          GPtrArray *params;
        } component;
        struct
        {
          ForeachData *context;
        } iterator;
        struct
        {
          TransformCallFunc func;
          ValueData        *next;
          GPtrArray        *args;
        } transform;
        struct
        {
          GPtrArray *funcs;
          GPtrArray *argss;
        } path;
        struct
        {
          GPtrArray      *args;
          GClosureMarshal marshal;
          GCallback       func;
          gpointer        user_data;
          GDestroyNotify  destroy_user_data;
        } closure;
        struct
        {
          ValueData *src;
          struct
          {
            ValueData *damping_ratio;
            ValueData *mass;
            ValueData *stiffness;
          } spring;
        } track_transition;
        struct
        {
          ValueData *value;
        } coercion;
        struct
        {
          ValueData  *object;
          char       *prop_name;
          GParamFlags pspec_flags;
        } property;
        struct
        {
          ValueData *parent_widget;
          char      *builder_type;
          GPtrArray *css_classes;
        } child;
        struct
        {
          ValueData *widget;
        } allocation;
      };
    },
    deinit_value (self);)

BGE_DEFINE_DATA (
    foreach,
    Foreach,
    {
      ValueData   *model;
      GHashTable  *values;
      ForeachData *parent;
    },
    BGE_RELEASE_DATA (model, value_data_unref);
    BGE_RELEASE_DATA (values, g_hash_table_unref);
    BGE_RELEASE_DATA (parent, foreach_data_unref))

static void
deinit_value (gpointer ptr)
{
  ValueData *value = ptr;

  g_clear_pointer (&value->name, g_free);
  switch (value->kind)
    {
    case VALUE_CONSTANT:
      g_value_unset (&value->constant.value);
      break;
    case VALUE_ITERATOR:
      g_clear_pointer (&value->iterator.context, foreach_data_unref);
      break;
    case VALUE_COMPONENT:
      g_clear_pointer (&value->component.params, g_ptr_array_unref);
      break;
    case VALUE_TRANSFORM:
      g_clear_pointer (&value->transform.next, value_data_unref);
      g_clear_pointer (&value->transform.args, g_ptr_array_unref);
      break;
    case VALUE_PATH:
      g_clear_pointer (&value->path.funcs, g_ptr_array_unref);
      g_clear_pointer (&value->path.argss, g_ptr_array_unref);
      break;
    case VALUE_CLOSURE:
      g_clear_pointer (&value->closure.args, g_ptr_array_unref);
      if (value->closure.destroy_user_data != NULL)
        g_clear_pointer (&value->closure.user_data, value->closure.destroy_user_data);
      break;
    case VALUE_COERCION:
      g_clear_pointer (&value->coercion.value, value_data_unref);
      break;
    case VALUE_TRACK_TRANSITION:
      g_clear_pointer (&value->track_transition.src, value_data_unref);
      g_clear_pointer (&value->track_transition.spring.damping_ratio, value_data_unref);
      g_clear_pointer (&value->track_transition.spring.mass, value_data_unref);
      g_clear_pointer (&value->track_transition.spring.stiffness, value_data_unref);
      break;
    case VALUE_PROPERTY:
      g_clear_pointer (&value->property.object, value_data_unref);
      g_clear_pointer (&value->property.prop_name, g_free);
      break;
    case VALUE_CHILD:
      g_clear_pointer (&value->child.parent_widget, value_data_unref);
      g_clear_pointer (&value->child.builder_type, g_free);
      g_clear_pointer (&value->child.css_classes, g_ptr_array_unref);
      break;
    case VALUE_ALLOCATION_WIDTH:
    case VALUE_ALLOCATION_HEIGHT:
    case VALUE_ALLOCATION_TRANSFORM:
      g_clear_pointer (&value->allocation.widget, value_data_unref);
      break;
    case VALUE_MEASURE_FOR_SIZE:
    case VALUE_MEASURE_MINIMUM_HEIGHT:
    case VALUE_MEASURE_MINIMUM_WIDTH:
    case VALUE_MEASURE_NATURAL_HEIGHT:
    case VALUE_MEASURE_NATURAL_WIDTH:
    case VALUE_OBJECT:
    case VALUE_REFERENCE_OBJECT:
    case VALUE_TICK_TIME:
    case VALUE_VARIABLE:
    case VALUE_WIDGET_HEIGHT:
    case VALUE_WIDGET_WIDTH:
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
deinit_transition (gpointer ptr);
BGE_DEFINE_DATA (
    transition,
    Transition,
    {
      TransitionKind kind;
      union
      {
        struct
        {
          ValueData *seconds;
          ValueData *easing;
        } ease;
        struct
        {
          ValueData *damping_ratio;
          ValueData *mass;
          ValueData *stiffness;
        } spring;
      };
    },
    deinit_transition (self);)

static void
deinit_transition (gpointer ptr)
{
  TransitionData *transition = ptr;

  switch (transition->kind)
    {
    case TRANSITION_EASE:
      g_clear_pointer (&transition->ease.seconds, value_data_unref);
      g_clear_pointer (&transition->ease.easing, value_data_unref);
      break;
    case TRANSITION_SPRING:
      g_clear_pointer (&transition->spring.damping_ratio, value_data_unref);
      g_clear_pointer (&transition->spring.mass, value_data_unref);
      g_clear_pointer (&transition->spring.stiffness, value_data_unref);
      break;
    default:
      g_assert_not_reached ();
    }
}

BGE_DEFINE_DATA (
    snapshot_call,
    SnapshotCall,
    {
      BgeWdgtSnapshotInstrKind kind;
      SnapshotCallFunc         func;
      GPtrArray               *args;
      GPtrArray               *rest;
      ValueData               *child;
      ForeachData             *foreach_context;
    },
    BGE_RELEASE_DATA (args, g_ptr_array_unref);
    BGE_RELEASE_DATA (rest, g_ptr_array_unref);
    BGE_RELEASE_DATA (child, value_data_unref);
    BGE_RELEASE_DATA (foreach_context, foreach_data_unref))

BGE_DEFINE_DATA (
    snapshot,
    Snapshot,
    {
      GPtrArray *calls;
    },
    BGE_RELEASE_DATA (calls, g_ptr_array_unref))

BGE_DEFINE_DATA (
    state,
    State,
    {
      char         *name;
      GHashTable   *setters;
      GHashTable   *transitions;
      SnapshotData *snapshot;

      GHashTable *expressions;
      GPtrArray  *snapshot_deps;
      GHashTable *values_need_tick;
      gboolean    needs_tick;
    },
    BGE_RELEASE_DATA (name, g_free);
    BGE_RELEASE_DATA (setters, g_hash_table_unref);
    BGE_RELEASE_DATA (transitions, g_hash_table_unref);
    BGE_RELEASE_DATA (snapshot, snapshot_data_unref);
    BGE_RELEASE_DATA (expressions, g_hash_table_unref);
    BGE_RELEASE_DATA (snapshot_deps, g_ptr_array_unref);
    BGE_RELEASE_DATA (values_need_tick, g_hash_table_unref););

/* --------------------------- */
/* Spec Builder Implementation */
/* --------------------------- */

struct _BgeWdgtSpec
{
  GObject parent_instance;

  char *name;

  GHashTable *values;
  GPtrArray  *values_stack;
  GPtrArray  *foreaches;
  GPtrArray  *foreach_stack;
  GPtrArray  *anon_values;
  GPtrArray  *track_transition_values;
  GHashTable *states;
  GPtrArray  *children;
  GPtrArray  *nonchildren;

  StateData *init_state;
  StateData *default_state;

  ValueData *reference;

  gboolean ready;
};

G_DEFINE_FINAL_TYPE (BgeWdgtSpec, bge_wdgt_spec, G_TYPE_OBJECT);

enum
{
  PROP_0,

  PROP_NAME,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static gboolean
check_can_coerce_type (GType dest,
                       GType src);

static void
coerce_value (const GValue *in,
              GType         dest_type,
              GValue       *out);

static ValueData *
wrap_coerce_value (BgeWdgtSpec *self,
                   ValueData   *value,
                   GType        dest_type);

static void
ensure_state_snapshot (StateData *state);

static void
bge_wdgt_spec_dispose (GObject *object)
{
  BgeWdgtSpec *self = BGE_WDGT_SPEC (object);

  g_clear_pointer (&self->name, g_free);

  g_clear_pointer (&self->values, g_hash_table_unref);
  g_clear_pointer (&self->values_stack, g_ptr_array_unref);
  g_clear_pointer (&self->foreaches, g_ptr_array_unref);
  g_clear_pointer (&self->foreach_stack, g_ptr_array_unref);
  g_clear_pointer (&self->anon_values, g_ptr_array_unref);
  g_clear_pointer (&self->track_transition_values, g_ptr_array_unref);
  g_clear_pointer (&self->states, g_hash_table_unref);
  g_clear_pointer (&self->children, g_ptr_array_unref);
  g_clear_pointer (&self->nonchildren, g_ptr_array_unref);

  g_clear_pointer (&self->init_state, state_data_unref);
  g_clear_pointer (&self->default_state, state_data_unref);

  g_clear_pointer (&self->reference, value_data_unref);

  G_OBJECT_CLASS (bge_wdgt_spec_parent_class)->dispose (object);
}

static void
bge_wdgt_spec_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BgeWdgtSpec *self = BGE_WDGT_SPEC (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, bge_wdgt_spec_get_name (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_spec_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BgeWdgtSpec *self = BGE_WDGT_SPEC (object);

  switch (prop_id)
    {
    case PROP_NAME:
      bge_wdgt_spec_set_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_spec_class_init (BgeWdgtSpecClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bge_wdgt_spec_set_property;
  object_class->get_property = bge_wdgt_spec_get_property;
  object_class->dispose      = bge_wdgt_spec_dispose;

  props[PROP_NAME] =
      g_param_spec_string (
          "name",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (BGE_TYPE_EASING);

  /* Make sure these names are available for the parser */
  g_type_ensure (GRAPHENE_TYPE_POINT);
  g_type_ensure (GRAPHENE_TYPE_POINT3D);
  g_type_ensure (GRAPHENE_TYPE_SIZE);
  g_type_ensure (GRAPHENE_TYPE_RECT);
  g_type_ensure (GRAPHENE_TYPE_VEC2);
  g_type_ensure (GRAPHENE_TYPE_VEC3);
  g_type_ensure (GRAPHENE_TYPE_VEC4);
  g_type_ensure (GRAPHENE_TYPE_QUAD);
  g_type_ensure (GRAPHENE_TYPE_QUATERNION);
  g_type_ensure (GRAPHENE_TYPE_MATRIX);
  g_type_ensure (GRAPHENE_TYPE_PLANE);
  g_type_ensure (GRAPHENE_TYPE_FRUSTUM);
  g_type_ensure (GRAPHENE_TYPE_SPHERE);
  g_type_ensure (GRAPHENE_TYPE_BOX);
  g_type_ensure (GRAPHENE_TYPE_TRIANGLE);
  g_type_ensure (GRAPHENE_TYPE_EULER);
  g_type_ensure (GRAPHENE_TYPE_RAY);

  g_type_ensure (GSK_TYPE_PATH);
  g_type_ensure (GSK_TYPE_PATH_POINT);
  g_type_ensure (GSK_TYPE_PATH_MEASURE);
  g_type_ensure (GSK_TYPE_TRANSFORM);
  g_type_ensure (GSK_TYPE_FILL_RULE);
  g_type_ensure (GSK_TYPE_LINE_CAP);
  g_type_ensure (GSK_TYPE_LINE_JOIN);

  /* Ensure GTK names are available for the parser */
  g_type_ensure (gtk_at_context_get_type ());
  g_type_ensure (gtk_about_dialog_get_type ());
  g_type_ensure (gtk_accessible_get_type ());
  g_type_ensure (gtk_accessible_announcement_priority_get_type ());
  g_type_ensure (gtk_accessible_autocomplete_get_type ());
  g_type_ensure (gtk_accessible_invalid_state_get_type ());
  g_type_ensure (gtk_accessible_list_get_type ());
  g_type_ensure (gtk_accessible_platform_state_get_type ());
  g_type_ensure (gtk_accessible_property_get_type ());
  g_type_ensure (gtk_accessible_range_get_type ());
  g_type_ensure (gtk_accessible_relation_get_type ());
  g_type_ensure (gtk_accessible_role_get_type ());
  g_type_ensure (gtk_accessible_sort_get_type ());
  g_type_ensure (gtk_accessible_state_get_type ());
  g_type_ensure (gtk_accessible_text_get_type ());
  g_type_ensure (gtk_accessible_text_content_change_get_type ());
  g_type_ensure (gtk_accessible_text_granularity_get_type ());
  g_type_ensure (gtk_accessible_tristate_get_type ());
  g_type_ensure (gtk_action_bar_get_type ());
  g_type_ensure (gtk_actionable_get_type ());
  g_type_ensure (gtk_activate_action_get_type ());
  g_type_ensure (gtk_adjustment_get_type ());
  g_type_ensure (gtk_alert_dialog_get_type ());
  g_type_ensure (gtk_align_get_type ());
  g_type_ensure (gtk_alternative_trigger_get_type ());
  g_type_ensure (gtk_any_filter_get_type ());
  g_type_ensure (gtk_app_chooser_get_type ());
  g_type_ensure (gtk_app_chooser_button_get_type ());
  g_type_ensure (gtk_app_chooser_dialog_get_type ());
  g_type_ensure (gtk_app_chooser_widget_get_type ());
  g_type_ensure (gtk_application_get_type ());
  g_type_ensure (gtk_application_inhibit_flags_get_type ());
  g_type_ensure (gtk_application_window_get_type ());
  g_type_ensure (gtk_arrow_type_get_type ());
  g_type_ensure (gtk_aspect_frame_get_type ());
  g_type_ensure (gtk_assistant_get_type ());
  g_type_ensure (gtk_assistant_page_get_type ());
  g_type_ensure (gtk_assistant_page_type_get_type ());
  g_type_ensure (gtk_baseline_position_get_type ());
  g_type_ensure (gtk_bin_layout_get_type ());
  g_type_ensure (gtk_bitset_get_type ());
  g_type_ensure (gtk_bitset_iter_get_type ());
  g_type_ensure (gtk_bookmark_list_get_type ());
  g_type_ensure (gtk_bool_filter_get_type ());
  g_type_ensure (gtk_border_get_type ());
  g_type_ensure (gtk_border_style_get_type ());
  g_type_ensure (gtk_box_get_type ());
  g_type_ensure (gtk_box_layout_get_type ());
  g_type_ensure (gtk_buildable_get_type ());
  g_type_ensure (gtk_builder_get_type ());
  g_type_ensure (gtk_builder_cscope_get_type ());
  g_type_ensure (gtk_builder_closure_flags_get_type ());
  g_type_ensure (gtk_builder_error_get_type ());
  g_type_ensure (gtk_builder_list_item_factory_get_type ());
  g_type_ensure (gtk_builder_scope_get_type ());
  g_type_ensure (gtk_button_get_type ());
  g_type_ensure (gtk_buttons_type_get_type ());
  g_type_ensure (gtk_cclosure_expression_get_type ());
  g_type_ensure (gtk_calendar_get_type ());
  g_type_ensure (gtk_callback_action_get_type ());
  g_type_ensure (gtk_cell_area_get_type ());
  g_type_ensure (gtk_cell_area_box_get_type ());
  g_type_ensure (gtk_cell_area_context_get_type ());
  g_type_ensure (gtk_cell_editable_get_type ());
  g_type_ensure (gtk_cell_layout_get_type ());
  g_type_ensure (gtk_cell_renderer_get_type ());
  g_type_ensure (gtk_cell_renderer_accel_get_type ());
  g_type_ensure (gtk_cell_renderer_accel_mode_get_type ());
  g_type_ensure (gtk_cell_renderer_combo_get_type ());
  g_type_ensure (gtk_cell_renderer_mode_get_type ());
  g_type_ensure (gtk_cell_renderer_pixbuf_get_type ());
  g_type_ensure (gtk_cell_renderer_progress_get_type ());
  g_type_ensure (gtk_cell_renderer_spin_get_type ());
  g_type_ensure (gtk_cell_renderer_spinner_get_type ());
  g_type_ensure (gtk_cell_renderer_state_get_type ());
  g_type_ensure (gtk_cell_renderer_text_get_type ());
  g_type_ensure (gtk_cell_renderer_toggle_get_type ());
  g_type_ensure (gtk_cell_view_get_type ());
  g_type_ensure (gtk_center_box_get_type ());
  g_type_ensure (gtk_center_layout_get_type ());
  g_type_ensure (gtk_check_button_get_type ());
  g_type_ensure (gtk_closure_expression_get_type ());
  g_type_ensure (gtk_collation_get_type ());
  g_type_ensure (gtk_color_button_get_type ());
  g_type_ensure (gtk_color_chooser_get_type ());
  g_type_ensure (gtk_color_chooser_dialog_get_type ());
  g_type_ensure (gtk_color_chooser_widget_get_type ());
  g_type_ensure (gtk_color_dialog_get_type ());
  g_type_ensure (gtk_color_dialog_button_get_type ());
  g_type_ensure (gtk_column_view_get_type ());
  g_type_ensure (gtk_column_view_cell_get_type ());
  g_type_ensure (gtk_column_view_column_get_type ());
  g_type_ensure (gtk_column_view_row_get_type ());
  g_type_ensure (gtk_column_view_sorter_get_type ());
  g_type_ensure (gtk_combo_box_get_type ());
  g_type_ensure (gtk_combo_box_text_get_type ());
  g_type_ensure (gtk_constant_expression_get_type ());
  g_type_ensure (gtk_constraint_get_type ());
  g_type_ensure (gtk_constraint_attribute_get_type ());
  g_type_ensure (gtk_constraint_guide_get_type ());
  g_type_ensure (gtk_constraint_layout_get_type ());
  g_type_ensure (gtk_constraint_layout_child_get_type ());
  g_type_ensure (gtk_constraint_relation_get_type ());
  g_type_ensure (gtk_constraint_strength_get_type ());
  g_type_ensure (gtk_constraint_target_get_type ());
  g_type_ensure (gtk_constraint_vfl_parser_error_get_type ());
  g_type_ensure (gtk_content_fit_get_type ());
  g_type_ensure (gtk_corner_type_get_type ());
  g_type_ensure (gtk_css_provider_get_type ());
  g_type_ensure (gtk_css_section_get_type ());
  g_type_ensure (gtk_custom_filter_get_type ());
  g_type_ensure (gtk_custom_layout_get_type ());
  g_type_ensure (gtk_custom_sorter_get_type ());
  g_type_ensure (gtk_debug_flags_get_type ());
  g_type_ensure (gtk_delete_type_get_type ());
  g_type_ensure (gtk_dialog_get_type ());
  g_type_ensure (gtk_dialog_error_get_type ());
  g_type_ensure (gtk_dialog_flags_get_type ());
  g_type_ensure (gtk_direction_type_get_type ());
  g_type_ensure (gtk_directory_list_get_type ());
  g_type_ensure (gtk_drag_icon_get_type ());
  g_type_ensure (gtk_drag_source_get_type ());
  g_type_ensure (gtk_drawing_area_get_type ());
  g_type_ensure (gtk_drop_controller_motion_get_type ());
  g_type_ensure (gtk_drop_down_get_type ());
  g_type_ensure (gtk_drop_target_get_type ());
  g_type_ensure (gtk_drop_target_async_get_type ());
  g_type_ensure (gtk_editable_get_type ());
  g_type_ensure (gtk_editable_label_get_type ());
  g_type_ensure (gtk_editable_properties_get_type ());
  g_type_ensure (gtk_emoji_chooser_get_type ());
  g_type_ensure (gtk_entry_get_type ());
  g_type_ensure (gtk_entry_buffer_get_type ());
  g_type_ensure (gtk_entry_completion_get_type ());
  g_type_ensure (gtk_entry_icon_position_get_type ());
  g_type_ensure (gtk_event_controller_get_type ());
  g_type_ensure (gtk_event_controller_focus_get_type ());
  g_type_ensure (gtk_event_controller_key_get_type ());
  g_type_ensure (gtk_event_controller_legacy_get_type ());
  g_type_ensure (gtk_event_controller_motion_get_type ());
  g_type_ensure (gtk_event_controller_scroll_get_type ());
  g_type_ensure (gtk_event_controller_scroll_flags_get_type ());
  g_type_ensure (gtk_event_sequence_state_get_type ());
  g_type_ensure (gtk_every_filter_get_type ());
  g_type_ensure (gtk_expander_get_type ());
  g_type_ensure (gtk_expression_get_type ());
  g_type_ensure (gtk_expression_watch_get_type ());
  g_type_ensure (gtk_file_chooser_get_type ());
  g_type_ensure (gtk_file_chooser_action_get_type ());
  g_type_ensure (gtk_file_chooser_dialog_get_type ());
  g_type_ensure (gtk_file_chooser_error_get_type ());
  g_type_ensure (gtk_file_chooser_native_get_type ());
  g_type_ensure (gtk_file_chooser_widget_get_type ());
  g_type_ensure (gtk_file_dialog_get_type ());
  g_type_ensure (gtk_file_filter_get_type ());
  g_type_ensure (gtk_file_launcher_get_type ());
  g_type_ensure (gtk_filter_get_type ());
  g_type_ensure (gtk_filter_change_get_type ());
  g_type_ensure (gtk_filter_list_model_get_type ());
  g_type_ensure (gtk_filter_match_get_type ());
  g_type_ensure (gtk_fixed_get_type ());
  g_type_ensure (gtk_fixed_layout_get_type ());
  g_type_ensure (gtk_fixed_layout_child_get_type ());
  g_type_ensure (gtk_flatten_list_model_get_type ());
  g_type_ensure (gtk_flow_box_get_type ());
  g_type_ensure (gtk_flow_box_child_get_type ());
  g_type_ensure (gtk_font_button_get_type ());
  g_type_ensure (gtk_font_chooser_get_type ());
  g_type_ensure (gtk_font_chooser_dialog_get_type ());
  g_type_ensure (gtk_font_chooser_level_get_type ());
  g_type_ensure (gtk_font_chooser_widget_get_type ());
  g_type_ensure (gtk_font_dialog_get_type ());
  g_type_ensure (gtk_font_dialog_button_get_type ());
  g_type_ensure (gtk_font_level_get_type ());
  g_type_ensure (gtk_font_rendering_get_type ());
  g_type_ensure (gtk_frame_get_type ());
  g_type_ensure (gtk_gl_area_get_type ());
  g_type_ensure (gtk_gesture_get_type ());
  g_type_ensure (gtk_gesture_click_get_type ());
  g_type_ensure (gtk_gesture_drag_get_type ());
  g_type_ensure (gtk_gesture_long_press_get_type ());
  g_type_ensure (gtk_gesture_pan_get_type ());
  g_type_ensure (gtk_gesture_rotate_get_type ());
  g_type_ensure (gtk_gesture_single_get_type ());
  g_type_ensure (gtk_gesture_stylus_get_type ());
  g_type_ensure (gtk_gesture_swipe_get_type ());
  g_type_ensure (gtk_gesture_zoom_get_type ());
  g_type_ensure (gtk_graphics_offload_get_type ());
  g_type_ensure (gtk_graphics_offload_enabled_get_type ());
  g_type_ensure (gtk_grid_get_type ());
  g_type_ensure (gtk_grid_layout_get_type ());
  g_type_ensure (gtk_grid_layout_child_get_type ());
  g_type_ensure (gtk_grid_view_get_type ());
  g_type_ensure (gtk_header_bar_get_type ());
  g_type_ensure (gtk_im_context_get_type ());
  g_type_ensure (gtk_im_context_simple_get_type ());
  g_type_ensure (gtk_im_multicontext_get_type ());
  g_type_ensure (gtk_icon_lookup_flags_get_type ());
  g_type_ensure (gtk_icon_paintable_get_type ());
  g_type_ensure (gtk_icon_size_get_type ());
  g_type_ensure (gtk_icon_theme_get_type ());
  g_type_ensure (gtk_icon_theme_error_get_type ());
  g_type_ensure (gtk_icon_view_get_type ());
  g_type_ensure (gtk_icon_view_drop_position_get_type ());
  g_type_ensure (gtk_image_get_type ());
  g_type_ensure (gtk_image_type_get_type ());
  g_type_ensure (gtk_info_bar_get_type ());
  g_type_ensure (gtk_input_hints_get_type ());
  g_type_ensure (gtk_input_purpose_get_type ());
  g_type_ensure (gtk_inscription_get_type ());
  g_type_ensure (gtk_inscription_overflow_get_type ());
  g_type_ensure (gtk_interface_color_scheme_get_type ());
  g_type_ensure (gtk_interface_contrast_get_type ());
  g_type_ensure (gtk_justification_get_type ());
  g_type_ensure (gtk_keyval_trigger_get_type ());
  g_type_ensure (gtk_label_get_type ());
  g_type_ensure (gtk_layout_child_get_type ());
  g_type_ensure (gtk_layout_manager_get_type ());
  g_type_ensure (gtk_level_bar_get_type ());
  g_type_ensure (gtk_level_bar_mode_get_type ());
  g_type_ensure (gtk_license_get_type ());
  g_type_ensure (gtk_link_button_get_type ());
  g_type_ensure (gtk_list_base_get_type ());
  g_type_ensure (gtk_list_box_get_type ());
  g_type_ensure (gtk_list_box_row_get_type ());
  g_type_ensure (gtk_list_header_get_type ());
  g_type_ensure (gtk_list_item_get_type ());
  g_type_ensure (gtk_list_item_factory_get_type ());
  g_type_ensure (gtk_list_scroll_flags_get_type ());
  g_type_ensure (gtk_list_store_get_type ());
  g_type_ensure (gtk_list_tab_behavior_get_type ());
  g_type_ensure (gtk_list_view_get_type ());
  g_type_ensure (gtk_lock_button_get_type ());
  g_type_ensure (gtk_map_list_model_get_type ());
  g_type_ensure (gtk_media_controls_get_type ());
  g_type_ensure (gtk_media_file_get_type ());
  g_type_ensure (gtk_media_stream_get_type ());
  g_type_ensure (gtk_menu_button_get_type ());
  g_type_ensure (gtk_message_dialog_get_type ());
  g_type_ensure (gtk_message_type_get_type ());
  g_type_ensure (gtk_mnemonic_action_get_type ());
  g_type_ensure (gtk_mnemonic_trigger_get_type ());
  g_type_ensure (gtk_mount_operation_get_type ());
  g_type_ensure (gtk_movement_step_get_type ());
  g_type_ensure (gtk_multi_filter_get_type ());
  g_type_ensure (gtk_multi_selection_get_type ());
  g_type_ensure (gtk_multi_sorter_get_type ());
  g_type_ensure (gtk_named_action_get_type ());
  g_type_ensure (gtk_native_get_type ());
  g_type_ensure (gtk_native_dialog_get_type ());
  g_type_ensure (gtk_natural_wrap_mode_get_type ());
  g_type_ensure (gtk_never_trigger_get_type ());
  g_type_ensure (gtk_no_selection_get_type ());
  g_type_ensure (gtk_notebook_get_type ());
  g_type_ensure (gtk_notebook_page_get_type ());
  g_type_ensure (gtk_notebook_tab_get_type ());
  g_type_ensure (gtk_nothing_action_get_type ());
  g_type_ensure (gtk_number_up_layout_get_type ());
  g_type_ensure (gtk_numeric_sorter_get_type ());
  g_type_ensure (gtk_object_expression_get_type ());
  g_type_ensure (gtk_ordering_get_type ());
  g_type_ensure (gtk_orientable_get_type ());
  g_type_ensure (gtk_orientation_get_type ());
  g_type_ensure (gtk_overflow_get_type ());
  g_type_ensure (gtk_overlay_get_type ());
  g_type_ensure (gtk_overlay_layout_get_type ());
  g_type_ensure (gtk_overlay_layout_child_get_type ());
  g_type_ensure (gtk_pack_type_get_type ());
  g_type_ensure (gtk_pad_action_type_get_type ());
  g_type_ensure (gtk_pad_controller_get_type ());
  g_type_ensure (gtk_page_orientation_get_type ());
  g_type_ensure (gtk_page_set_get_type ());
  g_type_ensure (gtk_page_setup_get_type ());
  g_type_ensure (gtk_pan_direction_get_type ());
  g_type_ensure (gtk_paned_get_type ());
  g_type_ensure (gtk_paper_size_get_type ());
  g_type_ensure (gtk_param_expression_get_type ());
  g_type_ensure (gtk_password_entry_get_type ());
  g_type_ensure (gtk_password_entry_buffer_get_type ());
  g_type_ensure (gtk_pick_flags_get_type ());
  g_type_ensure (gtk_picture_get_type ());
  g_type_ensure (gtk_policy_type_get_type ());
  g_type_ensure (gtk_popover_get_type ());
  g_type_ensure (gtk_popover_menu_get_type ());
  g_type_ensure (gtk_popover_menu_bar_get_type ());
  g_type_ensure (gtk_popover_menu_flags_get_type ());
  g_type_ensure (gtk_position_type_get_type ());
  g_type_ensure (gtk_print_context_get_type ());
  g_type_ensure (gtk_print_dialog_get_type ());
  g_type_ensure (gtk_print_duplex_get_type ());
  g_type_ensure (gtk_print_error_get_type ());
  g_type_ensure (gtk_print_operation_get_type ());
  g_type_ensure (gtk_print_operation_action_get_type ());
  g_type_ensure (gtk_print_operation_preview_get_type ());
  g_type_ensure (gtk_print_operation_result_get_type ());
  g_type_ensure (gtk_print_pages_get_type ());
  g_type_ensure (gtk_print_quality_get_type ());
  g_type_ensure (gtk_print_settings_get_type ());
  g_type_ensure (gtk_print_setup_get_type ());
  g_type_ensure (gtk_print_status_get_type ());
  g_type_ensure (gtk_progress_bar_get_type ());
  g_type_ensure (gtk_propagation_limit_get_type ());
  g_type_ensure (gtk_propagation_phase_get_type ());
  g_type_ensure (gtk_property_expression_get_type ());
  g_type_ensure (gtk_range_get_type ());
  g_type_ensure (gtk_recent_info_get_type ());
  g_type_ensure (gtk_recent_manager_get_type ());
  g_type_ensure (gtk_recent_manager_error_get_type ());
  g_type_ensure (gtk_requisition_get_type ());
  g_type_ensure (gtk_response_type_get_type ());
  g_type_ensure (gtk_revealer_get_type ());
  g_type_ensure (gtk_revealer_transition_type_get_type ());
  g_type_ensure (gtk_root_get_type ());
  g_type_ensure (gtk_scale_get_type ());
  g_type_ensure (gtk_scale_button_get_type ());
  g_type_ensure (gtk_scroll_info_get_type ());
  g_type_ensure (gtk_scroll_step_get_type ());
  g_type_ensure (gtk_scroll_type_get_type ());
  g_type_ensure (gtk_scrollable_get_type ());
  g_type_ensure (gtk_scrollable_policy_get_type ());
  g_type_ensure (gtk_scrollbar_get_type ());
  g_type_ensure (gtk_scrolled_window_get_type ());
  g_type_ensure (gtk_search_bar_get_type ());
  g_type_ensure (gtk_search_entry_get_type ());
  g_type_ensure (gtk_section_model_get_type ());
  g_type_ensure (gtk_selection_filter_model_get_type ());
  g_type_ensure (gtk_selection_mode_get_type ());
  g_type_ensure (gtk_selection_model_get_type ());
  g_type_ensure (gtk_sensitivity_type_get_type ());
  g_type_ensure (gtk_separator_get_type ());
  g_type_ensure (gtk_settings_get_type ());
  g_type_ensure (gtk_shortcut_get_type ());
  g_type_ensure (gtk_shortcut_action_get_type ());
  g_type_ensure (gtk_shortcut_action_flags_get_type ());
  g_type_ensure (gtk_shortcut_controller_get_type ());
  g_type_ensure (gtk_shortcut_label_get_type ());
  g_type_ensure (gtk_shortcut_manager_get_type ());
  g_type_ensure (gtk_shortcut_scope_get_type ());
  g_type_ensure (gtk_shortcut_trigger_get_type ());
  g_type_ensure (gtk_shortcut_type_get_type ());
  g_type_ensure (gtk_shortcuts_group_get_type ());
  g_type_ensure (gtk_shortcuts_section_get_type ());
  g_type_ensure (gtk_shortcuts_shortcut_get_type ());
  g_type_ensure (gtk_shortcuts_window_get_type ());
  g_type_ensure (gtk_signal_action_get_type ());
  g_type_ensure (gtk_signal_list_item_factory_get_type ());
  g_type_ensure (gtk_single_selection_get_type ());
  g_type_ensure (gtk_size_group_get_type ());
  g_type_ensure (gtk_size_group_mode_get_type ());
  g_type_ensure (gtk_size_request_mode_get_type ());
  g_type_ensure (gtk_slice_list_model_get_type ());
  g_type_ensure (gtk_snapshot_get_type ());
  g_type_ensure (gtk_sort_list_model_get_type ());
  g_type_ensure (gtk_sort_type_get_type ());
  g_type_ensure (gtk_sorter_get_type ());
  g_type_ensure (gtk_sorter_change_get_type ());
  g_type_ensure (gtk_sorter_order_get_type ());
  g_type_ensure (gtk_spin_button_get_type ());
  g_type_ensure (gtk_spin_button_update_policy_get_type ());
  g_type_ensure (gtk_spin_type_get_type ());
  g_type_ensure (gtk_spinner_get_type ());
  g_type_ensure (gtk_stack_get_type ());
  g_type_ensure (gtk_stack_page_get_type ());
  g_type_ensure (gtk_stack_sidebar_get_type ());
  g_type_ensure (gtk_stack_switcher_get_type ());
  g_type_ensure (gtk_stack_transition_type_get_type ());
  g_type_ensure (gtk_state_flags_get_type ());
  g_type_ensure (gtk_statusbar_get_type ());
  g_type_ensure (gtk_string_filter_get_type ());
  g_type_ensure (gtk_string_filter_match_mode_get_type ());
  g_type_ensure (gtk_string_list_get_type ());
  g_type_ensure (gtk_string_object_get_type ());
  g_type_ensure (gtk_string_sorter_get_type ());
  g_type_ensure (gtk_style_context_get_type ());
  g_type_ensure (gtk_style_context_print_flags_get_type ());
  g_type_ensure (gtk_style_provider_get_type ());
  g_type_ensure (gtk_switch_get_type ());
  g_type_ensure (gtk_symbolic_color_get_type ());
  g_type_ensure (gtk_symbolic_paintable_get_type ());
  g_type_ensure (gtk_system_setting_get_type ());
  g_type_ensure (gtk_text_get_type ());
  g_type_ensure (gtk_text_buffer_get_type ());
  g_type_ensure (gtk_text_buffer_notify_flags_get_type ());
  g_type_ensure (gtk_text_child_anchor_get_type ());
  g_type_ensure (gtk_text_direction_get_type ());
  g_type_ensure (gtk_text_extend_selection_get_type ());
  g_type_ensure (gtk_text_iter_get_type ());
  g_type_ensure (gtk_text_mark_get_type ());
  g_type_ensure (gtk_text_search_flags_get_type ());
  g_type_ensure (gtk_text_tag_get_type ());
  g_type_ensure (gtk_text_tag_table_get_type ());
  g_type_ensure (gtk_text_view_get_type ());
  g_type_ensure (gtk_text_view_layer_get_type ());
  g_type_ensure (gtk_text_window_type_get_type ());
  g_type_ensure (gtk_toggle_button_get_type ());
  g_type_ensure (gtk_tooltip_get_type ());
  g_type_ensure (gtk_tree_expander_get_type ());
  g_type_ensure (gtk_tree_iter_get_type ());
  g_type_ensure (gtk_tree_list_model_get_type ());
  g_type_ensure (gtk_tree_list_row_get_type ());
  g_type_ensure (gtk_tree_list_row_sorter_get_type ());
  g_type_ensure (gtk_tree_model_get_type ());
  g_type_ensure (gtk_tree_model_filter_get_type ());
  g_type_ensure (gtk_tree_model_flags_get_type ());
  g_type_ensure (gtk_tree_model_sort_get_type ());
  g_type_ensure (gtk_tree_path_get_type ());
  g_type_ensure (gtk_tree_row_reference_get_type ());
  g_type_ensure (gtk_tree_selection_get_type ());
  g_type_ensure (gtk_tree_sortable_get_type ());
  g_type_ensure (gtk_tree_store_get_type ());
  g_type_ensure (gtk_tree_view_get_type ());
  g_type_ensure (gtk_tree_view_column_get_type ());
  g_type_ensure (gtk_tree_view_column_sizing_get_type ());
  g_type_ensure (gtk_tree_view_drop_position_get_type ());
  g_type_ensure (gtk_tree_view_grid_lines_get_type ());
  g_type_ensure (gtk_unit_get_type ());
  g_type_ensure (gtk_uri_launcher_get_type ());
  g_type_ensure (gtk_video_get_type ());
  g_type_ensure (gtk_viewport_get_type ());
  g_type_ensure (gtk_volume_button_get_type ());
  g_type_ensure (gtk_widget_get_type ());
  g_type_ensure (gtk_widget_paintable_get_type ());
  g_type_ensure (gtk_window_get_type ());
  g_type_ensure (gtk_window_controls_get_type ());
  g_type_ensure (gtk_window_gravity_get_type ());
  g_type_ensure (gtk_window_group_get_type ());
  g_type_ensure (gtk_window_handle_get_type ());
  g_type_ensure (gtk_wrap_mode_get_type ());
}

static void
bge_wdgt_spec_init (BgeWdgtSpec *self)
{
  self->values                  = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_data_unref);
  self->values_stack            = g_ptr_array_new_with_free_func ((GDestroyNotify) g_hash_table_unref);
  self->foreaches               = g_ptr_array_new_with_free_func (foreach_data_unref);
  self->foreach_stack           = g_ptr_array_new_with_free_func (foreach_data_unref);
  self->anon_values             = g_ptr_array_new_with_free_func (value_data_unref);
  self->track_transition_values = g_ptr_array_new_with_free_func (value_data_unref);
  self->states                  = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, state_data_unref);
  self->children                = g_ptr_array_new_with_free_func (value_data_unref);
  self->nonchildren             = g_ptr_array_new_with_free_func (value_data_unref);

  self->init_state          = state_data_new ();
  self->init_state->name    = g_strdup ("init");
  self->init_state->setters = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, value_data_unref);
  self->init_state->transitions = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, transition_data_unref);
  self->init_state->expressions = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, (GDestroyNotify) gtk_expression_unref);
  self->init_state->snapshot_deps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gtk_expression_unref);
  self->init_state->values_need_tick = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, NULL);

  ensure_state_snapshot (self->init_state);
  g_hash_table_replace (self->states,
                        g_strdup ("init"),
                        state_data_ref (self->init_state));

  self->ready = FALSE;
}

BgeWdgtSpec *
bge_wdgt_spec_new (void)
{
  return g_object_new (BGE_TYPE_WDGT_SPEC, NULL);
}

BgeWdgtSpec *
bge_wdgt_spec_new_for_string (const char *string,
                              GError    **error)
{
  g_return_val_if_fail (string != NULL, NULL);
  return bge_wdgt_parse_string (string, error);
}

BgeWdgtSpec *
bge_wdgt_spec_new_for_resource (const char *resource,
                                GError    **error)
{
  g_autoptr (GBytes) bytes = NULL;
  gsize         size       = 0;
  gconstpointer data       = NULL;

  g_return_val_if_fail (resource != NULL, NULL);

  bytes = g_resources_lookup_data (resource, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
  if (bytes == NULL)
    return NULL;

  data = g_bytes_get_data (bytes, &size);
  return bge_wdgt_parse_string ((const char *) data, error);
}

const char *
bge_wdgt_spec_get_name (BgeWdgtSpec *self)
{
  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), NULL);
  return self->name;
}

void
bge_wdgt_spec_set_name (BgeWdgtSpec *self,
                        const char  *name)
{
  g_return_if_fail (BGE_IS_WDGT_SPEC (self));

  if (name == self->name || (name != NULL && self->name != NULL && g_strcmp0 (name, self->name) == 0))
    return;

  g_clear_pointer (&self->name, g_free);
  if (name != NULL)
    self->name = g_strdup (name);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NAME]);
}

void
bge_wdgt_spec_set_name_take (BgeWdgtSpec *self,
                             char        *name)
{
  g_return_if_fail (BGE_IS_WDGT_SPEC (self));

  if (name != NULL && self->name != NULL && g_strcmp0 (name, self->name) == 0)
    {
      g_free (name);
      return;
    }

  g_clear_pointer (&self->name, g_free);
  if (name != NULL)
    self->name = name;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NAME]);
}

gboolean
bge_wdgt_spec_add_constant_source_value (BgeWdgtSpec  *self,
                                         const char   *name,
                                         const GValue *constant,
                                         GError      **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (constant != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->type = constant->g_type;
  value->kind = VALUE_CONSTANT;
  g_value_copy (constant, g_value_init (&value->constant.value, constant->g_type));

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_component_source_value (BgeWdgtSpec       *self,
                                          const char        *name,
                                          GType              type,
                                          const char *const *components,
                                          guint              n_components,
                                          GError           **error)
{
  GType expected_types[ARGBUF_SIZE] = { 0 };
  g_autoptr (ValueData) value       = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (components != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  if (type == GRAPHENE_TYPE_POINT)
    {
      if (n_components != 2)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "composed point value needs 2 arguments");
          return FALSE;
        }
      expected_types[0] = G_TYPE_DOUBLE;
      expected_types[1] = G_TYPE_DOUBLE;
    }
  else if (type == GRAPHENE_TYPE_POINT3D)
    {
      if (n_components != 3)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "composed 3d point value needs 3 arguments");
          return FALSE;
        }
      expected_types[0] = G_TYPE_DOUBLE;
      expected_types[1] = G_TYPE_DOUBLE;
      expected_types[2] = G_TYPE_DOUBLE;
    }
  else if (type == GRAPHENE_TYPE_SIZE)
    {
      if (n_components != 2)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "composed 2d size value needs 2 arguments");
          return FALSE;
        }
      expected_types[0] = G_TYPE_DOUBLE;
      expected_types[1] = G_TYPE_DOUBLE;
    }
  else if (type == GRAPHENE_TYPE_RECT)
    {
      if (n_components != 4)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "composed rectangle value needs 4 arguments");
          return FALSE;
        }
      expected_types[0] = G_TYPE_DOUBLE;
      expected_types[1] = G_TYPE_DOUBLE;
      expected_types[2] = G_TYPE_DOUBLE;
      expected_types[3] = G_TYPE_DOUBLE;
    }
  else if (type == GSK_TYPE_STROKE)
    {
      if (n_components != 4)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "composed rectangle value needs 4 arguments");
          return FALSE;
        }
      expected_types[0] = G_TYPE_DOUBLE; /* line width */
      expected_types[1] = GSK_TYPE_LINE_CAP;
      expected_types[2] = GSK_TYPE_LINE_JOIN;
      expected_types[3] = G_TYPE_DOUBLE; /* miter limit */
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' is not elligible for a component value",
                   g_type_name (type));
      return FALSE;
    }

  value                   = value_data_new ();
  value->name             = g_strdup (name);
  value->type             = type;
  value->kind             = VALUE_COMPONENT;
  value->component.params = g_ptr_array_new_with_free_func (value_data_unref);

  for (guint i = 0; i < n_components; i++)
    {
      ValueData *value_data = NULL;

      value_data = g_hash_table_lookup (self->values, components[i]);
      if (value_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' is undefined", components[i]);
          return FALSE;
        }
      if (!g_type_is_a (value_data->type, expected_types[i]))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "component %u for type %s "
                       "must be of type %s, got %s",
                       i, g_type_name (type),
                       g_type_name (expected_types[i]),
                       g_type_name (value_data->type));
          return FALSE;
        }

      g_ptr_array_add (value->component.params,
                       value_data_ref (value_data));
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_transform_source_value (BgeWdgtSpec       *self,
                                          const char        *name,
                                          const char        *next,
                                          const char        *instruction,
                                          const char *const *args,
                                          guint              n_args,
                                          GError           **error)
{
  gboolean   result           = FALSE;
  ValueData *next_data        = NULL;
  g_autoptr (ValueData) value = NULL;
  TransformInstr match        = { 0 };

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (next != NULL, FALSE);
  g_return_val_if_fail (instruction != NULL, FALSE);
  g_return_val_if_fail (n_args == 0 || args != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  next_data = g_hash_table_lookup (self->values, next);
  if (next_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", next);
      return FALSE;
    }
  if (next_data->type != GSK_TYPE_TRANSFORM)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' must be of type %s to "
                   "build a transform, got %s",
                   next,
                   g_type_name (GSK_TYPE_TRANSFORM),
                   g_type_name (next_data->type));
      return FALSE;
    }

  result = lookup_transform_instr (instruction, &match);
  if (!result)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "\"%s\" is not a valid transform builder instruction",
                   instruction);
      return FALSE;
    }
  if (n_args != match.n_args)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transform builder function %s requires %u "
                   "arguments, got %u",
                   match.name, match.n_args, n_args);
      return FALSE;
    }

  value                 = value_data_new ();
  value->name           = g_strdup (name);
  value->type           = GSK_TYPE_TRANSFORM;
  value->kind           = VALUE_TRANSFORM;
  value->transform.func = match.call;
  value->transform.next = value_data_ref (next_data);
  value->transform.args = g_ptr_array_new_with_free_func (value_data_unref);

  for (guint i = 0; i < n_args; i++)
    {
      ValueData *value_data = NULL;

      value_data = g_hash_table_lookup (self->values, args[i]);
      if (value_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' is undefined", args[i]);
          return FALSE;
        }

      if (!g_type_is_a (value_data->type, match.args[i]))
        {
          if (check_can_coerce_type (match.args[i], value_data->type))
            g_ptr_array_add (value->transform.args,
                             wrap_coerce_value (self, value_data, match.args[i]));
          else
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                           "argument %u for snapshot instruction %s "
                           "must be of type %s, got %s",
                           i, match.name,
                           g_type_name (match.args[i]),
                           g_type_name (value_data->type));
              return FALSE;
            }
        }
      else
        g_ptr_array_add (value->transform.args, value_data_ref (value_data));
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_path_source_value (BgeWdgtSpec              *self,
                                     const char               *name,
                                     const char *const        *instructions,
                                     const char *const *const *argss,
                                     const guint              *n_argss,
                                     guint                     n_args,
                                     GError                  **error)
{
  gboolean result             = FALSE;
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (instructions != NULL, FALSE);
  g_return_val_if_fail (argss != NULL, FALSE);
  g_return_val_if_fail (n_argss != NULL, FALSE);
  g_return_val_if_fail (n_args > 0, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value             = value_data_new ();
  value->name       = g_strdup (name);
  value->type       = GSK_TYPE_PATH;
  value->kind       = VALUE_PATH;
  value->path.funcs = g_ptr_array_new ();
  value->path.argss = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_ptr_array_unref);

  for (guint i = 0; i < n_args; i++)
    {
      PathBuilderInstr match     = { 0 };
      g_autoptr (GPtrArray) args = NULL;

      result = lookup_path_builder_instr (instructions[i], &match);
      if (!result)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "\"%s\" is not a valid path builder instruction",
                       instructions[i]);
          return FALSE;
        }
      if (n_argss[i] != match.n_args)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "path builder instructions %s requires %u "
                       "arguments, got %u",
                       match.name, match.n_args, n_argss[i]);
          return FALSE;
        }

      args = g_ptr_array_new_with_free_func (value_data_unref);

      for (guint j = 0; j < n_argss[i]; j++)
        {
          ValueData *value_data = NULL;

          value_data = g_hash_table_lookup (self->values, argss[i][j]);
          if (value_data == NULL)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                           "value '%s' is undefined", argss[i][j]);
              return FALSE;
            }

          if (!g_type_is_a (value_data->type, match.args[j]))
            {
              if (check_can_coerce_type (match.args[j], value_data->type))
                g_ptr_array_add (args, wrap_coerce_value (self, value_data, match.args[j]));
              else
                {
                  g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                               "argument %u for path builder instruction %s "
                               "must be of type %s, got %s",
                               j, match.name,
                               g_type_name (match.args[j]),
                               g_type_name (value_data->type));
                  return FALSE;
                }
            }
          else
            g_ptr_array_add (args, value_data_ref (value_data));
        }

      g_ptr_array_add (value->path.funcs, match.call);
      g_ptr_array_add (value->path.argss, g_steal_pointer (&args));
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_instance_source_value (BgeWdgtSpec *self,
                                         const char  *name,
                                         GType        type,
                                         GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }
  if (!g_type_is_a (type, G_TYPE_OBJECT))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' is not an object type",
                   g_type_name (type));
      return FALSE;
    }
  if (!G_TYPE_IS_INSTANTIATABLE (type) ||
      G_TYPE_IS_ABSTRACT (type))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' is not instantiable",
                   g_type_name (type));
      return FALSE;
    }

  value       = value_data_new ();
  value->kind = VALUE_OBJECT;
  value->type = type;
  value->name = g_strdup (name);

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  g_ptr_array_add (self->nonchildren, value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_child_source_value (BgeWdgtSpec       *self,
                                      const char        *name,
                                      GType              type,
                                      const char        *parent,
                                      const char        *builder_type,
                                      const char *const *css_classes,
                                      guint              n_css_classes,
                                      GError           **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }
  if (!g_type_is_a (type, GTK_TYPE_WIDGET))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' does not derive from GtkWidget",
                   g_type_name (type));
      return FALSE;
    }
  if (!G_TYPE_IS_INSTANTIATABLE (type) ||
      G_TYPE_IS_ABSTRACT (type))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' is not instantiable",
                   g_type_name (type));
      return FALSE;
    }

  value                     = value_data_new ();
  value->kind               = VALUE_CHILD;
  value->type               = type;
  value->name               = g_strdup (name);
  value->child.builder_type = builder_type != NULL ? g_strdup (builder_type) : NULL;

  if (n_css_classes > 0 &&
      css_classes != NULL)
    {
      value->child.css_classes = g_ptr_array_new_with_free_func (g_free);
      g_ptr_array_set_size (value->child.css_classes, n_css_classes);

      for (guint i = 0; i < n_css_classes; i++)
        {
          g_ptr_array_index (value->child.css_classes, i) = g_strdup (css_classes[i]);
        }
    }

  if (parent != NULL)
    {
      ValueData *parent_data = NULL;

      parent_data = g_hash_table_lookup (self->values, parent);
      if (parent_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' is undefined", parent);
          return FALSE;
        }
      if (!g_type_is_a (parent_data->type, GTK_TYPE_BUILDABLE))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "parent widget '%s' is not of type %s",
                       g_type_name (GTK_TYPE_BUILDABLE), parent);
          return FALSE;
        }

      value->child.parent_widget = value_data_ref (parent_data);
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  g_ptr_array_add (self->children, value_data_ref (value));
  return TRUE;
}

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
                                         GError           **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (func != NULL, FALSE);
  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (n_args > 0, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      if (user_data != NULL &&
          destroy_user_data != NULL)
        destroy_user_data (user_data);
      return FALSE;
    }
  if (!G_TYPE_IS_VALUE (type))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "invalid type '%s'", g_type_name (type));
      if (user_data != NULL &&
          destroy_user_data != NULL)
        destroy_user_data (user_data);
      return FALSE;
    }

  value                  = value_data_new ();
  value->kind            = VALUE_CLOSURE;
  value->name            = g_strdup (name);
  value->type            = type;
  value->closure.marshal = marshal;
  value->closure.func    = func;
  value->closure.args    = g_ptr_array_new_with_free_func (value_data_unref);

  value->closure.user_data         = user_data;
  value->closure.destroy_user_data = destroy_user_data;

  for (guint i = 0; i < n_args; i++)
    {
      ValueData *value_data = NULL;

      value_data = g_hash_table_lookup (self->values, args[i]);
      if (value_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' is undefined", args[i]);
          return FALSE;
        }

      if (arg_types != NULL &&
          !g_type_is_a (value_data->type, arg_types[i]))
        {
          if (check_can_coerce_type (arg_types[i], value_data->type))
            g_ptr_array_add (value->closure.args,
                             wrap_coerce_value (self, value_data, arg_types[i]));
          else
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                           "component %u for type %s "
                           "must be of type %s, got %s",
                           i, g_type_name (type),
                           g_type_name (arg_types[i]),
                           g_type_name (value_data->type));
              return FALSE;
            }
        }
      else
        g_ptr_array_add (value->closure.args, value_data_ref (value_data));
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_measure_for_size_source_value (BgeWdgtSpec *self,
                                                 const char  *name,
                                                 GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->kind = VALUE_MEASURE_FOR_SIZE;
  value->type = G_TYPE_INT;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_widget_width_source_value (BgeWdgtSpec *self,
                                             const char  *name,
                                             GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->kind = VALUE_WIDGET_WIDTH;
  value->type = G_TYPE_DOUBLE;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_widget_height_source_value (BgeWdgtSpec *self,
                                              const char  *name,
                                              GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->kind = VALUE_WIDGET_HEIGHT;
  value->type = G_TYPE_DOUBLE;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_tick_time_source_value (BgeWdgtSpec *self,
                                          const char  *name,
                                          GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->kind = VALUE_TICK_TIME;
  value->type = G_TYPE_DOUBLE;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_track_transition_source_value (BgeWdgtSpec *self,
                                                 const char  *name,
                                                 const char  *src,
                                                 const char  *damping_ratio,
                                                 const char  *mass,
                                                 const char  *stiffness,
                                                 GError     **error)
{
  ValueData *src_value                              = NULL;
  g_autoptr (ValueData) coerced_src_value           = NULL;
  ValueData *damping_ratio_value                    = NULL;
  g_autoptr (ValueData) coerced_damping_ratio_value = NULL;
  ValueData *mass_value                             = NULL;
  g_autoptr (ValueData) coerced_mass_value          = NULL;
  ValueData *stiffness_value                        = NULL;
  g_autoptr (ValueData) coerced_stiffness_value     = NULL;
  g_autoptr (ValueData) value                       = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (damping_ratio != NULL, FALSE);
  g_return_val_if_fail (mass != NULL, FALSE);
  g_return_val_if_fail (stiffness != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  /* TODO clean up type checking process */

  src_value = g_hash_table_lookup (self->values, src);
  if (src_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", src);
      return FALSE;
    }
  if (src_value->type != G_TYPE_DOUBLE)
    {
      if (check_can_coerce_type (G_TYPE_DOUBLE, src_value->type))
        coerced_src_value = wrap_coerce_value (self, src_value, src_value->type);
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "source type %s cannot be transitioned",
                       g_type_name (src_value->type));
          return FALSE;
        }
    }

  damping_ratio_value = g_hash_table_lookup (self->values, damping_ratio);
  if (damping_ratio_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", damping_ratio);
      return FALSE;
    }
  if (damping_ratio_value->type != G_TYPE_DOUBLE)
    {
      if (check_can_coerce_type (G_TYPE_DOUBLE, damping_ratio_value->type))
        coerced_damping_ratio_value = wrap_coerce_value (self, damping_ratio_value, damping_ratio_value->type);
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "damping ratio must be a double, got type %s",
                       g_type_name (damping_ratio_value->type));
          return FALSE;
        }
    }

  mass_value = g_hash_table_lookup (self->values, mass);
  if (mass_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", mass);
      return FALSE;
    }
  if (mass_value->type != G_TYPE_DOUBLE)
    {
      if (check_can_coerce_type (G_TYPE_DOUBLE, mass_value->type))
        coerced_mass_value = wrap_coerce_value (self, mass_value, mass_value->type);
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "mass must be a double, got type %s",
                       g_type_name (mass_value->type));
          return FALSE;
        }
    }

  stiffness_value = g_hash_table_lookup (self->values, stiffness);
  if (stiffness_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", stiffness);
      return FALSE;
    }
  if (stiffness_value->type != G_TYPE_DOUBLE)
    {
      if (check_can_coerce_type (G_TYPE_DOUBLE, stiffness_value->type))
        coerced_stiffness_value = wrap_coerce_value (self, stiffness_value, stiffness_value->type);
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "stiffness must be a double, got type %s",
                       g_type_name (stiffness_value->type));
          return FALSE;
        }
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->kind = VALUE_TRACK_TRANSITION;
  value->type = G_TYPE_DOUBLE;

  value->track_transition.src =
      coerced_src_value != NULL
          ? value_data_ref (coerced_src_value)
          : value_data_ref (src_value);
  value->track_transition.spring.damping_ratio =
      coerced_damping_ratio_value != NULL
          ? value_data_ref (coerced_damping_ratio_value)
          : value_data_ref (damping_ratio_value);
  value->track_transition.spring.mass =
      coerced_mass_value != NULL
          ? value_data_ref (coerced_mass_value)
          : value_data_ref (mass_value);
  value->track_transition.spring.stiffness =
      coerced_stiffness_value != NULL
          ? value_data_ref (coerced_stiffness_value)
          : value_data_ref (stiffness_value);

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  g_ptr_array_add (self->track_transition_values, value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_variable_value (BgeWdgtSpec *self,
                                  GType        type,
                                  const char  *name,
                                  GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (!G_TYPE_IS_VALUE (type))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' cannot be stored in value '%s'",
                   g_type_name (type), name);
      return FALSE;
    }
  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->type = type;
  value->kind = VALUE_VARIABLE;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_reference_object_value (BgeWdgtSpec *self,
                                          GType        type,
                                          const char  *name,
                                          GError     **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (self->reference != NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "an object reference value has already been defined");
      return FALSE;
    }
  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }
  if (!g_type_is_a (type, G_TYPE_OBJECT))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type '%s' cannot be stored in reference object value '%s'",
                   g_type_name (type), name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->type = type;
  value->kind = VALUE_REFERENCE_OBJECT;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  self->reference = value_data_ref (value);
  return TRUE;
}

gboolean
bge_wdgt_spec_add_property_value (BgeWdgtSpec *self,
                                  const char  *name,
                                  const char  *object,
                                  const char  *property,
                                  GType       *type_out,
                                  GError     **error)
{
  ValueData *object_value           = NULL;
  ValueData *existing_value         = NULL;
  g_autoptr (GTypeClass) type_class = NULL;
  GParamSpec *pspec                 = NULL;
  g_autoptr (ValueData) value       = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (object != NULL, FALSE);
  g_return_val_if_fail (property != NULL, FALSE);

  object_value = g_hash_table_lookup (self->values, object);
  if (object_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", object);
      return FALSE;
    }
  if (!g_type_is_a (object_value->type, G_TYPE_OBJECT))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is not an object", object);
      return FALSE;
    }

  existing_value = g_hash_table_lookup (self->values, name);
  if (existing_value != NULL)
    {
      if (existing_value->kind == VALUE_PROPERTY &&
          existing_value->property.object == object_value &&
          g_strcmp0 (existing_value->property.prop_name, property) == 0)
        return TRUE;
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' already exists", name);
          return FALSE;
        }
    }

  type_class = g_type_class_ref (object_value->type);
  pspec      = g_object_class_find_property (G_OBJECT_CLASS (type_class), property);
  if (pspec == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "property '%s' doesn't exist on type %s",
                   property, g_type_name (object_value->type));
      return FALSE;
    }

  value                       = value_data_new ();
  value->name                 = g_strdup (name);
  value->type                 = pspec->value_type;
  value->kind                 = VALUE_PROPERTY;
  value->property.object      = value_data_ref (object_value);
  value->property.prop_name   = g_strdup (property);
  value->property.pspec_flags = pspec->flags;

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));

  if (type_out != NULL)
    *type_out = pspec->value_type;
  return TRUE;
}

gboolean
bge_wdgt_spec_add_allocation_width_value (BgeWdgtSpec *self,
                                          const char  *name,
                                          const char  *child,
                                          GError     **error)
{
  ValueData *child_value      = NULL;
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (child != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  child_value = g_hash_table_lookup (self->values, child);
  if (child_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", child);
      return FALSE;
    }
  if (!g_type_is_a (child_value->type, GTK_TYPE_WIDGET))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is not a widget", child);
      return FALSE;
    }

  value                    = value_data_new ();
  value->name              = g_strdup (name);
  value->type              = G_TYPE_INT;
  value->kind              = VALUE_ALLOCATION_WIDTH;
  value->allocation.widget = value_data_ref (child_value);

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_allocation_height_value (BgeWdgtSpec *self,
                                           const char  *name,
                                           const char  *child,
                                           GError     **error)
{
  ValueData *child_value      = NULL;
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (child != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  child_value = g_hash_table_lookup (self->values, child);
  if (child_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", child);
      return FALSE;
    }
  if (!g_type_is_a (child_value->type, GTK_TYPE_WIDGET))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is not a widget", child);
      return FALSE;
    }

  value                    = value_data_new ();
  value->name              = g_strdup (name);
  value->type              = G_TYPE_INT;
  value->kind              = VALUE_ALLOCATION_HEIGHT;
  value->allocation.widget = value_data_ref (child_value);

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_allocation_transform_value (BgeWdgtSpec *self,
                                              const char  *name,
                                              const char  *child,
                                              GError     **error)
{
  ValueData *child_value      = NULL;
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (child != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  child_value = g_hash_table_lookup (self->values, child);
  if (child_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", child);
      return FALSE;
    }
  if (!g_type_is_a (child_value->type, GTK_TYPE_WIDGET))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is not a widget", child);
      return FALSE;
    }

  value                    = value_data_new ();
  value->name              = g_strdup (name);
  value->type              = GSK_TYPE_TRANSFORM;
  value->kind              = VALUE_ALLOCATION_TRANSFORM;
  value->allocation.widget = value_data_ref (child_value);

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_measure_value (BgeWdgtSpec       *self,
                                 const char        *name,
                                 BgeWdgtMeasureKind kind,
                                 GError           **error)
{
  g_autoptr (ValueData) value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->values, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", name);
      return FALSE;
    }

  value       = value_data_new ();
  value->name = g_strdup (name);
  value->type = G_TYPE_INT;

  switch (kind)
    {
    case BGE_WDGT_MEASURE_MINIMUM_WIDTH:
      value->kind = VALUE_MEASURE_MINIMUM_WIDTH;
      break;
    case BGE_WDGT_MEASURE_NATURAL_WIDTH:
      value->kind = VALUE_MEASURE_NATURAL_WIDTH;
      break;
    case BGE_WDGT_MEASURE_MINIMUM_HEIGHT:
      value->kind = VALUE_MEASURE_MINIMUM_HEIGHT;
      break;
    case BGE_WDGT_MEASURE_NATURAL_HEIGHT:
      value->kind = VALUE_MEASURE_NATURAL_HEIGHT;
      break;
    default:
      {
        g_critical ("Passed an invalid measure kind");
        return FALSE;
      }
    }

  g_hash_table_replace (self->values, g_strdup (name), value_data_ref (value));
  return TRUE;
}

gboolean
bge_wdgt_spec_add_state (BgeWdgtSpec *self,
                         const char  *name,
                         gboolean     default_state,
                         GError     **error)
{
  g_autoptr (StateData) state = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  if (g_hash_table_contains (self->states, name))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "state '%s' already exists", name);
      return FALSE;
    }
  if (default_state && self->default_state != NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "default state already specified");
      return FALSE;
    }

  state          = state_data_new ();
  state->name    = g_strdup (name);
  state->setters = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, value_data_unref);
  state->transitions = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, transition_data_unref);
  state->expressions = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, (GDestroyNotify) gtk_expression_unref);
  state->snapshot_deps = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gtk_expression_unref);
  state->values_need_tick = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, NULL);

  g_hash_table_replace (self->states, g_strdup (name), state_data_ref (state));
  if (default_state)
    self->default_state = state_data_ref (state);

  return TRUE;
}

gboolean
bge_wdgt_spec_set_value (BgeWdgtSpec *self,
                         const char  *state,
                         const char  *dest_value,
                         const char  *src_value,
                         GError     **error)
{
  StateData *state_data                  = NULL;
  ValueData *dest_data                   = NULL;
  ValueData *src_data                    = NULL;
  g_autoptr (ValueData) coerced_src_data = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);
  g_return_val_if_fail (src_value != NULL, FALSE);

  if (state != NULL)
    state_data = g_hash_table_lookup (self->states, state);
  else
    state_data = self->init_state;

  dest_data = g_hash_table_lookup (self->values, dest_value);
  src_data  = g_hash_table_lookup (self->values, src_value);

  if (state_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "state '%s' is undefined", state);
      return FALSE;
    }
  if (dest_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", dest_value);
      return FALSE;
    }
  if (src_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", src_value);
      return FALSE;
    }
  if (dest_data == src_data)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "cannot assign '%s' to itself", src_value);
      return FALSE;
    }
  if (!g_type_is_a (src_data->type, dest_data->type))
    {
      if (check_can_coerce_type (dest_data->type, src_data->type))
        coerced_src_data = wrap_coerce_value (self, src_data, dest_data->type);
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "source type %s cannot be assigned to destination type %s",
                       g_type_name (src_data->type), g_type_name (dest_data->type));
          return FALSE;
        }
    }
  if (dest_data->kind == VALUE_CONSTANT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "cannot assign to a constant source value");
      return FALSE;
    }
  if (dest_data->kind == VALUE_PROPERTY &&
      !(dest_data->property.pspec_flags & G_PARAM_WRITABLE))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "property %s on type %s is not writable",
                   dest_data->property.prop_name,
                   g_type_name (dest_data->type));
      return FALSE;
    }
  if (src_data->kind == VALUE_PROPERTY &&
      !(src_data->property.pspec_flags & G_PARAM_READABLE))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "property %s on type %s is not writable",
                   src_data->property.prop_name,
                   g_type_name (src_data->type));
      return FALSE;
    }

  g_hash_table_replace (
      state_data->setters,
      value_data_ref (dest_data),
      coerced_src_data != NULL
          ? value_data_ref (coerced_src_data)
          : value_data_ref (src_data));
  return TRUE;
}

gboolean
bge_wdgt_spec_transition_value (BgeWdgtSpec *self,
                                const char  *state,
                                const char  *value,
                                const char  *seconds,
                                const char  *easing,
                                GError     **error)
{
  StateData *state_data                 = NULL;
  ValueData *value_data                 = NULL;
  ValueData *seconds_data               = NULL;
  ValueData *easing_data                = NULL;
  g_autoptr (TransitionData) transition = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  /* state is required for transitions */
  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (seconds != NULL, FALSE);
  g_return_val_if_fail (easing != NULL, FALSE);

  state_data = g_hash_table_lookup (self->states, state);
  if (state_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "state '%s' is undefined", state);
      return FALSE;
    }

  value_data   = g_hash_table_lookup (self->values, value);
  seconds_data = g_hash_table_lookup (self->values, seconds);
  easing_data  = g_hash_table_lookup (self->values, easing);

  if (value_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", value);
      return FALSE;
    }
  if (value_data->type != G_TYPE_DOUBLE)
    /* TODO: support more types */
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type %s cannot be transitioned",
                   g_type_name (value_data->type));
      return FALSE;
    }

  if (seconds_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", value);
      return FALSE;
    }
  if (seconds_data->type != G_TYPE_DOUBLE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transition ease seconds value must be a double, got %s",
                   g_type_name (seconds_data->type));
      return FALSE;
    }

  if (easing_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", value);
      return FALSE;
    }
  if (easing_data->type != BGE_TYPE_EASING)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transition ease easing value must be of type %s, got %s",
                   g_type_name (BGE_TYPE_EASING),
                   g_type_name (easing_data->type));
      return FALSE;
    }

  transition               = transition_data_new ();
  transition->kind         = TRANSITION_EASE;
  transition->ease.seconds = value_data_ref (seconds_data);
  transition->ease.easing  = value_data_ref (easing_data);

  g_hash_table_replace (
      state_data->transitions,
      value_data_ref (value_data),
      transition_data_ref (transition));
  /* We want the init state to track the transition as well in case of value
     overlays */
  g_hash_table_replace (
      self->init_state->transitions,
      value_data_ref (value_data),
      transition_data_ref (transition));
  return TRUE;
}

gboolean
bge_wdgt_spec_transition_value_spring (BgeWdgtSpec *self,
                                       const char  *state,
                                       const char  *value,
                                       const char  *damping_ratio,
                                       const char  *mass,
                                       const char  *stiffness,
                                       GError     **error)
{
  StateData *state_data                 = NULL;
  ValueData *value_data                 = NULL;
  ValueData *damping_ratio_data         = NULL;
  ValueData *mass_data                  = NULL;
  ValueData *stiffness_data             = NULL;
  g_autoptr (TransitionData) transition = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  /* state is required for transitions */
  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (damping_ratio != NULL, FALSE);
  g_return_val_if_fail (mass != NULL, FALSE);
  g_return_val_if_fail (stiffness != NULL, FALSE);

  state_data         = g_hash_table_lookup (self->states, state);
  value_data         = g_hash_table_lookup (self->values, value);
  damping_ratio_data = g_hash_table_lookup (self->values, damping_ratio);
  mass_data          = g_hash_table_lookup (self->values, mass);
  stiffness_data     = g_hash_table_lookup (self->values, stiffness);

  if (state_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "state '%s' is undefined", state);
      return FALSE;
    }
  if (value_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", value);
      return FALSE;
    }
  if (damping_ratio_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", damping_ratio);
      return FALSE;
    }
  if (mass_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", mass);
      return FALSE;
    }
  if (stiffness_data == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", stiffness);
      return FALSE;
    }

  if (value_data->type != G_TYPE_DOUBLE)
    /* TODO: support more types */
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "type %s cannot be transitioned",
                   g_type_name (value_data->type));
      return FALSE;
    }

  if (damping_ratio_data->type != G_TYPE_DOUBLE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transition damping ratio must of type %s, got %s",
                   g_type_name (G_TYPE_DOUBLE),
                   g_type_name (damping_ratio_data->type));
      return FALSE;
    }
  if (mass_data->type != G_TYPE_DOUBLE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transition mass must of type %s, got %s",
                   g_type_name (G_TYPE_DOUBLE),
                   g_type_name (mass_data->type));
      return FALSE;
    }
  if (stiffness_data->type != G_TYPE_DOUBLE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "transition stiffness must of type %s, got %s",
                   g_type_name (G_TYPE_DOUBLE),
                   g_type_name (stiffness_data->type));
      return FALSE;
    }

  transition                       = transition_data_new ();
  transition->kind                 = TRANSITION_SPRING;
  transition->spring.damping_ratio = value_data_ref (damping_ratio_data);
  transition->spring.mass          = value_data_ref (mass_data);
  transition->spring.stiffness     = value_data_ref (stiffness_data);

  g_hash_table_replace (
      state_data->transitions,
      value_data_ref (value_data),
      transition_data_ref (transition));
  /* We want the init state to track the transition as well in case of value
     overlays */
  g_hash_table_replace (
      self->init_state->transitions,
      value_data_ref (value_data),
      transition_data_ref (transition));
  return TRUE;
}

gboolean
bge_wdgt_spec_append_snapshot_instr (BgeWdgtSpec             *self,
                                     const char              *state,
                                     BgeWdgtSnapshotInstrKind kind,
                                     const char              *instr,
                                     const char *const       *args,
                                     guint                    n_args,
                                     guint                   *n_pops_out,
                                     GError                 **error)
{
  gboolean      result              = FALSE;
  StateData    *state_data          = NULL;
  ForeachData  *foreach_context     = NULL;
  SnapshotInstr match               = { 0 };
  guint         match_rest_start    = 0;
  g_autoptr (SnapshotCallData) call = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (instr != NULL, FALSE);
  g_return_val_if_fail (n_args == 0 || args != NULL, FALSE);

  if (state != NULL)
    {
      state_data = g_hash_table_lookup (self->states, state);
      if (state_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "state '%s' is undefined", state);
          return FALSE;
        }
    }
  else
    state_data = self->init_state;

  if (self->foreach_stack->len > 0)
    foreach_context = g_ptr_array_index (
        self->foreach_stack,
        self->foreach_stack->len - 1);

  switch (kind)
    {
    case BGE_WDGT_SNAPSHOT_INSTR_PUSH:
      result = lookup_snapshot_push_instr (instr, &match);
      if (!result)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "\"%s\" is not a valid snapshot push instruction",
                       instr);
          return FALSE;
        }
      if (n_pops_out != NULL)
        *n_pops_out = match.n_pops;
      break;
    case BGE_WDGT_SNAPSHOT_INSTR_TRANSFORM:
      result = lookup_snapshot_transform_instr (instr, &match);
      if (!result)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "\"%s\" is not a valid snapshot transform instruction",
                       instr);
          return FALSE;
        }
      break;
    case BGE_WDGT_SNAPSHOT_INSTR_APPEND:
      result = lookup_snapshot_append_instr (instr, &match);
      if (!result)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "\"%s\" is not a valid snapshot append instruction",
                       instr);
          return FALSE;
        }
      break;
    case BGE_WDGT_SNAPSHOT_INSTR_POP:
    case BGE_WDGT_SNAPSHOT_INSTR_SAVE:
    case BGE_WDGT_SNAPSHOT_INSTR_RESTORE:
      {
        call       = snapshot_call_data_new ();
        call->kind = kind;
        if (foreach_context != NULL)
          call->foreach_context = foreach_data_ref (foreach_context);

        ensure_state_snapshot (state_data);
        g_ptr_array_add (state_data->snapshot->calls, snapshot_call_data_ref (call));
      }
      return TRUE;
    case BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD:
      {
        ValueData *child = NULL;

        if (n_args != 1)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                         "child snapshot instruction requires "
                         "a single argument");
            return FALSE;
          }

        child = g_hash_table_lookup (self->values, args[0]);
        if (child == NULL)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                         "value '%s' is undefined", args[0]);
            return FALSE;
          }
        if (!g_type_is_a (child->type, GTK_TYPE_WIDGET))
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                         "value '%s' is not a child", args[0]);
            return FALSE;
          }

        call        = snapshot_call_data_new ();
        call->kind  = kind;
        call->child = value_data_ref (child);
        if (foreach_context != NULL)
          call->foreach_context = foreach_data_ref (foreach_context);

        ensure_state_snapshot (state_data);
        g_ptr_array_add (state_data->snapshot->calls, snapshot_call_data_ref (call));
      }
      return TRUE;
    default:
      g_critical ("invalid snapshot instruction kind specified");
      return FALSE;
    }
  match_rest_start = match.n_args - match.n_rest;

  if (n_args != match.n_args)
    {
      if (match.n_rest > 0 &&
          n_args > match.n_args)
        {
          if ((n_args - match.n_args) % match.n_rest != 0)
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                         "snapshot instruction %s cannot handle %u "
                         "trailing arguments",
                         match.name, n_args - match.n_args);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "snapshot instruction %s requires %u "
                       "arguments, got %u",
                       match.name, match.n_args, n_args);
          return FALSE;
        }
    }

  call       = snapshot_call_data_new ();
  call->kind = kind;
  call->func = match.call;
  call->args = g_ptr_array_new_with_free_func (value_data_unref);
  call->rest = g_ptr_array_new_with_free_func (value_data_unref);
  if (foreach_context != NULL)
    call->foreach_context = foreach_data_ref (foreach_context);

  for (guint i = 0; i < n_args; i++)
    {
      ValueData *value_data    = NULL;
      gboolean   in_rest       = FALSE;
      GType      expected_type = 0;

      value_data = g_hash_table_lookup (self->values, args[i]);
      if (value_data == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "value '%s' is undefined", args[i]);
          return FALSE;
        }

      in_rest = i >= match_rest_start;
      if (in_rest)
        expected_type = match.args[match_rest_start +
                                   ((i - match_rest_start) %
                                    match.n_rest)];
      else
        expected_type = match.args[i];

      if (!g_type_is_a (value_data->type, expected_type))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                       "argument %u for snapshot instruction %s "
                       "must be of type %s, got %s",
                       i, match.name,
                       g_type_name (expected_type),
                       g_type_name (value_data->type));
          return FALSE;
        }

      if (in_rest)
        g_ptr_array_add (call->rest, value_data_ref (value_data));
      else
        g_ptr_array_add (call->args, value_data_ref (value_data));
    }

  ensure_state_snapshot (state_data);
  g_ptr_array_add (state_data->snapshot->calls, snapshot_call_data_ref (call));

  return TRUE;
}

gboolean
bge_wdgt_spec_push_foreach (BgeWdgtSpec *self,
                            const char  *model,
                            const char  *value_iterator,
                            const char  *index_iterator,
                            GType        iterator_type,
                            GError     **error)
{
  ValueData *model_value                     = NULL;
  g_autoptr (GHashTable) values_copy         = NULL;
  GHashTableIter iter                        = { 0 };
  g_autoptr (ForeachData) foreach_context    = NULL;
  g_autoptr (ValueData) value_iterator_value = NULL;
  g_autoptr (ValueData) index_iterator_value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_SPEC (self), FALSE);
  g_return_val_if_fail (!self->ready, FALSE);
  g_return_val_if_fail (model != NULL, FALSE);
  g_return_val_if_fail (value_iterator != NULL, FALSE);
  g_return_val_if_fail (index_iterator != NULL, FALSE);

  model_value = g_hash_table_lookup (self->values, model);
  if (model_value == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' is undefined", model);
      return FALSE;
    }
  if (!g_type_is_a (model_value->type, G_TYPE_LIST_MODEL))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' does not implement GListModel", model);
      return FALSE;
    }

  if (g_hash_table_contains (self->values, value_iterator))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", value_iterator);
      return FALSE;
    }
  if (g_hash_table_contains (self->values, index_iterator))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "value '%s' already exists", index_iterator);
      return FALSE;
    }

  if (!g_type_is_a (iterator_type, G_TYPE_OBJECT))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_UNKNOWN,
                   "iterator type does not derive from GObject");
      return FALSE;
    }

  values_copy = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, value_data_unref);

  g_hash_table_iter_init (&iter, self->values);
  for (;;)
    {
      char      *name  = NULL;
      ValueData *value = NULL;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &name,
              (gpointer *) &value))
        break;

      g_hash_table_replace (values_copy,
                            g_strdup (name),
                            value_data_ref (value));
    }

  g_ptr_array_add (self->values_stack, g_steal_pointer (&self->values));
  self->values = g_hash_table_ref (values_copy);

  foreach_context         = foreach_data_new ();
  foreach_context->model  = value_data_ref (model_value);
  foreach_context->values = g_hash_table_ref (values_copy);
  if (self->foreach_stack->len > 0)
    {
      ForeachData *parent = NULL;

      parent = g_ptr_array_index (
          self->foreach_stack,
          self->foreach_stack->len - 1);
      foreach_context->parent = foreach_data_ref (parent);
    }

  g_ptr_array_add (self->foreach_stack, foreach_data_ref (foreach_context));
  g_ptr_array_add (self->foreaches, foreach_data_ref (foreach_context));

  value_iterator_value                   = value_data_new ();
  value_iterator_value->kind             = VALUE_ITERATOR;
  value_iterator_value->name             = g_strdup (value_iterator);
  value_iterator_value->type             = iterator_type;
  value_iterator_value->iterator.context = foreach_data_ref (foreach_context);
  g_hash_table_replace (values_copy,
                        g_strdup (value_iterator),
                        value_data_ref (value_iterator_value));

  index_iterator_value                   = value_data_new ();
  index_iterator_value->kind             = VALUE_ITERATOR;
  index_iterator_value->name             = g_strdup (index_iterator);
  index_iterator_value->type             = G_TYPE_UINT;
  index_iterator_value->iterator.context = foreach_data_ref (foreach_context);
  g_hash_table_replace (values_copy,
                        g_strdup (index_iterator),
                        value_data_ref (index_iterator_value));

  return TRUE;
}

gboolean
bge_wdgt_spec_pop_foreach (BgeWdgtSpec *self)
{
  GHashTable *restore_values = NULL;

  g_return_val_if_fail (self->foreach_stack->len > 0, FALSE);
  g_assert (self->values_stack->len > 0);

  g_clear_pointer (&self->values, g_hash_table_unref);

  restore_values = g_ptr_array_index (self->values_stack, self->values_stack->len - 1);
  self->values   = g_hash_table_ref (restore_values);

  g_ptr_array_set_size (self->values_stack, self->values_stack->len - 1);
  g_ptr_array_set_size (self->foreach_stack, self->foreach_stack->len - 1);

  return TRUE;
}

static gboolean
check_can_coerce_type (GType dest,
                       GType src)
{
  const struct
  {
    GType dest;
    GType src;
  } valid_pairings[] = {
    {    G_TYPE_INT,   G_TYPE_UINT },
    {    G_TYPE_INT,  G_TYPE_FLOAT },
    {    G_TYPE_INT, G_TYPE_DOUBLE },

    {   G_TYPE_UINT,    G_TYPE_INT },
    {   G_TYPE_UINT,  G_TYPE_FLOAT },
    {   G_TYPE_UINT, G_TYPE_DOUBLE },

    {  G_TYPE_INT64,    G_TYPE_INT },
    {  G_TYPE_INT64,   G_TYPE_UINT },
    {  G_TYPE_INT64,  G_TYPE_FLOAT },
    {  G_TYPE_INT64, G_TYPE_DOUBLE },

    { G_TYPE_UINT64,    G_TYPE_INT },
    { G_TYPE_UINT64,   G_TYPE_UINT },
    { G_TYPE_UINT64,  G_TYPE_INT64 },
    { G_TYPE_UINT64,  G_TYPE_FLOAT },
    { G_TYPE_UINT64, G_TYPE_DOUBLE },

    {  G_TYPE_FLOAT,    G_TYPE_INT },
    {  G_TYPE_FLOAT,   G_TYPE_UINT },
    {  G_TYPE_FLOAT, G_TYPE_DOUBLE },

    { G_TYPE_DOUBLE,    G_TYPE_INT },
    { G_TYPE_DOUBLE,   G_TYPE_UINT },
    { G_TYPE_DOUBLE,  G_TYPE_FLOAT },

    { G_TYPE_STRING,    G_TYPE_INT },
    { G_TYPE_STRING,   G_TYPE_UINT },
    { G_TYPE_STRING,  G_TYPE_INT64 },
    { G_TYPE_STRING, G_TYPE_UINT64 },
    { G_TYPE_STRING,  G_TYPE_FLOAT },
    { G_TYPE_STRING, G_TYPE_DOUBLE },
  };

  for (guint i = 0; i < G_N_ELEMENTS (valid_pairings); i++)
    {
      if (dest == valid_pairings[i].dest &&
          src == valid_pairings[i].src)
        return TRUE;
    }
  return FALSE;
}

static void
coerce_value (const GValue *in,
              GType         dest_type,
              GValue       *out)
{
  /* Not a switch for style, though not the case here GType macros can sometimes
     be non-compile-time constants */
  if (dest_type == G_TYPE_INT)
    {
      int val = 0;

      if (in->g_type == G_TYPE_UINT)
        val = g_value_get_uint (in);
      else if (in->g_type == G_TYPE_FLOAT)
        val = round (g_value_get_float (in));
      else if (in->g_type == G_TYPE_DOUBLE)
        val = round (g_value_get_double (in));

      g_value_set_int (out, val);
    }
  else if (dest_type == G_TYPE_UINT)
    {
      guint val = 0;

      if (in->g_type == G_TYPE_INT)
        val = g_value_get_int (in);
      else if (in->g_type == G_TYPE_FLOAT)
        val = round (g_value_get_float (in));
      else if (in->g_type == G_TYPE_DOUBLE)
        val = round (g_value_get_double (in));

      g_value_set_uint (out, val);
    }
  else if (dest_type == G_TYPE_INT64)
    {
      gint64 val = 0;

      if (in->g_type == G_TYPE_INT)
        val = g_value_get_int (in);
      else if (in->g_type == G_TYPE_UINT)
        val = g_value_get_uint (in);
      else if (in->g_type == G_TYPE_FLOAT)
        val = round (g_value_get_float (in));
      else if (in->g_type == G_TYPE_DOUBLE)
        val = round (g_value_get_double (in));

      g_value_set_int64 (out, val);
    }
  else if (dest_type == G_TYPE_UINT64)
    {
      guint64 val = 0;

      if (in->g_type == G_TYPE_INT)
        val = g_value_get_int (in);
      else if (in->g_type == G_TYPE_UINT)
        val = g_value_get_uint (in);
      else if (in->g_type == G_TYPE_INT64)
        val = g_value_get_int64 (in);
      else if (in->g_type == G_TYPE_UINT64)
        val = round (g_value_get_float (in));
      else if (in->g_type == G_TYPE_DOUBLE)
        val = round (g_value_get_double (in));

      g_value_set_uint64 (out, val);
    }
  else if (dest_type == G_TYPE_FLOAT)
    {
      float val = 0;

      if (in->g_type == G_TYPE_INT)
        val = g_value_get_int (in);
      else if (in->g_type == G_TYPE_UINT)
        val = g_value_get_uint (in);
      else if (in->g_type == G_TYPE_DOUBLE)
        val = round (g_value_get_double (in));

      g_value_set_float (out, val);
    }
  else if (dest_type == G_TYPE_DOUBLE)
    {
      double val = 0;

      if (in->g_type == G_TYPE_INT)
        val = g_value_get_int (in);
      else if (in->g_type == G_TYPE_UINT)
        val = g_value_get_uint (in);
      else if (in->g_type == G_TYPE_FLOAT)
        val = round (g_value_get_float (in));

      g_value_set_double (out, val);
    }
  else if (dest_type == G_TYPE_STRING)
    {
      g_autofree char *val = NULL;

      if (in->g_type == G_TYPE_INT)
        val = g_strdup_printf ("%d", g_value_get_int (in));
      else if (in->g_type == G_TYPE_UINT)
        val = g_strdup_printf ("%u", g_value_get_uint (in));
      else if (in->g_type == G_TYPE_INT64)
        val = g_strdup_printf ("%zd", g_value_get_int64 (in));
      else if (in->g_type == G_TYPE_UINT64)
        val = g_strdup_printf ("%zu", g_value_get_uint64 (in));
      else if (in->g_type == G_TYPE_FLOAT)
        val = g_strdup_printf ("%f", g_value_get_float (in));
      else if (in->g_type == G_TYPE_DOUBLE)
        val = g_strdup_printf ("%f", g_value_get_double (in));

      g_value_take_string (out, g_steal_pointer (&val));
    }
}

static ValueData *
wrap_coerce_value (BgeWdgtSpec *self,
                   ValueData   *value,
                   GType        dest_type)
{
  g_autoptr (ValueData) coerced = NULL;

  g_assert (!g_type_is_a (value->type, dest_type));

  coerced                 = value_data_new ();
  coerced->type           = dest_type;
  coerced->kind           = VALUE_COERCION;
  coerced->coercion.value = value_data_ref (value);

  g_ptr_array_add (self->anon_values, value_data_ref (coerced));
  return g_steal_pointer (&coerced);
}

static void
ensure_state_snapshot (StateData *state)
{
  if (state->snapshot != NULL)
    return;

  state->snapshot        = snapshot_data_new ();
  state->snapshot->calls = g_ptr_array_new_with_free_func (
      snapshot_call_data_unref);
}

/* ------------------------------ */
/* Widget Renderer Implementation */
/* ------------------------------ */

#define BGE_TYPE_WDGT_NOTIFIER (bge_wdgt_notifier_get_type ())
G_DECLARE_FINAL_TYPE (BgeWdgtNotifier, bge_wdgt_notifier, BGE, WDGT_NOTIFIER, GObject)

struct _BgeWdgtNotifier
{
  GObject parent_instance;

  double value;
};

G_DEFINE_FINAL_TYPE (BgeWdgtNotifier, bge_wdgt_notifier, G_TYPE_OBJECT);

enum
{
  NOTIFIER_PROP_0,

  NOTIFIER_PROP_VALUE,

  LAST_NOTIFIER_PROP
};
static GParamSpec *notifier_props[LAST_NOTIFIER_PROP] = { 0 };

static void
bge_wdgt_notifier_dispose (GObject *object)
{
  BgeWdgtNotifier *self = BGE_WDGT_NOTIFIER (object);

  (void) self;

  G_OBJECT_CLASS (bge_wdgt_notifier_parent_class)->dispose (object);
}

static void
bge_wdgt_notifier_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BgeWdgtNotifier *self = BGE_WDGT_NOTIFIER (object);

  switch (prop_id)
    {
    case NOTIFIER_PROP_VALUE:
      g_value_set_double (value, self->value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_notifier_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BgeWdgtNotifier *self = BGE_WDGT_NOTIFIER (object);

  switch (prop_id)
    {
    case NOTIFIER_PROP_VALUE:
      self->value = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_notifier_class_init (BgeWdgtNotifierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bge_wdgt_notifier_set_property;
  object_class->get_property = bge_wdgt_notifier_get_property;
  object_class->dispose      = bge_wdgt_notifier_dispose;

  notifier_props[NOTIFIER_PROP_VALUE] =
      g_param_spec_double (
          "value",
          NULL, NULL,
          G_MININT, G_MAXINT, 0.0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, notifier_props);
}

static void
bge_wdgt_notifier_init (BgeWdgtNotifier *self)
{
}

/* --- */

BGE_DEFINE_DATA (
    foreach_instance,
    ForeachInstance,
    {
      guint            current_idx;
      BgeWdgtNotifier *notifier;
    },
    BGE_RELEASE_DATA (notifier, g_object_unref))

BGE_DEFINE_DATA (
    state_instance,
    StateInstance,
    {
      GHashTable *transitions;
    },
    BGE_RELEASE_DATA (transitions, g_hash_table_unref));

struct _BgeWdgtRenderer
{
  GtkWidget parent_instance;

  BgeWdgtSpec *spec;
  char        *state;
  GObject     *reference;
  GtkWidget   *child;

  StateInstanceData *init_instance;
  GPtrArray         *init_watches;

  StateData         *active_state;
  StateInstanceData *active_instance;
  SnapshotData      *active_snapshot;

  StateData         *last_state;
  StateInstanceData *last_instance;
  GTimer            *since_last_state;
  guint              tick;
  gboolean           had_tick_since_state_switch;

  GHashTable *objects;
  GPtrArray  *nonchildren;
  GPtrArray  *children;
  GHashTable *allocations;
  GHashTable *foreach_instances;
  GHashTable *state_instances;
  GHashTable *transition_instances;
  GHashTable *transition_closure_instances;
  GHashTable *track_transition_closure_instances;
  GPtrArray  *track_transitions;

  GPtrArray *bindings;
  GPtrArray *watches;

  BgeWdgtNotifier *reference_notifier;

  int              measure_minimum_width;
  int              measure_natural_width;
  int              measure_minimum_height;
  int              measure_natural_height;
  int              current_measure_for_size;
  BgeWdgtNotifier *measure_for_size_notifier;

  int              widget_width;
  int              widget_height;
  BgeWdgtNotifier *widget_width_notifier;
  BgeWdgtNotifier *widget_height_notifier;

  GTimer          *tick_time;
  BgeWdgtNotifier *tick_time_notifier;
};

G_DEFINE_FINAL_TYPE (BgeWdgtRenderer, bge_wdgt_renderer, GTK_TYPE_WIDGET);

enum
{
  RENDERER_PROP_0,

  RENDERER_PROP_SPEC,
  RENDERER_PROP_RESOURCE,
  RENDERER_PROP_STATE,
  RENDERER_PROP_REFERENCE,
  RENDERER_PROP_CHILD,

  LAST_RENDERER_PROP
};
static GParamSpec *renderer_props[LAST_RENDERER_PROP] = { 0 };

BGE_DEFINE_DATA (
    watch_setter,
    WatchSetter,
    {
      BgeWdgtRenderer   *self;
      StateData         *state;
      StateInstanceData *instance;
      ValueData         *dest;
      ValueData         *src;
    },
    BGE_RELEASE_DATA (state, state_data_unref);
    BGE_RELEASE_DATA (instance, state_instance_data_unref);
    BGE_RELEASE_DATA (dest, value_data_unref);
    BGE_RELEASE_DATA (src, value_data_unref))

BGE_DEFINE_DATA (
    allocation,
    Allocation,
    {
      ValueData *width;
      ValueData *height;
      ValueData *transform;
    },
    BGE_RELEASE_DATA (width, value_data_unref);
    BGE_RELEASE_DATA (height, value_data_unref);
    BGE_RELEASE_DATA (transform, value_data_unref))

typedef struct
{
  double velocity;
  double est_duration;
  double cache_damping_ratio;
  double cache_mass;
  double cache_stiffness;
} SpringCache;

BGE_DEFINE_DATA (
    transition_instance,
    TransitionInstance,
    {
      double           value;
      double           elapsed;
      BgeWdgtNotifier *notifier;
      struct
      {
        BgeEasing easing;
        double    seconds;
      } ease;
      SpringCache spring;
    },
    BGE_RELEASE_DATA (notifier, g_object_unref))

BGE_DEFINE_DATA (
    transition_closure,
    TransitionClosure,
    {
      StateData         *state;
      StateInstanceData *instance;
      ValueData         *value;
    },
    BGE_RELEASE_DATA (state, state_data_unref);
    BGE_RELEASE_DATA (instance, state_instance_data_unref);
    BGE_RELEASE_DATA (value, value_data_unref));

BGE_DEFINE_DATA (
    track_transition_closure,
    TrackTransitionClosure,
    {
      ValueData       *value;
      GTimer          *timer;
      BgeWdgtNotifier *notifier;
      double           elapsed;
      double           current;
      double           target;
      double           last;
      SpringCache      spring;
    },
    BGE_RELEASE_DATA (value, value_data_unref);
    BGE_RELEASE_DATA (notifier, g_object_unref);
    BGE_RELEASE_DATA (timer, g_timer_destroy));

static void
regenerate (BgeWdgtRenderer *self);

static void
spawn_nonchild (BgeWdgtRenderer *self,
                ValueData       *value);

static void
spawn_child (BgeWdgtRenderer *self,
             ValueData       *value,
             GtkBuilder      *dummy_builder);

static void
apply_state (BgeWdgtRenderer *self);

static GtkExpression *
ensure_expressions (BgeWdgtSpec *self,
                    ValueData   *value,
                    StateData   *state,
                    gboolean    *value_needs_tick);

static void
set_value (BgeWdgtRenderer   *self,
           StateData         *state,
           StateInstanceData *instance,
           ValueData         *dest,
           ValueData         *src,
           GPtrArray         *watches);

static int
resolve_value_int (BgeWdgtRenderer *self,
                   ValueData       *value,
                   StateData       *state);

static double
resolve_value_double (BgeWdgtRenderer *self,
                      ValueData       *value,
                      StateData       *state);

static gint
resolve_value_enum (BgeWdgtRenderer *self,
                    ValueData       *value,
                    StateData       *state);

static gpointer
resolve_value_boxed_dup (BgeWdgtRenderer *self,
                         ValueData       *value,
                         StateData       *state);

static gpointer
resolve_value_object_dup (BgeWdgtRenderer *self,
                          ValueData       *value,
                          StateData       *state);

static void
discard_binding (gpointer ptr);

static void
discard_watch (gpointer ptr);

static void
prop_change_queue_draw (BgeWdgtRenderer *self);

static void
reset_setter (WatchSetterData *data);

static void
wdgt_renderer_set_from_resource (BgeWdgtRenderer *self,
                                 const char      *resource);

static void
bge_wdgt_renderer_dispose (GObject *object)
{
  BgeWdgtRenderer *self = BGE_WDGT_RENDERER (object);

  if (self->tick > 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick);
      self->tick = 0;
    }

  g_clear_pointer (&self->spec, g_object_unref);
  g_clear_pointer (&self->state, g_free);
  g_clear_pointer (&self->reference, g_object_unref);
  g_clear_pointer (&self->child, gtk_widget_unparent);

  g_clear_pointer (&self->init_instance, state_instance_data_unref);
  g_clear_pointer (&self->init_watches, g_ptr_array_unref);

  g_clear_pointer (&self->active_state, state_data_unref);
  g_clear_pointer (&self->active_instance, state_instance_data_unref);
  g_clear_pointer (&self->active_snapshot, snapshot_data_unref);

  g_clear_pointer (&self->last_state, state_data_unref);
  g_clear_pointer (&self->last_instance, state_instance_data_unref);
  g_clear_pointer (&self->since_last_state, g_timer_destroy);

  g_clear_pointer (&self->objects, g_hash_table_unref);
  g_clear_pointer (&self->children, g_ptr_array_unref);
  g_clear_pointer (&self->allocations, g_hash_table_unref);
  g_clear_pointer (&self->nonchildren, g_ptr_array_unref);
  g_clear_pointer (&self->foreach_instances, g_hash_table_unref);
  g_clear_pointer (&self->state_instances, g_hash_table_unref);
  g_clear_pointer (&self->transition_instances, g_hash_table_unref);
  g_clear_pointer (&self->transition_closure_instances, g_hash_table_unref);
  g_clear_pointer (&self->track_transition_closure_instances, g_hash_table_unref);
  g_clear_pointer (&self->track_transitions, g_ptr_array_unref);

  g_clear_pointer (&self->bindings, g_ptr_array_unref);
  g_clear_pointer (&self->watches, g_ptr_array_unref);

  g_clear_pointer (&self->reference_notifier, g_object_unref);
  g_clear_pointer (&self->measure_for_size_notifier, g_object_unref);
  g_clear_pointer (&self->widget_width_notifier, g_object_unref);
  g_clear_pointer (&self->widget_height_notifier, g_object_unref);

  g_clear_pointer (&self->tick_time, g_timer_destroy);
  g_clear_pointer (&self->tick_time_notifier, g_object_unref);

  G_OBJECT_CLASS (bge_wdgt_renderer_parent_class)->dispose (object);
}

static void
bge_wdgt_renderer_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  BgeWdgtRenderer *self = BGE_WDGT_RENDERER (object);

  switch (prop_id)
    {
    case RENDERER_PROP_SPEC:
      g_value_set_object (value, bge_wdgt_renderer_get_spec (self));
      break;
    case RENDERER_PROP_STATE:
      g_value_set_string (value, bge_wdgt_renderer_get_state (self));
      break;
    case RENDERER_PROP_REFERENCE:
      g_value_set_object (value, bge_wdgt_renderer_get_reference (self));
      break;
    case RENDERER_PROP_CHILD:
      g_value_set_object (value, bge_wdgt_renderer_get_child (self));
      break;
    case RENDERER_PROP_RESOURCE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_renderer_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  BgeWdgtRenderer *self = BGE_WDGT_RENDERER (object);

  switch (prop_id)
    {
    case RENDERER_PROP_SPEC:
      bge_wdgt_renderer_set_spec (self, g_value_get_object (value));
      break;
    case RENDERER_PROP_RESOURCE:
      wdgt_renderer_set_from_resource (self, g_value_get_string (value));
      break;
    case RENDERER_PROP_STATE:
      bge_wdgt_renderer_set_state (self, g_value_get_string (value));
      break;
    case RENDERER_PROP_REFERENCE:
      bge_wdgt_renderer_set_reference (self, g_value_get_object (value));
      break;
    case RENDERER_PROP_CHILD:
      bge_wdgt_renderer_set_child (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bge_wdgt_renderer_measure (GtkWidget     *widget,
                           GtkOrientation orientation,
                           int            for_size,
                           int           *minimum,
                           int           *natural,
                           int           *minimum_baseline,
                           int           *natural_baseline)
{
  BgeWdgtRenderer *self          = BGE_WDGT_RENDERER (widget);
  gboolean         wdgt_measured = FALSE;

  self->current_measure_for_size = for_size;
  /* This will update our measurement values in self */
  g_object_notify_by_pspec (
      G_OBJECT (self->measure_for_size_notifier),
      notifier_props[NOTIFIER_PROP_VALUE]);

  switch (orientation)
    {
    case GTK_ORIENTATION_HORIZONTAL:
      if (self->measure_minimum_width > 0 ||
          self->measure_natural_width > 0)
        {
          *minimum      = self->measure_minimum_width;
          *natural      = self->measure_natural_width;
          wdgt_measured = TRUE;
        }
      break;
    case GTK_ORIENTATION_VERTICAL:
      if (self->measure_minimum_height > 0 ||
          self->measure_natural_height > 0)
        {
          *minimum      = self->measure_minimum_height;
          *natural      = self->measure_natural_height;
          wdgt_measured = TRUE;
        }
      break;
    default:
      g_assert_not_reached ();
    }

  if (!wdgt_measured)
    {
      GtkWidget *child = NULL;

      child = gtk_widget_get_first_child (GTK_WIDGET (self));
      if (child != NULL)
        gtk_widget_measure (
            GTK_WIDGET (child),
            orientation, for_size,
            minimum, natural,
            minimum_baseline, natural_baseline);
    }
}

static void
bge_wdgt_renderer_size_allocate (GtkWidget *widget,
                                 int        width,
                                 int        height,
                                 int        baseline)
{
  BgeWdgtRenderer *self = BGE_WDGT_RENDERER (widget);

  self->widget_width  = width;
  self->widget_height = height;
  g_object_notify_by_pspec (
      G_OBJECT (self->widget_width_notifier),
      notifier_props[NOTIFIER_PROP_VALUE]);
  g_object_notify_by_pspec (
      G_OBJECT (self->widget_height_notifier),
      notifier_props[NOTIFIER_PROP_VALUE]);

  if (self->child != NULL &&
      gtk_widget_should_layout (self->child))
    gtk_widget_allocate (self->child, width, height, baseline, NULL);

  for (guint i = 0; i < self->children->len; i++)
    {
      GtkWidget      *child              = NULL;
      AllocationData *allocation         = NULL;
      int             alloc_width        = width;
      int             alloc_height       = height;
      g_autoptr (GskTransform) transform = NULL;

      child      = g_ptr_array_index (self->children, i);
      allocation = g_hash_table_lookup (self->allocations, child);

      alloc_width = resolve_value_int (
          self,
          allocation->width,
          self->active_instance != NULL
              ? self->active_state
              : self->spec->init_state);
      alloc_height = resolve_value_int (
          self,
          allocation->height,
          self->active_state != NULL
              ? self->active_state
              : self->spec->init_state);
      transform = resolve_value_boxed_dup (
          self,
          allocation->transform,
          self->active_state != NULL
              ? self->active_state
              : self->spec->init_state);

      gtk_widget_allocate (
          child,
          MAX (alloc_width, 0.0),
          MAX (alloc_height, 0.0),
          baseline,
          g_steal_pointer (&transform));
    }
}

static gboolean
recurse_snapshot (BgeWdgtRenderer *self,
                  GtkSnapshot     *snapshot,
                  GPtrArray       *calls,
                  guint           *idx,
                  ForeachData     *foreach_context,
                  ForeachData     *parent_foreach,
                  gboolean         skip_foreach)
{
  gboolean             result           = FALSE;
  guint                start_idx        = 0;
  guint                n_iters          = 0;
  ForeachData         *next_foreach     = NULL;
  ForeachInstanceData *foreach_instance = NULL;
  g_autoptr (GListModel) model          = NULL;

  start_idx = *idx;

  if (foreach_context != NULL)
    {
      for (ForeachData *data = foreach_context->parent;
           data != NULL && data != parent_foreach;
           data = data->parent)
        {
          next_foreach    = foreach_context;
          foreach_context = data;
        }

      foreach_instance = g_hash_table_lookup (
          self->foreach_instances, foreach_context);
      g_assert (foreach_instance != NULL);

      model = resolve_value_object_dup (
          self,
          foreach_context->model,
          self->active_state);
      if (model != NULL)
        n_iters = g_list_model_get_n_items (model);
      else
        {
          n_iters      = 1;
          skip_foreach = TRUE;
        }
    }
  else
    n_iters = 1;

  for (guint i = 0; i < n_iters; i++)
    {
      if (foreach_instance != NULL)
        {
          foreach_instance->current_idx = i;

          g_object_notify_by_pspec (
              G_OBJECT (foreach_instance->notifier),
              notifier_props[NOTIFIER_PROP_VALUE]);
        }

      *idx = start_idx;

      if (next_foreach != NULL)
        {
          result = recurse_snapshot (
              self,
              snapshot,
              calls,
              idx,
              next_foreach,
              foreach_context,
              skip_foreach);
          if (!result)
            return FALSE;
        }

      for (; *idx < calls->len; (*idx)++)
        {
          SnapshotCallData *call = NULL;

          call = g_ptr_array_index (calls, *idx);

          if ((call->foreach_context != NULL ||
               parent_foreach != NULL) &&
              call->foreach_context == parent_foreach)
            break;
          else if (call->foreach_context != foreach_context)
            {
              result = recurse_snapshot (
                  self,
                  snapshot,
                  calls,
                  idx,
                  call->foreach_context,
                  foreach_context,
                  skip_foreach);
              if (!result)
                return FALSE;
            }
          else if (!skip_foreach)
            {
              switch (call->kind)
                {
                case BGE_WDGT_SNAPSHOT_INSTR_POP:
                  gtk_snapshot_pop (snapshot);
                  break;
                case BGE_WDGT_SNAPSHOT_INSTR_SAVE:
                  gtk_snapshot_save (snapshot);
                  break;
                case BGE_WDGT_SNAPSHOT_INSTR_RESTORE:
                  gtk_snapshot_restore (snapshot);
                  break;
                case BGE_WDGT_SNAPSHOT_INSTR_APPEND:
                case BGE_WDGT_SNAPSHOT_INSTR_PUSH:
                case BGE_WDGT_SNAPSHOT_INSTR_TRANSFORM:
                  {
                    GValue arg_values[ARGBUF_SIZE]  = { 0 };
                    guint  n_arg_values             = 0;
                    GValue rest_values[ARGBUF_SIZE] = { 0 };
                    guint  n_rest_values            = 0;

                    n_arg_values  = MIN (call->args->len, G_N_ELEMENTS (arg_values));
                    n_rest_values = MIN (call->rest->len, G_N_ELEMENTS (rest_values));

                    for (guint j = 0; j < n_arg_values; j++)
                      {
                        ValueData     *value      = NULL;
                        GtkExpression *expression = NULL;

                        value      = g_ptr_array_index (call->args, j);
                        expression = g_hash_table_lookup (self->active_state->expressions, value);
                        result     = gtk_expression_evaluate (expression, self, &arg_values[j]);
                        if (!result)
                          return FALSE;
                      }
                    for (guint j = 0; j < n_rest_values; j++)
                      {
                        ValueData     *value      = NULL;
                        GtkExpression *expression = NULL;

                        value      = g_ptr_array_index (call->rest, j);
                        expression = g_hash_table_lookup (self->active_state->expressions, value);
                        result     = gtk_expression_evaluate (expression, self, &rest_values[j]);
                        if (!result)
                          return FALSE;
                      }

                    call->func (snapshot, arg_values, rest_values, n_rest_values);

                    for (guint j = 0; j < n_rest_values; j++)
                      {
                        g_value_unset (&rest_values[j]);
                      }
                    for (guint j = 0; j < n_arg_values; j++)
                      {
                        g_value_unset (&arg_values[j]);
                      }
                  }
                  break;
                case BGE_WDGT_SNAPSHOT_INSTR_SNAPSHOT_CHILD:
                  {
                    GtkExpression *expression  = NULL;
                    GValue         child_value = G_VALUE_INIT;
                    GtkWidget     *child       = NULL;

                    expression = g_hash_table_lookup (
                        self->active_state->expressions,
                        call->child);
                    gtk_expression_evaluate (expression, self, &child_value);

                    child = g_value_get_object (&child_value);
                    if (child != NULL)
                      {
                        if (gtk_widget_get_parent (child) == GTK_WIDGET (self))
                          gtk_widget_snapshot_child (GTK_WIDGET (self), child, snapshot);
                        else
                          g_critical ("Trying to snapshot a widget which is "
                                      "not a direct child of this spec! Skipping");
                      }

                    g_value_unset (&child_value);
                  }
                  break;
                default:
                  break;
                }
            }
        }
    }

  return TRUE;
}

static void
bge_wdgt_renderer_snapshot (GtkWidget   *widget,
                            GtkSnapshot *snapshot)
{
  BgeWdgtRenderer *self     = BGE_WDGT_RENDERER (widget);
  GPtrArray       *calls    = NULL;
  guint            call_idx = 0;

  if (self->active_state == NULL ||
      self->active_instance == NULL ||
      self->active_snapshot == NULL)
    goto done;

  calls = self->active_snapshot->calls;
  recurse_snapshot (self, snapshot, calls, &call_idx, NULL, NULL, FALSE);

done:
  if (self->child != NULL)
    gtk_widget_snapshot_child (GTK_WIDGET (self), self->child, snapshot);

  // for (guint i = 0; i < self->children->len; i++)
  //   {
  //     GtkWidget *child = NULL;
  //
  //     child = g_ptr_array_index (self->children, i);
  //     gtk_widget_snapshot_child (GTK_WIDGET (self), child, snapshot);
  //   }
}

static void
bge_wdgt_renderer_class_init (BgeWdgtRendererClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = bge_wdgt_renderer_set_property;
  object_class->get_property = bge_wdgt_renderer_get_property;
  object_class->dispose      = bge_wdgt_renderer_dispose;

  renderer_props[RENDERER_PROP_SPEC] =
      g_param_spec_object (
          "spec",
          NULL, NULL,
          BGE_TYPE_WDGT_SPEC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  renderer_props[RENDERER_PROP_RESOURCE] =
      g_param_spec_string (
          "resource",
          NULL, NULL, NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  renderer_props[RENDERER_PROP_STATE] =
      g_param_spec_string (
          "state",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  renderer_props[RENDERER_PROP_REFERENCE] =
      g_param_spec_object (
          "reference",
          NULL, NULL,
          G_TYPE_OBJECT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  renderer_props[RENDERER_PROP_CHILD] =
      g_param_spec_object (
          "child",
          NULL, NULL,
          GTK_TYPE_WIDGET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_RENDERER_PROP, renderer_props);

  widget_class->measure       = bge_wdgt_renderer_measure;
  widget_class->size_allocate = bge_wdgt_renderer_size_allocate;
  widget_class->snapshot      = bge_wdgt_renderer_snapshot;

  g_type_ensure (BGE_TYPE_WDGT_NOTIFIER);
}

static gboolean
tick_cb (BgeWdgtRenderer *self,
         GdkFrameClock   *frame_clock,
         gpointer         user_data)
{
  gboolean       should_animate                 = FALSE;
  GHashTableIter iter                           = { 0 };
  gboolean       finished_all_state_transitions = TRUE;

  self->had_tick_since_state_switch = TRUE;

  g_object_notify_by_pspec (
      G_OBJECT (self->tick_time_notifier),
      notifier_props[NOTIFIER_PROP_VALUE]);

  should_animate = bge_should_animate (GTK_WIDGET (self));

  for (guint i = 0; i < self->track_transitions->len;)
    {
      TrackTransitionClosureData *data    = NULL;
      double                      elapsed = 0.0;

      data    = g_ptr_array_index (self->track_transitions, i);
      elapsed = g_timer_elapsed (data->timer, NULL);

      g_object_notify_by_pspec (
          G_OBJECT (data->notifier),
          notifier_props[NOTIFIER_PROP_VALUE]);

      if (should_animate &&
          elapsed <= data->spring.est_duration)
        i++;
      else
        g_ptr_array_remove_index (self->track_transitions, i);
    }

  if (should_animate &&
      self->spec != NULL &&
      self->active_state != NULL &&
      self->active_instance != NULL &&
      self->last_state != NULL &&
      self->last_instance != NULL)
    {
      double elapsed = 0.0;

      elapsed = g_timer_elapsed (self->since_last_state, NULL);

      g_hash_table_iter_init (&iter, self->active_state->transitions);
      for (;;)
        {
          ValueData              *value                    = NULL;
          TransitionData         *transition               = NULL;
          TransitionInstanceData *transition_instance      = NULL;
          TransitionInstanceData *init_transition_instance = NULL;

          if (!g_hash_table_iter_next (
                  &iter,
                  (gpointer *) &value,
                  (gpointer *) &transition))
            break;

          transition_instance = g_hash_table_lookup (self->active_instance->transitions, value);
          if (transition_instance != NULL)
            g_object_notify_by_pspec (
                G_OBJECT (transition_instance->notifier),
                notifier_props[NOTIFIER_PROP_VALUE]);
          init_transition_instance = g_hash_table_lookup (self->init_instance->transitions, value);
          if (init_transition_instance != NULL)
            g_object_notify_by_pspec (
                G_OBJECT (init_transition_instance->notifier),
                notifier_props[NOTIFIER_PROP_VALUE]);

          switch (transition->kind)
            {
            case TRANSITION_EASE:
              if (elapsed <= transition_instance->ease.seconds)
                finished_all_state_transitions = FALSE;
              break;
            case TRANSITION_SPRING:
              if (transition_instance->spring.est_duration < 0.0 ||
                  elapsed <= transition_instance->spring.est_duration)
                finished_all_state_transitions = FALSE;
              break;
            default:
              g_assert_not_reached ();
            }
        }
    }

  if (finished_all_state_transitions)
    {
      g_clear_pointer (&self->last_state, state_data_unref);
      g_clear_pointer (&self->last_instance, state_instance_data_unref);
    }

  if ((self->active_state == NULL ||
       !self->active_state->needs_tick) &&
      self->track_transitions->len == 0 &&
      finished_all_state_transitions)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (self), self->tick);
      self->tick = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
bge_wdgt_renderer_init (BgeWdgtRenderer *self)
{
  self->init_watches = g_ptr_array_new_with_free_func (discard_watch);

  self->objects = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, g_object_unref);
  self->children = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gtk_widget_unparent);
  self->nonchildren = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_object_unref);
  self->allocations = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      g_object_unref, allocation_data_unref);
  self->foreach_instances = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      foreach_data_unref, NULL);
  self->state_instances = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      state_data_unref, state_instance_data_unref);
  self->transition_instances = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      transition_data_unref, transition_instance_data_unref);
  self->transition_closure_instances = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      transition_data_unref, transition_closure_data_unref);
  self->track_transition_closure_instances = g_hash_table_new_full (
      g_direct_hash, g_direct_equal,
      value_data_unref, track_transition_closure_data_unref);
  self->track_transitions = g_ptr_array_new_with_free_func (
      track_transition_closure_data_unref);

  self->bindings = g_ptr_array_new_with_free_func (discard_binding);
  self->watches  = g_ptr_array_new_with_free_func (discard_watch);

  self->since_last_state = g_timer_new ();
  self->tick_time        = g_timer_new ();

  self->reference_notifier        = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
  self->measure_for_size_notifier = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
  self->widget_width_notifier     = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
  self->widget_height_notifier    = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
  self->tick_time_notifier        = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
}

BgeWdgtRenderer *
bge_wdgt_renderer_new (void)
{
  return g_object_new (BGE_TYPE_WDGT_RENDERER, NULL);
}

BgeWdgtSpec *
bge_wdgt_renderer_get_spec (BgeWdgtRenderer *self)
{
  g_return_val_if_fail (BGE_IS_WDGT_RENDERER (self), NULL);
  return self->spec;
}

const char *
bge_wdgt_renderer_get_state (BgeWdgtRenderer *self)
{
  g_return_val_if_fail (BGE_IS_WDGT_RENDERER (self), NULL);
  return self->state;
}

GObject *
bge_wdgt_renderer_get_reference (BgeWdgtRenderer *self)
{
  g_return_val_if_fail (BGE_IS_WDGT_RENDERER (self), NULL);
  return self->reference;
}

GtkWidget *
bge_wdgt_renderer_get_child (BgeWdgtRenderer *self)
{
  g_return_val_if_fail (BGE_IS_WDGT_RENDERER (self), NULL);
  return self->child;
}

void
bge_wdgt_renderer_set_spec (BgeWdgtRenderer *self,
                            BgeWdgtSpec     *spec)
{
  GBinding *name_binding = NULL;

  g_return_if_fail (BGE_IS_WDGT_RENDERER (self));
  g_return_if_fail (spec == NULL || BGE_IS_WDGT_SPEC (spec));

  if (spec == self->spec)
    return;

  g_clear_pointer (&self->spec, g_object_unref);
  if (spec != NULL)
    self->spec = g_object_ref (spec);

  regenerate (self);
  apply_state (self);

  if (spec != NULL)
    {
      name_binding = g_object_bind_property (spec, "name", self, "name", G_BINDING_SYNC_CREATE);
      g_ptr_array_add (self->bindings, g_object_ref (name_binding));
    }
  else
    gtk_widget_set_name (GTK_WIDGET (self), NULL);

  g_object_notify_by_pspec (G_OBJECT (self), renderer_props[RENDERER_PROP_SPEC]);
}

void
bge_wdgt_renderer_set_state (BgeWdgtRenderer *self,
                             const char      *state)
{
  g_return_if_fail (BGE_IS_WDGT_RENDERER (self));

  if (state == self->state || (state != NULL && self->state != NULL && g_strcmp0 (state, self->state) == 0))
    return;

  g_clear_pointer (&self->state, g_free);
  if (state != NULL)
    self->state = g_strdup (state);

  apply_state (self);

  g_object_notify_by_pspec (G_OBJECT (self), renderer_props[RENDERER_PROP_STATE]);
}

void
bge_wdgt_renderer_set_reference (BgeWdgtRenderer *self,
                                 GObject         *reference)
{
  g_return_if_fail (BGE_IS_WDGT_RENDERER (self));

  if (reference == self->reference)
    return;

  g_clear_pointer (&self->reference, g_object_unref);
  if (reference != NULL)
    self->reference = g_object_ref (reference);

  g_object_notify_by_pspec (G_OBJECT (self), renderer_props[RENDERER_PROP_REFERENCE]);
  g_object_notify_by_pspec (G_OBJECT (self->reference_notifier), notifier_props[NOTIFIER_PROP_VALUE]);
}

void
bge_wdgt_renderer_set_child (BgeWdgtRenderer *self,
                             GtkWidget       *child)
{
  g_return_if_fail (BGE_IS_WDGT_RENDERER (self));

  if (self->child == child)
    return;

  if (child != NULL)
    g_return_if_fail (gtk_widget_get_parent (child) == NULL);

  g_clear_pointer (&self->child, gtk_widget_unparent);
  self->child = child;

  if (child != NULL)
    gtk_widget_set_parent (child, GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), renderer_props[RENDERER_PROP_CHILD]);
}

void
bge_wdgt_renderer_set_state_take (BgeWdgtRenderer *self,
                                  char            *state)
{
  g_return_if_fail (BGE_IS_WDGT_RENDERER (self));

  if (state != NULL && self->state != NULL && g_strcmp0 (state, self->state) == 0)
    {
      g_free (state);
      return;
    }

  g_clear_pointer (&self->state, g_free);
  if (state != NULL)
    self->state = state;

  g_object_notify_by_pspec (G_OBJECT (self), renderer_props[RENDERER_PROP_STATE]);
}

gpointer
bge_wdgt_renderer_lookup_object (BgeWdgtRenderer *self,
                                 const char      *name)
{
  ValueData *value = NULL;

  g_return_val_if_fail (BGE_IS_WDGT_RENDERER (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (self->spec == NULL)
    return NULL;

  value = g_hash_table_lookup (self->spec->values, name);
  if (value == NULL)
    return NULL;

  if (!g_type_is_a (value->type, G_TYPE_OBJECT))
    return NULL;

  return resolve_value_object_dup (
      self, value,
      self->active_state != NULL
          ? self->active_state
          : self->spec->init_state);
}

static void
regenerate (BgeWdgtRenderer *self)
{
  BgeWdgtSpec *spec                    = self->spec;
  g_autoptr (GtkBuilder) dummy_builder = NULL;
  GHashTableIter state_iter            = { 0 };
  GHashTableIter setters_iter          = { 0 };

  g_clear_pointer (&self->init_instance, state_instance_data_unref);
  g_ptr_array_set_size (self->init_watches, 0);
  g_hash_table_remove_all (self->state_instances);
  g_hash_table_remove_all (self->objects);
  g_ptr_array_set_size (self->children, 0);
  g_ptr_array_set_size (self->nonchildren, 0);
  g_hash_table_remove_all (self->allocations);
  g_hash_table_remove_all (self->foreach_instances);
  g_ptr_array_set_size (self->track_transitions, 0);

  g_clear_pointer (&self->last_state, state_data_unref);
  g_clear_pointer (&self->last_instance, state_instance_data_unref);
  g_clear_pointer (&self->active_state, state_data_unref);
  g_clear_pointer (&self->active_instance, state_instance_data_unref);
  g_clear_pointer (&self->active_snapshot, snapshot_data_unref);
  g_ptr_array_set_size (self->bindings, 0);
  g_ptr_array_set_size (self->watches, 0);

  self->measure_minimum_width  = -1;
  self->measure_natural_width  = -1;
  self->measure_minimum_height = -1;
  self->measure_natural_height = -1;

  if (self->spec == NULL)
    return;

  for (guint i = 0; i < spec->foreaches->len; i++)
    {
      ForeachData *data                                = NULL;
      g_autoptr (ForeachInstanceData) foreach_instance = NULL;

      data = g_ptr_array_index (spec->foreaches, i);

      foreach_instance           = foreach_instance_data_new ();
      foreach_instance->notifier = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);

      g_hash_table_replace (self->foreach_instances,
                            foreach_data_ref (data),
                            foreach_instance_data_ref (foreach_instance));
    }

  for (guint i = 0; i < spec->nonchildren->len; i++)
    {
      ValueData *value = NULL;

      value = g_ptr_array_index (spec->nonchildren, i);
      spawn_nonchild (self, value);
    }
  dummy_builder = gtk_builder_new ();
  for (guint i = 0; i < spec->children->len; i++)
    {
      ValueData *value = NULL;

      value = g_ptr_array_index (spec->children, i);
      spawn_child (self, value, dummy_builder);
    }

  g_hash_table_iter_init (&state_iter, spec->states);
  for (;;)
    {
      char      *state_name                  = NULL;
      StateData *state                       = NULL;
      g_autoptr (StateInstanceData) instance = NULL;
      GHashTableIter transitions_iter        = { 0 };

      if (!g_hash_table_iter_next (
              &state_iter,
              (gpointer *) &state_name,
              (gpointer *) &state))
        break;

      instance              = state_instance_data_new ();
      instance->transitions = g_hash_table_new_full (
          g_direct_hash, g_direct_equal,
          value_data_unref, transition_instance_data_unref);
      g_hash_table_replace (self->state_instances,
                            state_data_ref (state),
                            state_instance_data_ref (instance));

      g_hash_table_iter_init (&transitions_iter, state->transitions);
      for (;;)
        {
          ValueData      *value                            = NULL;
          TransitionData *transition                       = NULL;
          g_autoptr (BgeWdgtNotifier) notifier_object      = NULL;
          g_autoptr (TransitionInstanceData) instance_data = NULL;
          g_autoptr (TransitionClosureData) closure_data   = NULL;

          if (!g_hash_table_iter_next (
                  &transitions_iter,
                  (gpointer *) &value,
                  (gpointer *) &transition))
            break;

          notifier_object = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);

          instance_data           = transition_instance_data_new ();
          instance_data->notifier = g_object_ref (notifier_object);
          switch (transition->kind)
            {
            case TRANSITION_EASE:
              instance_data->elapsed = G_MAXINT;
              break;
            case TRANSITION_SPRING:
              instance_data->spring.est_duration = -1.0;
              break;
            default:
              g_assert_not_reached ();
            }
          g_hash_table_replace (instance->transitions,
                                value_data_ref (value),
                                transition_instance_data_ref (instance_data));
          g_hash_table_replace (self->transition_instances,
                                transition_data_ref (transition),
                                transition_instance_data_ref (instance_data));

          closure_data           = transition_closure_data_new ();
          closure_data->state    = state_data_ref (state);
          closure_data->instance = state_instance_data_ref (instance);
          closure_data->value    = value_data_ref (value);
          g_hash_table_replace (self->transition_closure_instances,
                                transition_data_ref (transition),
                                transition_closure_data_ref (closure_data));
        }

      for (guint i = 0; i < spec->track_transition_values->len; i++)
        {
          ValueData *value                                    = NULL;
          g_autoptr (TrackTransitionClosureData) closure_data = NULL;

          value = g_ptr_array_index (spec->track_transition_values, i);

          closure_data           = track_transition_closure_data_new ();
          closure_data->value    = value_data_ref (value);
          closure_data->timer    = g_timer_new ();
          closure_data->notifier = g_object_new (BGE_TYPE_WDGT_NOTIFIER, NULL);
          g_hash_table_replace (self->track_transition_closure_instances,
                                value_data_ref (value),
                                track_transition_closure_data_ref (closure_data));
        }

      if (state == spec->init_state)
        self->init_instance = state_instance_data_ref (instance);
    }

  /* Separate to ensure `self->init_instance` has been set */

  if (!spec->ready)
    {
      g_hash_table_iter_init (&state_iter, spec->states);
      for (;;)
        {
          GHashTableIter value_iter = { 0 };
          char          *state_name = NULL;
          StateData     *state      = NULL;

          if (!g_hash_table_iter_next (
                  &state_iter,
                  (gpointer *) &state_name,
                  (gpointer *) &state))
            break;

          g_hash_table_iter_init (&value_iter, spec->values);
          for (;;)
            {
              char      *value_name                = NULL;
              ValueData *value                     = NULL;
              g_autoptr (GtkExpression) expression = NULL;

              if (!g_hash_table_iter_next (
                      &value_iter,
                      (gpointer *) &value_name,
                      (gpointer *) &value))
                break;

              expression = ensure_expressions (spec, value, state, NULL);
            }

          for (guint i = 0; i < spec->anon_values->len; i++)
            {
              ValueData *value                     = NULL;
              g_autoptr (GtkExpression) expression = NULL;

              value      = g_ptr_array_index (spec->anon_values, i);
              expression = ensure_expressions (spec, value, state, NULL);
            }

          for (guint i = 0; i < spec->foreaches->len; i++)
            {
              ForeachData *data = NULL;

              data = g_ptr_array_index (spec->foreaches, i);

              {
                g_autoptr (GtkExpression) expression =
                    ensure_expressions (spec, data->model, state, NULL);
              }

              g_hash_table_iter_init (&value_iter, data->values);
              for (;;)
                {
                  char      *value_name                = NULL;
                  ValueData *value                     = NULL;
                  g_autoptr (GtkExpression) expression = NULL;

                  if (!g_hash_table_iter_next (
                          &value_iter,
                          (gpointer *) &value_name,
                          (gpointer *) &value))
                    break;

                  expression = ensure_expressions (spec, value, state, NULL);
                }
            }
        }

      g_hash_table_iter_init (&state_iter, spec->states);
      for (;;)
        {
          char      *state_name     = NULL;
          StateData *state          = NULL;
          StateData *snapshot_state = NULL;

          if (!g_hash_table_iter_next (
                  &state_iter,
                  (gpointer *) &state_name,
                  (gpointer *) &state))
            break;

          g_hash_table_iter_init (
              &setters_iter,
              state->setters);
          for (;;)
            {
              ValueData *dest = NULL;
              ValueData *src  = NULL;

              if (!g_hash_table_iter_next (
                      &setters_iter,
                      (gpointer *) &dest,
                      (gpointer *) &src))
                break;

              if (g_hash_table_contains (state->values_need_tick, src))
                state->needs_tick = TRUE;
            }

          if (state->snapshot != NULL)
            snapshot_state = state;
          else
            /* If this state doesn't have snapshot instructions specified, fallback
               on using the init state */
            snapshot_state = self->spec->init_state;

          for (guint i = 0; i < snapshot_state->snapshot->calls->len; i++)
            {
              SnapshotCallData *call = NULL;

              call = g_ptr_array_index (snapshot_state->snapshot->calls, i);
              if (call->args == NULL)
                /* pop, save, restore... */
                continue;

              for (guint j = 0; j < call->args->len; j++)
                {
                  ValueData     *arg        = NULL;
                  gboolean       found      = FALSE;
                  GtkExpression *expression = NULL;

                  arg        = g_ptr_array_index (call->args, j);
                  expression = g_hash_table_lookup (state->expressions, arg);
                  g_assert (expression != NULL);

                  for (guint k = 0; k < state->snapshot_deps->len; k++)
                    {
                      GtkExpression *other = NULL;

                      other = g_ptr_array_index (state->snapshot_deps, k);
                      if (expression == other)
                        {
                          found = TRUE;
                          break;
                        }
                    }
                  if (found)
                    /* Ensure we don't duplicate deps */
                    continue;

                  if (g_hash_table_contains (state->values_need_tick, arg))
                    state->needs_tick = TRUE;

                  g_ptr_array_add (state->snapshot_deps,
                                   gtk_expression_ref (expression));
                }
            }
        }

      spec->ready = TRUE;
    }

  g_hash_table_iter_init (
      &setters_iter,
      self->spec->init_state->setters);
  for (;;)
    {
      ValueData *dest = NULL;
      ValueData *src  = NULL;

      if (!g_hash_table_iter_next (
              &setters_iter,
              (gpointer *) &dest,
              (gpointer *) &src))
        break;

      set_value (
          self,
          self->spec->init_state,
          self->init_instance,
          dest,
          src,
          self->init_watches);
    }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
spawn_nonchild (BgeWdgtRenderer *self,
                ValueData       *value)
{
  g_autoptr (GObject) object = NULL;

  g_assert (value->kind == VALUE_OBJECT);

  object = g_object_new (value->type, NULL);
  if (g_type_is_a (value->type, G_TYPE_INITIALLY_UNOWNED))
    g_object_ref_sink (object);

  g_hash_table_replace (self->objects,
                        value_data_ref (value),
                        g_object_ref (object));
  g_ptr_array_add (self->nonchildren, g_object_ref (object));
}

static void
spawn_child (BgeWdgtRenderer *self,
             ValueData       *value,
             GtkBuilder      *dummy_builder)
{
  GtkWidget *widget                     = NULL;
  g_autoptr (AllocationData) allocation = NULL;

  g_assert (value->kind == VALUE_CHILD);

  widget = g_object_new (
      value->type,
      "name", value->name,
      NULL);
  if (value->child.css_classes != NULL)
    {
      for (guint j = 0; j < value->child.css_classes->len; j++)
        {
          const char *class = NULL;

          class = g_ptr_array_index (value->child.css_classes, j);
          gtk_widget_add_css_class (widget, class);
        }
    }

  if (value->child.parent_widget != NULL)
    {
      GtkWidget *parent_widget           = NULL;
      g_autoptr (GtkBuilder) tmp_builder = NULL;

      parent_widget = g_hash_table_lookup (
          self->objects,
          value->child.parent_widget);
      g_assert (parent_widget != NULL);

      if (dummy_builder == NULL)
        tmp_builder = gtk_builder_new ();

      GTK_BUILDABLE_GET_IFACE (parent_widget)
          ->add_child (
              GTK_BUILDABLE (parent_widget),
              dummy_builder != NULL
                  ? dummy_builder
                  : tmp_builder,
              G_OBJECT (widget),
              value->child.builder_type);
    }
  else
    {
      gtk_widget_set_parent (widget, GTK_WIDGET (self));
      g_ptr_array_add (self->children, g_object_ref (widget));
    }

  g_hash_table_replace (self->objects,
                        value_data_ref (value),
                        g_object_ref (widget));

  allocation = allocation_data_new ();
  g_hash_table_replace (self->allocations,
                        g_object_ref (widget),
                        allocation_data_ref (allocation));
}

static void
apply_state (BgeWdgtRenderer *self)
{
  BgeWdgtSpec       *spec     = self->spec;
  StateData         *state    = NULL;
  StateInstanceData *instance = NULL;
  GHashTableIter     iter     = { 0 };

  if (self->had_tick_since_state_switch)
    {
      g_clear_pointer (&self->last_state, state_data_unref);
      g_clear_pointer (&self->last_instance, state_instance_data_unref);
      if (self->active_state != NULL &&
          self->active_instance != NULL)
        {
          self->last_state    = g_steal_pointer (&self->active_state);
          self->last_instance = g_steal_pointer (&self->active_instance);
        }
      g_timer_start (self->since_last_state);
      self->had_tick_since_state_switch = FALSE;
    }

  g_clear_pointer (&self->active_state, state_data_unref);
  g_clear_pointer (&self->active_instance, state_instance_data_unref);
  g_clear_pointer (&self->active_snapshot, snapshot_data_unref);

  g_ptr_array_set_size (self->bindings, 0);
  g_ptr_array_set_size (self->watches, 0);
  gtk_widget_queue_draw (GTK_WIDGET (self));

  if (self->spec == NULL ||
      self->state == NULL)
    return;

  state = g_hash_table_lookup (spec->states, self->state);
  if (state == NULL)
    {
      g_critical ("state \"%s\" doesn't exist on spec \"%s\"",
                  self->state, spec->name);
      return;
    }
  instance = g_hash_table_lookup (self->state_instances, state);
  g_assert (instance != NULL);

  self->active_state    = state_data_ref (state);
  self->active_instance = state_instance_data_ref (instance);

  if (state->snapshot != NULL)
    self->active_snapshot = snapshot_data_ref (state->snapshot);
  else if (self->spec->init_state->snapshot != NULL)
    self->active_snapshot = snapshot_data_ref (self->spec->init_state->snapshot);

  g_hash_table_iter_init (&iter, state->setters);
  for (;;)
    {
      ValueData              *dest                = NULL;
      ValueData              *src                 = NULL;
      TransitionInstanceData *transition_instance = NULL;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &dest,
              (gpointer *) &src))
        break;

      set_value (
          self,
          state,
          instance,
          dest,
          src,
          self->watches);

      transition_instance = g_hash_table_lookup (instance->transitions, dest);
      if (transition_instance != NULL)
        transition_instance->spring.est_duration = -1.0;
    }

  g_hash_table_iter_init (&iter, spec->init_state->setters);
  for (;;)
    {
      ValueData *dest = NULL;
      ValueData *src  = NULL;

      if (!g_hash_table_iter_next (
              &iter,
              (gpointer *) &dest,
              (gpointer *) &src))
        break;

      set_value (
          self,
          state,
          instance,
          dest,
          src,
          self->watches);
    }

  for (guint i = 0; i < state->snapshot_deps->len; i++)
    {
      GtkExpression      *expression = NULL;
      GtkExpressionWatch *watch      = NULL;

      expression = g_ptr_array_index (state->snapshot_deps, i);
      watch      = gtk_expression_watch (
          expression,
          self,
          (GtkExpressionNotify) prop_change_queue_draw,
          self, NULL);
      g_ptr_array_add (self->watches, watch);
    }

  gtk_widget_queue_allocate (GTK_WIDGET (self));
  gtk_widget_queue_draw (GTK_WIDGET (self));

  if (self->tick == 0)
    self->tick = gtk_widget_add_tick_callback (
        GTK_WIDGET (self),
        (GtkTickCallback) tick_cb,
        NULL, NULL);
}

static graphene_point_t *
expression_create_point (gpointer object,
                         double   x,
                         double   y,
                         gpointer user_data)
{
  graphene_point_t *point = NULL;

  point    = graphene_point_alloc ();
  point->x = x;
  point->y = y;

  return point;
}

static graphene_size_t *
expression_create_size (gpointer object,
                        double   width,
                        double   height,
                        gpointer user_data)
{
  graphene_size_t *size = NULL;

  size         = graphene_size_alloc ();
  size->width  = width;
  size->height = height;

  return size;
}

static graphene_point3d_t *
expression_create_point3d (gpointer object,
                           double   x,
                           double   y,
                           double   z,
                           gpointer user_data)
{
  graphene_point3d_t *point3d = NULL;

  point3d    = graphene_point3d_alloc ();
  point3d->x = x;
  point3d->y = y;
  point3d->z = z;

  return point3d;
}

static graphene_rect_t *
expression_create_rect (gpointer object,
                        double   x,
                        double   y,
                        double   width,
                        double   height,
                        gpointer user_data)
{
  graphene_rect_t *rect = NULL;

  rect              = graphene_rect_alloc ();
  rect->origin.x    = x;
  rect->origin.y    = y;
  rect->size.width  = width;
  rect->size.height = height;

  return rect;
}

static GskStroke *
expression_create_stroke (gpointer    object,
                          double      line_width,
                          GskLineCap  line_cap,
                          GskLineJoin line_join,
                          double      miter_limit,
                          gpointer    user_data)
{
  GskStroke *stroke = NULL;

  stroke = gsk_stroke_new (line_width);
  gsk_stroke_set_line_cap (stroke, line_cap);
  gsk_stroke_set_line_join (stroke, line_join);
  gsk_stroke_set_miter_limit (stroke, miter_limit);

  return stroke;
}

static void
expression_coerce_type (gpointer      this,
                        GValue       *return_value,
                        guint         n_param_values,
                        const GValue *param_values,
                        gpointer      dest_type_ptr)
{
  GType dest_type = GPOINTER_TO_SIZE (dest_type_ptr);
  coerce_value (&param_values[0], dest_type, return_value);
}

static gpointer
expression_lookup_object (BgeWdgtRenderer *this,
                          ValueData       *value)
{
  return g_object_ref (g_hash_table_lookup (this->objects, value));
}

static gpointer
expression_get_reference_notifier (BgeWdgtRenderer *this,
                                   gpointer         user_data)
{
  return g_object_ref (this->reference_notifier);
}
static gpointer
expression_get_measure_for_size_notifier (BgeWdgtRenderer *this,
                                          gpointer         user_data)
{
  return g_object_ref (this->measure_for_size_notifier);
}
static gpointer
expression_get_widget_width_notifier (BgeWdgtRenderer *this,
                                      gpointer         user_data)
{
  return g_object_ref (this->widget_width_notifier);
}
static gpointer
expression_get_widget_height_notifier (BgeWdgtRenderer *this,
                                       gpointer         user_data)
{
  return g_object_ref (this->widget_height_notifier);
}
static gpointer
expression_get_tick_time_notifier (BgeWdgtRenderer *this,
                                   gpointer         user_data)
{
  return g_object_ref (this->tick_time_notifier);
}
static gpointer
expression_get_foreach_instance_notifier (BgeWdgtRenderer *this,
                                          ForeachData     *data)
{
  ForeachInstanceData *instance = NULL;

  instance = g_hash_table_lookup (this->foreach_instances, data);
  g_assert (instance != NULL);
  return g_object_ref (instance->notifier);
}
static gpointer
expression_get_transition_instance_notifier (BgeWdgtRenderer *this,
                                             TransitionData  *data)
{
  TransitionInstanceData *instance = NULL;

  instance = g_hash_table_lookup (this->transition_instances, data);
  g_assert (instance != NULL);
  return g_object_ref (instance->notifier);
}
static gpointer
expression_get_track_transition_closure_instance_notifier (BgeWdgtRenderer *this,
                                                           ValueData       *value)
{
  TrackTransitionClosureData *instance = NULL;

  instance = g_hash_table_lookup (this->track_transition_closure_instances, value);
  g_assert (instance != NULL);
  return g_object_ref (instance->notifier);
}

static guint
expression_get_foreach_index (BgeWdgtRenderer *this,
                              double           notify,
                              ForeachData     *data)
{
  ForeachInstanceData *instance = NULL;

  instance = g_hash_table_lookup (this->foreach_instances, data);
  g_assert (instance != NULL);
  return instance->current_idx;
}

static gpointer
expression_get_model_item (BgeWdgtRenderer *this,
                           GListModel      *model,
                           guint            idx,
                           double           notify,
                           gpointer         user_data)
{
  return g_list_model_get_item (model, idx);
}

static int
expression_get_measure_for_size (BgeWdgtRenderer *this,
                                 double           notify,
                                 gpointer         user_data)
{
  return this->current_measure_for_size;
}

static double
expression_get_widget_width (BgeWdgtRenderer *this,
                             double           notify,
                             gpointer         user_data)
{
  return (double) this->widget_width;
}

static double
expression_get_widget_height (BgeWdgtRenderer *this,
                              double           notify,
                              gpointer         user_data)
{
  return (double) this->widget_height;
}

static double
expression_get_tick_time (BgeWdgtRenderer *this,
                          double           notify,
                          gpointer         user_data)
{
  return g_timer_elapsed (this->tick_time, NULL);
}

static gpointer
expression_get_reference_object (BgeWdgtRenderer *this,
                                 double           notify,
                                 gpointer         user_data)
{
  return this->reference != NULL
             ? g_object_ref (this->reference)
             : NULL;
}

static double
expression_adjust_state_transition (BgeWdgtRenderer *this,
                                    double           in,
                                    double           notifier,
                                    TransitionData  *data)
{
  TransitionClosureData  *closure_data             = NULL;
  TransitionData         *transition               = NULL;
  TransitionData         *last_transition          = NULL;
  TransitionInstanceData *transition_instance      = NULL;
  TransitionInstanceData *last_transition_instance = NULL;
  ValueData              *last_in_value            = NULL;
  double                  last_in                  = 0.0;
  double                  elapsed                  = 0.0;
  double                  progress                 = 0.0;
  double                  interpolated_number      = 0.0;

  closure_data = g_hash_table_lookup (this->transition_closure_instances, data);
  g_assert (closure_data != NULL);
  g_assert (closure_data->value->type == G_TYPE_DOUBLE);

  if (!bge_should_animate (GTK_WIDGET (this)))
    return in;

  if (this->active_state == NULL ||
      this->active_instance == NULL ||
      this->last_state == NULL ||
      this->last_instance == NULL)
    return in;

  transition = g_hash_table_lookup (
      this->active_state->transitions, closure_data->value);
  if (transition == NULL)
    return in;
  last_transition = g_hash_table_lookup (
      this->last_state->transitions, closure_data->value);

  transition_instance = g_hash_table_lookup (
      this->active_instance->transitions, closure_data->value);
  g_assert (transition_instance != NULL);
  last_transition_instance = g_hash_table_lookup (
      this->last_instance->transitions, closure_data->value);

  if (last_transition != NULL &&
      last_transition_instance != NULL &&
      last_transition->kind == TRANSITION_EASE &&
      last_transition_instance->elapsed < last_transition_instance->ease.seconds)
    last_in = last_transition_instance->value;
  else
    {
      last_in_value = g_hash_table_lookup (this->last_state->setters, closure_data->value);
      if (last_in_value == NULL)
        return in;
      last_in = resolve_value_double (this, last_in_value, this->last_state);
    }

  elapsed                      = g_timer_elapsed (this->since_last_state, NULL);
  transition_instance->elapsed = elapsed;

  switch (transition->kind)
    {
    case TRANSITION_EASE:
      {
        BgeEasing easing  = 0;
        double    seconds = 0.0;

        easing = resolve_value_enum (
            this,
            transition->ease.easing,
            this->active_state);
        seconds = resolve_value_double (
            this,
            transition->ease.seconds,
            this->active_state);

        if (elapsed >= seconds)
          interpolated_number = in;
        else
          {
            progress            = bge_easing_ease (easing, elapsed / seconds);
            interpolated_number = last_in + progress * (in - last_in);
          }

        transition_instance->ease.easing  = easing;
        transition_instance->ease.seconds = seconds;
      }
      break;
    case TRANSITION_SPRING:
      {
        double damping_ratio = 0.0;
        double mass          = 0.0;
        double stiffness     = 0.0;
        double damping       = 0.0;

        damping_ratio = resolve_value_double (
            this,
            transition->spring.damping_ratio,
            this->active_state);
        mass = resolve_value_double (
            this,
            transition->spring.mass,
            this->active_state);
        stiffness = resolve_value_double (
            this,
            transition->spring.stiffness,
            this->active_state);

        damping = damping_ratio *
                  (/* critical damping */
                   2 * sqrt (mass * stiffness));

        if (transition_instance->spring.est_duration < 0.0 ||
            damping_ratio != transition_instance->spring.cache_damping_ratio ||
            mass != transition_instance->spring.cache_mass ||
            stiffness != transition_instance->spring.cache_stiffness)
          {
            if (last_transition != NULL &&
                last_transition->kind == TRANSITION_SPRING)
              transition_instance->spring.velocity = last_transition_instance->spring.velocity;
            else
              transition_instance->spring.velocity = 0.0;

            transition_instance->spring.est_duration = spring_calculate_duration (
                damping,
                mass,
                stiffness,
                last_in,
                in,
                FALSE);

            transition_instance->spring.cache_damping_ratio = damping_ratio;
            transition_instance->spring.cache_mass          = mass;
            transition_instance->spring.cache_stiffness     = stiffness;
          }

        if (elapsed >= transition_instance->spring.est_duration)
          interpolated_number = in;
        else
          interpolated_number = spring_oscillate (
              damping,
              mass,
              stiffness,
              last_in,
              in,
              elapsed,
              &transition_instance->spring.velocity);
      }
      break;
    default:
      g_assert_not_reached ();
    }

  transition_instance->value = interpolated_number;
  return interpolated_number;
}

static double
expression_adjust_track_transition (BgeWdgtRenderer *this,
                                    double           in,
                                    double           damping_ratio,
                                    double           mass,
                                    double           stiffness,
                                    double           notifier,
                                    ValueData       *value)
{
  TrackTransitionClosureData *closure_data = NULL;
  guint                       idx          = 0;
  gboolean                    registered   = FALSE;
  double                      damping      = 0.0;

  g_assert (value->type == G_TYPE_DOUBLE);

  if (!bge_should_animate (GTK_WIDGET (this)))
    return in;

  closure_data = g_hash_table_lookup (this->track_transition_closure_instances, value);
  g_assert (closure_data != NULL);

  registered = g_ptr_array_find (this->track_transitions, closure_data, &idx);
  if (!registered)
    {
      g_ptr_array_add (this->track_transitions,
                       track_transition_closure_data_ref (closure_data));

      if (this->tick == 0)
        this->tick = gtk_widget_add_tick_callback (
            GTK_WIDGET (this),
            (GtkTickCallback) tick_cb,
            NULL, NULL);
    }

  damping = damping_ratio *
            (/* critical damping */
             2 * sqrt (mass * stiffness));

  if (!registered ||
      closure_data->target != in ||
      damping_ratio != closure_data->spring.cache_damping_ratio ||
      mass != closure_data->spring.cache_mass ||
      stiffness != closure_data->spring.cache_stiffness)
    {
      closure_data->target  = in;
      closure_data->last    = closure_data->current;
      closure_data->elapsed = 0.0;
      g_timer_start (closure_data->timer);

      closure_data->spring.est_duration = spring_calculate_duration (
          damping,
          mass,
          stiffness,
          closure_data->last,
          closure_data->target,
          FALSE);

      closure_data->spring.cache_damping_ratio = damping_ratio;
      closure_data->spring.cache_mass          = mass;
      closure_data->spring.cache_stiffness     = stiffness;
    }
  else
    closure_data->elapsed = g_timer_elapsed (closure_data->timer, NULL);

  if (closure_data->elapsed >= closure_data->spring.est_duration)
    {
      closure_data->current = closure_data->target;
      closure_data->last    = closure_data->target;
    }
  else
    closure_data->current = spring_oscillate (
        damping,
        mass,
        stiffness,
        closure_data->last,
        closure_data->target,
        closure_data->elapsed,
        &closure_data->spring.velocity);

  return closure_data->current;
}

static GskTransform *
expression_perform_transform (gpointer          this,
                              guint             n_param_values,
                              const GValue     *param_values,
                              TransformCallFunc func)
{
  return func (g_value_dup_boxed (&param_values[0]), param_values + 1);
}

static GskPath *
expression_perform_path_build (gpointer      this,
                               guint         n_param_values,
                               const GValue *param_values,
                               ValueData    *path_value)
{
  g_autoptr (GskPathBuilder) builder = NULL;
  const GValue *cursor               = 0;

  g_assert (path_value->kind == VALUE_PATH);

  builder = gsk_path_builder_new ();
  cursor  = param_values;

  for (guint i = 0; i < path_value->path.argss->len; i++)
    {
      PathBuilderCallFunc func = NULL;
      GPtrArray          *args = NULL;

      func = g_ptr_array_index (path_value->path.funcs, i);
      args = g_ptr_array_index (path_value->path.argss, i);

      func (builder, cursor);
      cursor += args->len;
    }

  return gsk_path_builder_to_path (builder);
}

static GtkExpression *
ensure_expressions (BgeWdgtSpec *self,
                    ValueData   *value,
                    StateData   *state,
                    gboolean    *value_needs_tick)
{
  GtkExpression *cached                = NULL;
  g_autoptr (GtkExpression) expression = NULL;
  TransitionData *transition           = NULL;
  gboolean        need_tick            = FALSE;

  cached = g_hash_table_lookup (state->expressions, value);
  if (cached != NULL)
    {
      if (value_needs_tick != NULL &&
          g_hash_table_contains (state->values_need_tick, value))
        *value_needs_tick = TRUE;
      return gtk_expression_ref (cached);
    }

  switch (value->kind)
    {
    case VALUE_CONSTANT:
      expression = gtk_constant_expression_new_for_value (&value->constant.value);
      break;
    case VALUE_OBJECT:
    case VALUE_CHILD:
      expression = gtk_cclosure_expression_new (
          value->type,
          bge_marshal_OBJECT__VOID,
          0, (GtkExpression *[]){},
          G_CALLBACK (expression_lookup_object),
          value_data_ref (value),
          value_data_unref_closure);
      break;
    case VALUE_REFERENCE_OBJECT:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_reference_notifier),
            NULL, NULL);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_OBJECT__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_reference_object),
            GSIZE_TO_POINTER (value->type),
            NULL);
      }
      break;
    case VALUE_ITERATOR:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_foreach_instance_notifier),
            foreach_data_ref (value->iterator.context),
            foreach_data_unref_closure);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            G_TYPE_UINT,
            bge_marshal_UINT__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_foreach_index),
            foreach_data_ref (value->iterator.context),
            foreach_data_unref_closure);
        if (value->type != G_TYPE_UINT)
          {
            GtkExpression *model_expression = NULL;

            g_assert (g_type_is_a (value->type, G_TYPE_OBJECT));

            model_expression = ensure_expressions (
                self,
                value->iterator.context->model,
                state,
                &need_tick);
            expression = gtk_cclosure_expression_new (
                value->type,
                bge_marshal_OBJECT__OBJECT_UINT_DOUBLE,
                3, (GtkExpression *[]){ model_expression, expression, notify_expression },
                G_CALLBACK (expression_get_model_item),
                NULL, NULL);
          }
      }
      break;
    case VALUE_VARIABLE:
    case VALUE_ALLOCATION_WIDTH:
    case VALUE_ALLOCATION_HEIGHT:
    case VALUE_ALLOCATION_TRANSFORM:
    case VALUE_MEASURE_MINIMUM_WIDTH:
    case VALUE_MEASURE_NATURAL_WIDTH:
    case VALUE_MEASURE_MINIMUM_HEIGHT:
    case VALUE_MEASURE_NATURAL_HEIGHT:
      {
        ValueData *holds = NULL;

        holds = g_hash_table_lookup (state->setters, value);
        if (holds == NULL)
          holds = g_hash_table_lookup (self->init_state->setters, value);

        if (holds != NULL)
          expression = ensure_expressions (
              self, holds, state, &need_tick);
        else
          {
            GValue empty_value = G_VALUE_INIT;

            g_value_init (&empty_value, value->type);
            expression = gtk_constant_expression_new_for_value (&empty_value);
            g_value_unset (&empty_value);
          }
      }
      break;
    case VALUE_MEASURE_FOR_SIZE:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_measure_for_size_notifier),
            NULL, NULL);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_INT__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_measure_for_size),
            GSIZE_TO_POINTER (value->type),
            NULL);
      }
      break;
    case VALUE_WIDGET_WIDTH:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_widget_width_notifier),
            NULL, NULL);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_DOUBLE__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_widget_width),
            GSIZE_TO_POINTER (value->type),
            NULL);
      }
      break;
    case VALUE_WIDGET_HEIGHT:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_widget_height_notifier),
            NULL, NULL);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_DOUBLE__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_widget_height),
            GSIZE_TO_POINTER (value->type),
            NULL);
      }
      break;
    case VALUE_TICK_TIME:
      {
        GtkExpression *notifier_constant = NULL;
        GtkExpression *notify_expression = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_tick_time_notifier),
            NULL, NULL);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_DOUBLE__DOUBLE,
            1, (GtkExpression *[]){ notify_expression },
            G_CALLBACK (expression_get_tick_time),
            GSIZE_TO_POINTER (value->type),
            NULL);

        need_tick = TRUE;
      }
      break;
    case VALUE_COERCION:
      {
        expression = ensure_expressions (
            self, value->coercion.value, state, &need_tick);
        expression = gtk_cclosure_expression_new (
            value->type,
            _marshal_DIRECT__ARGS_DIRECT,
            1, (GtkExpression *[]){ expression },
            G_CALLBACK (expression_coerce_type),
            GSIZE_TO_POINTER (value->type),
            NULL);
      }
      break;
    case VALUE_TRACK_TRANSITION:
      {
        g_autoptr (BgeWdgtNotifier) notifier_object = NULL;
        GtkExpression *notifier_constant            = NULL;
        GtkExpression *notify_expression            = NULL;
        GtkExpression *damping_ratio_expression     = NULL;
        GtkExpression *mass_expression              = NULL;
        GtkExpression *stiffness_expression         = NULL;

        notifier_constant = gtk_cclosure_expression_new (
            BGE_TYPE_WDGT_NOTIFIER,
            bge_marshal_OBJECT__VOID,
            0, (GtkExpression *[]){},
            G_CALLBACK (expression_get_track_transition_closure_instance_notifier),
            value_data_ref (value), value_data_unref_closure);
        notify_expression = gtk_property_expression_new_for_pspec (
            notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

        damping_ratio_expression = ensure_expressions (
            self,
            value->track_transition.spring.damping_ratio,
            state,
            &need_tick);
        mass_expression = ensure_expressions (
            self,
            value->track_transition.spring.mass,
            state,
            &need_tick);
        stiffness_expression = ensure_expressions (
            self,
            value->track_transition.spring.stiffness,
            state,
            &need_tick);

        expression = ensure_expressions (
            self,
            value->track_transition.src,
            state,
            &need_tick);
        expression = gtk_cclosure_expression_new (
            value->type,
            bge_marshal_DOUBLE__DOUBLE_DOUBLE_DOUBLE_DOUBLE_DOUBLE,
            5, (GtkExpression *[]){ expression, damping_ratio_expression, mass_expression, stiffness_expression, notify_expression },
            G_CALLBACK (expression_adjust_track_transition),
            value_data_ref (value),
            value_data_unref_closure);
      }
      break;
    case VALUE_COMPONENT:
      {
        g_autoptr (GPtrArray) params = NULL;
        GClosureMarshal marshal      = NULL;
        GCallback       callback     = NULL;

        params = g_ptr_array_new ();

        for (guint i = 0; i < value->component.params->len; i++)
          {
            ValueData *member                     = NULL;
            g_autoptr (GtkExpression) member_expr = NULL;

            member      = g_ptr_array_index (value->component.params, i);
            member_expr = ensure_expressions (self, member, state, &need_tick);
            g_ptr_array_add (params, g_steal_pointer (&member_expr));
          }

        if (value->type == GRAPHENE_TYPE_POINT)
          {
            marshal  = bge_marshal_BOXED__DOUBLE_DOUBLE;
            callback = G_CALLBACK (expression_create_point);
          }
        else if (value->type == GRAPHENE_TYPE_SIZE)
          {
            marshal  = bge_marshal_BOXED__DOUBLE_DOUBLE;
            callback = G_CALLBACK (expression_create_size);
          }
        else if (value->type == GRAPHENE_TYPE_POINT3D)
          {
            marshal  = bge_marshal_BOXED__DOUBLE_DOUBLE_DOUBLE;
            callback = G_CALLBACK (expression_create_point3d);
          }
        else if (value->type == GRAPHENE_TYPE_RECT)
          {
            marshal  = bge_marshal_BOXED__DOUBLE_DOUBLE_DOUBLE_DOUBLE;
            callback = G_CALLBACK (expression_create_rect);
          }
        else if (value->type == GSK_TYPE_STROKE)
          {
            marshal  = bge_marshal_BOXED__DOUBLE_ENUM_ENUM_DOUBLE;
            callback = G_CALLBACK (expression_create_stroke);
          }
        else
          g_assert_not_reached ();

        expression = gtk_cclosure_expression_new (
            value->type,
            marshal,
            params->len,
            (GtkExpression **) params->pdata,
            callback,
            self,
            NULL);
      }
      break;
    case VALUE_TRANSFORM:
      {
        g_autoptr (GPtrArray) params        = NULL;
        g_autoptr (GtkExpression) next_expr = NULL;

        params = g_ptr_array_new ();

        next_expr = ensure_expressions (self, value->transform.next, state, &need_tick);
        g_ptr_array_add (params, g_steal_pointer (&next_expr));

        for (guint i = 0; i < value->transform.args->len; i++)
          {
            ValueData *arg                     = NULL;
            g_autoptr (GtkExpression) arg_expr = NULL;

            arg      = g_ptr_array_index (value->transform.args, i);
            arg_expr = ensure_expressions (self, arg, state, &need_tick);
            g_ptr_array_add (params, g_steal_pointer (&arg_expr));
          }

        expression = gtk_cclosure_expression_new (
            value->type,
            _marshal_BOXED__ARGS_DIRECT,
            params->len,
            (GtkExpression **) params->pdata,
            G_CALLBACK (expression_perform_transform),
            value->transform.func,
            NULL);
      }
      break;
    case VALUE_PATH:
      {
        g_autoptr (GPtrArray) params        = NULL;
        g_autoptr (GtkExpression) next_expr = NULL;

        params = g_ptr_array_new ();

        g_assert (value->path.funcs->len == value->path.argss->len);
        for (guint i = 0; i < value->path.argss->len; i++)
          {
            GPtrArray *args = NULL;

            args = g_ptr_array_index (value->path.argss, i);
            for (guint j = 0; j < args->len; j++)
              {
                ValueData *arg                     = NULL;
                g_autoptr (GtkExpression) arg_expr = NULL;

                arg      = g_ptr_array_index (args, j);
                arg_expr = ensure_expressions (self, arg, state, &need_tick);
                g_ptr_array_add (params, g_steal_pointer (&arg_expr));
              }
          }

        expression = gtk_cclosure_expression_new (
            value->type,
            _marshal_BOXED__ARGS_DIRECT,
            params->len,
            (GtkExpression **) params->pdata,
            G_CALLBACK (expression_perform_path_build),
            value_data_ref (value),
            value_data_unref_closure);
      }
      break;
    case VALUE_CLOSURE:
      {
        g_autoptr (GPtrArray) params = NULL;

        params = g_ptr_array_new ();

        for (guint i = 0; i < value->closure.args->len; i++)
          {
            ValueData *arg                     = NULL;
            g_autoptr (GtkExpression) arg_expr = NULL;

            arg      = g_ptr_array_index (value->closure.args, i);
            arg_expr = ensure_expressions (self, arg, state, &need_tick);
            g_ptr_array_add (params, g_steal_pointer (&arg_expr));
          }

        expression = gtk_cclosure_expression_new (
            value->type,
            value->closure.marshal,
            params->len,
            (GtkExpression **) params->pdata,
            value->closure.func,
            value->closure.user_data,
            NULL);
      }
      break;
    case VALUE_PROPERTY:
      {
        g_autoptr (GtkExpression) object_expression = NULL;

        /* Mark subproperty values as dependencies as well */
        object_expression = ensure_expressions (
            self, value->property.object, state, &need_tick);
        expression = gtk_property_expression_new (
            value->property.object->type,
            g_steal_pointer (&object_expression),
            value->property.prop_name);
      }
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  transition = g_hash_table_lookup (state->transitions, value);
  if (value->type == G_TYPE_DOUBLE &&
      transition != NULL)
    {
      GtkExpression *notifier_constant = NULL;
      GtkExpression *notify_expression = NULL;

      notifier_constant = gtk_cclosure_expression_new (
          BGE_TYPE_WDGT_NOTIFIER,
          bge_marshal_OBJECT__VOID,
          0, (GtkExpression *[]){},
          G_CALLBACK (expression_get_transition_instance_notifier),
          transition_data_ref (transition),
          transition_data_unref_closure);
      notify_expression = gtk_property_expression_new_for_pspec (
          notifier_constant, notifier_props[NOTIFIER_PROP_VALUE]);

      expression = gtk_cclosure_expression_new (
          value->type,
          bge_marshal_DOUBLE__DOUBLE_DOUBLE,
          2, (GtkExpression *[]){ expression, notify_expression },
          G_CALLBACK (expression_adjust_state_transition),
          transition_data_ref (transition),
          transition_data_unref_closure);
    }

  g_hash_table_replace (
      state->expressions,
      value_data_ref (value),
      gtk_expression_ref (expression));

  if (need_tick)
    {
      /* We need to track this so we know not to remove the animation tick
         callback when transitions are not active */
      g_hash_table_replace (
          state->values_need_tick,
          value_data_ref (value),
          NULL);
      if (value_needs_tick != NULL)
        *value_needs_tick = TRUE;
    }

  return gtk_expression_ref (expression);
}

static void
set_value (BgeWdgtRenderer   *self,
           StateData         *state,
           StateInstanceData *instance,
           ValueData         *dest,
           ValueData         *src,
           GPtrArray         *watches)
{
  GtkExpression *src_expression  = NULL;
  GtkExpression *dest_expression = NULL;

  src_expression = g_hash_table_lookup (state->expressions, src);
  g_assert (src_expression != NULL);
  dest_expression = g_hash_table_lookup (state->expressions, dest);
  g_assert (dest_expression != NULL);

  switch (dest->kind)
    {
    case VALUE_PROPERTY:
      {
        GtkExpression *dest_obj_expression = NULL;
        GValue         dest_obj_resolved   = G_VALUE_INIT;
        GObject       *dest_obj            = NULL;

        dest_obj_expression = g_hash_table_lookup (state->expressions, dest->property.object);
        g_assert (dest_obj_expression != NULL);
        gtk_expression_evaluate (
            dest_obj_expression,
            self,
            &dest_obj_resolved);
        dest_obj = g_value_get_object (&dest_obj_resolved);

        if (dest_obj != NULL)
          {
            gboolean resolved     = FALSE;
            GValue   src_resolved = G_VALUE_INIT;

            resolved = gtk_expression_evaluate (
                src_expression,
                self,
                &src_resolved);
            if (resolved)
              g_object_set_property (
                  dest_obj,
                  dest->property.prop_name,
                  &src_resolved);
            g_value_unset (&src_resolved);
          }

        g_value_unset (&dest_obj_resolved);
      }
      break;
    case VALUE_ALLOCATION_WIDTH:
    case VALUE_ALLOCATION_HEIGHT:
    case VALUE_ALLOCATION_TRANSFORM:
      {
        GtkExpression *dest_widget_expression = NULL;
        GValue         dest_widget_resolved   = G_VALUE_INIT;
        GtkWidget     *dest_widget            = NULL;

        dest_widget_expression = g_hash_table_lookup (state->expressions, dest->allocation.widget);
        g_assert (dest_widget_expression != NULL);
        gtk_expression_evaluate (
            dest_widget_expression,
            self,
            &dest_widget_resolved);
        dest_widget = g_value_get_object (&dest_widget_resolved);

        if (dest_widget != NULL)
          {
            AllocationData *allocation = NULL;

            allocation = g_hash_table_lookup (self->allocations, dest_widget);
            if (allocation != NULL)
              {
                switch (dest->kind)
                  {
                  case VALUE_ALLOCATION_WIDTH:
                    g_clear_pointer (&allocation->width, value_data_unref);
                    allocation->width = value_data_ref (src);
                    break;
                  case VALUE_ALLOCATION_HEIGHT:
                    g_clear_pointer (&allocation->height, value_data_unref);
                    allocation->height = value_data_ref (src);
                    break;
                  case VALUE_ALLOCATION_TRANSFORM:
                    g_clear_pointer (&allocation->transform, value_data_unref);
                    allocation->transform = value_data_ref (src);
                    break;
                  case VALUE_CHILD:
                  case VALUE_CLOSURE:
                  case VALUE_COERCION:
                  case VALUE_COMPONENT:
                  case VALUE_CONSTANT:
                  case VALUE_ITERATOR:
                  case VALUE_MEASURE_FOR_SIZE:
                  case VALUE_MEASURE_MINIMUM_HEIGHT:
                  case VALUE_MEASURE_MINIMUM_WIDTH:
                  case VALUE_MEASURE_NATURAL_HEIGHT:
                  case VALUE_MEASURE_NATURAL_WIDTH:
                  case VALUE_OBJECT:
                  case VALUE_PATH:
                  case VALUE_PROPERTY:
                  case VALUE_REFERENCE_OBJECT:
                  case VALUE_TICK_TIME:
                  case VALUE_TRACK_TRANSITION:
                  case VALUE_TRANSFORM:
                  case VALUE_VARIABLE:
                  case VALUE_WIDGET_HEIGHT:
                  case VALUE_WIDGET_WIDTH:
                  default:
                    g_assert_not_reached ();
                  }

                gtk_widget_queue_allocate (GTK_WIDGET (self));
              }
          }

        g_value_unset (&dest_widget_resolved);
      }
      break;
    case VALUE_MEASURE_MINIMUM_WIDTH:
    case VALUE_MEASURE_NATURAL_WIDTH:
    case VALUE_MEASURE_MINIMUM_HEIGHT:
    case VALUE_MEASURE_NATURAL_HEIGHT:
      {
        GValue src_resolved = G_VALUE_INIT;

        gtk_expression_evaluate (
            src_expression,
            self,
            &src_resolved);

        switch (dest->kind)
          {
          case VALUE_MEASURE_MINIMUM_WIDTH:
            self->measure_minimum_width = g_value_get_int (&src_resolved);
            break;
          case VALUE_MEASURE_NATURAL_WIDTH:
            self->measure_natural_width = g_value_get_int (&src_resolved);
            break;
          case VALUE_MEASURE_MINIMUM_HEIGHT:
            self->measure_minimum_height = g_value_get_int (&src_resolved);
            break;
          case VALUE_MEASURE_NATURAL_HEIGHT:
            self->measure_natural_height = g_value_get_int (&src_resolved);
            break;
          case VALUE_ALLOCATION_HEIGHT:
          case VALUE_ALLOCATION_TRANSFORM:
          case VALUE_ALLOCATION_WIDTH:
          case VALUE_CHILD:
          case VALUE_CLOSURE:
          case VALUE_COERCION:
          case VALUE_COMPONENT:
          case VALUE_CONSTANT:
          case VALUE_ITERATOR:
          case VALUE_MEASURE_FOR_SIZE:
          case VALUE_OBJECT:
          case VALUE_PATH:
          case VALUE_PROPERTY:
          case VALUE_REFERENCE_OBJECT:
          case VALUE_TICK_TIME:
          case VALUE_TRACK_TRANSITION:
          case VALUE_TRANSFORM:
          case VALUE_VARIABLE:
          case VALUE_WIDGET_HEIGHT:
          case VALUE_WIDGET_WIDTH:
          default:
            g_assert_not_reached ();
          }

        g_value_unset (&src_resolved);

        gtk_widget_queue_resize (GTK_WIDGET (self));
      }
      break;
    case VALUE_CHILD:
    case VALUE_CLOSURE:
    case VALUE_COERCION:
    case VALUE_COMPONENT:
    case VALUE_CONSTANT:
    case VALUE_ITERATOR:
    case VALUE_MEASURE_FOR_SIZE:
    case VALUE_OBJECT:
    case VALUE_PATH:
    case VALUE_REFERENCE_OBJECT:
    case VALUE_TICK_TIME:
    case VALUE_TRACK_TRANSITION:
    case VALUE_TRANSFORM:
    case VALUE_VARIABLE:
    case VALUE_WIDGET_HEIGHT:
    case VALUE_WIDGET_WIDTH:
      break;
    default:
      g_assert_not_reached ();
    }

  if (watches != NULL)
    {
      g_autoptr (WatchSetterData) watch_data = NULL;
      GtkExpressionWatch *watch              = NULL;

      watch_data           = watch_setter_data_new ();
      watch_data->self     = self;
      watch_data->state    = state_data_ref (state);
      watch_data->instance = state_instance_data_ref (instance);
      watch_data->dest     = value_data_ref (dest);
      watch_data->src      = value_data_ref (src);

      watch = gtk_expression_watch (
          src_expression,
          self,
          (GtkExpressionNotify) reset_setter,
          watch_setter_data_ref (watch_data),
          watch_setter_data_unref);
      g_ptr_array_add (watches, watch);
    }
}

static int
resolve_value_int (BgeWdgtRenderer *self,
                   ValueData       *value,
                   StateData       *state)
{
  GtkExpression *expression = NULL;
  GValue         resolved   = G_VALUE_INIT;
  int            ret        = 0.0;

  expression = g_hash_table_lookup (state->expressions, value);
  g_assert (expression != NULL);
  gtk_expression_evaluate (
      expression,
      self,
      &resolved);
  ret = g_value_get_int (&resolved);
  g_value_unset (&resolved);

  return ret;
}

static double
resolve_value_double (BgeWdgtRenderer *self,
                      ValueData       *value,
                      StateData       *state)
{
  GtkExpression *expression = NULL;
  GValue         resolved   = G_VALUE_INIT;
  double         ret        = 0.0;

  expression = g_hash_table_lookup (state->expressions, value);
  g_assert (expression != NULL);
  gtk_expression_evaluate (
      expression,
      self,
      &resolved);
  ret = g_value_get_double (&resolved);
  g_value_unset (&resolved);

  return ret;
}

static gint
resolve_value_enum (BgeWdgtRenderer *self,
                    ValueData       *value,
                    StateData       *state)
{
  GtkExpression *expression = NULL;
  GValue         resolved   = G_VALUE_INIT;
  gint           ret        = 0;

  expression = g_hash_table_lookup (state->expressions, value);
  g_assert (expression != NULL);
  gtk_expression_evaluate (
      expression,
      self,
      &resolved);
  ret = g_value_get_enum (&resolved);
  g_value_unset (&resolved);

  return ret;
}

static gpointer
resolve_value_boxed_dup (BgeWdgtRenderer *self,
                         ValueData       *value,
                         StateData       *state)
{
  GtkExpression *expression = NULL;
  GValue         resolved   = G_VALUE_INIT;
  gpointer       ret        = NULL;

  expression = g_hash_table_lookup (state->expressions, value);
  g_assert (expression != NULL);
  gtk_expression_evaluate (
      expression,
      self,
      &resolved);
  ret = g_value_dup_boxed (&resolved);
  g_value_unset (&resolved);

  return ret;
}

static gpointer
resolve_value_object_dup (BgeWdgtRenderer *self,
                          ValueData       *value,
                          StateData       *state)
{
  GtkExpression *expression = NULL;
  GValue         resolved   = G_VALUE_INIT;
  gpointer       ret        = NULL;

  expression = g_hash_table_lookup (state->expressions, value);
  g_assert (expression != NULL);
  gtk_expression_evaluate (
      expression,
      self,
      &resolved);
  ret = g_value_dup_object (&resolved);
  g_value_unset (&resolved);

  return ret;
}

static void
discard_binding (gpointer ptr)
{
  GBinding *binding = ptr;

  g_binding_unbind (binding);
  g_object_unref (binding);
}

static void
discard_watch (gpointer ptr)
{
  GtkExpressionWatch *watch = ptr;

  /* unrefs for us */
  gtk_expression_watch_unwatch (watch);
}

static void
prop_change_queue_draw (BgeWdgtRenderer *self)
{
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
reset_setter (WatchSetterData *data)
{
  BgeWdgtRenderer *self = data->self;

  /* Active state watchers are "overlayed" over the permanent init state. Thus,
     if this is the init state and the active state also sets this value, we
     should avoid touching it */
  if (self->active_state != NULL /* ? */ &&
      data->state == self->spec->init_state &&
      g_hash_table_contains (
          self->active_state->setters,
          data->dest))
    return;

  set_value (
      data->self,
      data->state,
      data->instance,
      data->dest,
      data->src,
      NULL);
}

static GHashTable *renderer_resource_cache = NULL;

static void
cache_weak_notify (char    *resource,
                   GObject *where_the_object_was)
{
  g_hash_table_remove (renderer_resource_cache, resource);
  g_free (resource);
}

static void
wdgt_renderer_set_from_resource (BgeWdgtRenderer *self,
                                 const char      *resource)
{
  g_autoptr (GError) local_error = NULL;
  g_autoptr (GBytes) bytes       = NULL;
  BgeWdgtSpec  *cached           = NULL;
  gsize         buffer_size      = 0;
  gconstpointer buffer           = NULL;
  g_autoptr (BgeWdgtSpec) spec   = NULL;

  if (g_once_init_enter_pointer (&renderer_resource_cache))
    {
      GHashTable *tmp = NULL;

      tmp = g_hash_table_new_full (
          g_str_hash,
          g_str_equal,
          g_free,
          NULL);
      g_once_init_leave_pointer (&renderer_resource_cache, tmp);
    }

  cached = g_hash_table_lookup (
      renderer_resource_cache,
      resource);
  if (cached == NULL)
    {
      bytes = g_resources_lookup_data (
          resource,
          G_RESOURCE_LOOKUP_FLAGS_NONE,
          &local_error);
      if (bytes == NULL)
        {
          g_critical ("failed to set renderer spec from resource: %s",
                      local_error->message);
          bge_wdgt_renderer_set_spec (self, NULL);
          return;
        }

      buffer = g_bytes_get_data (bytes, &buffer_size);
      spec   = bge_wdgt_spec_new_for_string (buffer, &local_error);
      if (spec == NULL)
        {
          g_critical ("failed to set renderer spec from resource %s: %s",
                      resource, local_error->message);
          bge_wdgt_renderer_set_spec (self, NULL);
          return;
        }

      g_hash_table_replace (
          renderer_resource_cache,
          g_strdup (resource),
          spec);
      g_object_weak_ref (
          G_OBJECT (spec),
          (GWeakNotify) cache_weak_notify,
          g_strdup (resource));

      cached = spec;
    }

  bge_wdgt_renderer_set_spec (self, cached);
}

static void
_marshal_DIRECT__ARGS_DIRECT (GClosure                *closure,
                              GValue                  *return_value,
                              guint                    n_param_values,
                              const GValue            *param_values,
                              gpointer invocation_hint G_GNUC_UNUSED,
                              gpointer                 marshal_data)
{
  typedef void (*GMarshalFunc_DIRECT__ARGS_DIRECT) (gpointer      data1,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      data2);
  GCClosure                       *cc = (GCClosure *) closure;
  gpointer                         data1, data2;
  GMarshalFunc_DIRECT__ARGS_DIRECT callback;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values >= 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_DIRECT__ARGS_DIRECT) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            return_value,
            n_param_values - 1,
            param_values + 1,
            data2);
}

static void
_marshal_BOXED__ARGS_DIRECT (GClosure                *closure,
                             GValue                  *return_value,
                             guint                    n_param_values,
                             const GValue            *param_values,
                             gpointer invocation_hint G_GNUC_UNUSED,
                             gpointer                 marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__ARGS_DIRECT) (gpointer      data1,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      data2);
  GCClosure                      *cc = (GCClosure *) closure;
  gpointer                        data1, data2;
  GMarshalFunc_BOXED__ARGS_DIRECT callback;
  gpointer                        v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values >= 1);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_BOXED__ARGS_DIRECT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
                       n_param_values - 1,
                       param_values + 1,
                       data2);

  g_value_take_boxed (return_value, v_return);
}

/* End of bge-wdgt-spec.c */
