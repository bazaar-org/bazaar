/* bz-search-filter-popover.c
 *
 * Copyright 2026 Alexander Vanhee
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

#include "config.h"

#include <glib/gi18n.h>

#include "bz-application.h"
#include "bz-category-flags.h"
#include "bz-flathub-category.h"
#include "bz-flathub-state.h"
#include "bz-search-filter-popover.h"
#include "bz-state-info.h"

struct _BzSearchFilterPopover
{
  GtkPopover parent_instance;

  BzCategoryFlags selected_categories;
  gboolean        only_verified;
  gboolean        only_free;
  gboolean        only_non_eol;
  gboolean        only_mobile;
  gboolean        has_active_filters;
  gboolean        state_forced_verified;
  gboolean        state_forced_free;
  gboolean        state_forced_non_eol;

  AdwWrapBox *wrap_box;
  GtkWidget  *verified_button;
  GtkWidget  *free_button;
  GtkWidget  *non_eol_button;
  GtkWidget  *mobile_button;
};

G_DEFINE_FINAL_TYPE (BzSearchFilterPopover, bz_search_filter_popover, GTK_TYPE_POPOVER)

enum
{
  PROP_0,
  PROP_SELECTED_CATEGORIES,
  PROP_ONLY_VERIFIED,
  PROP_ONLY_FREE,
  PROP_ONLY_NON_EOL,
  PROP_ONLY_MOBILE,
  PROP_HAS_ACTIVE_FILTERS,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void update_has_active_filters (BzSearchFilterPopover *self);
static void apply_filter_button (BzSearchFilterPopover *self,
                                 guint                  prop,
                                 gboolean               value);
static void sync_from_state (BzSearchFilterPopover *self);
static void on_state_setting_changed (BzSearchFilterPopover *self,
                                      GParamSpec            *pspec,
                                      BzStateInfo           *state);
static void on_category_button_clicked (GtkButton *button,
                                        gpointer   user_data);
static void rebuild_category_buttons (BzSearchFilterPopover *self);
static void on_show (GtkPopover *popover,
                     gpointer    user_data);
static void on_filter_button_clicked (GtkButton *button,
                                      gpointer   user_data);

static void
bz_search_filter_popover_dispose (GObject *object)
{
  BzSearchFilterPopover *self  = BZ_SEARCH_FILTER_POPOVER (object);
  BzStateInfo           *state = NULL;

  state = bz_state_info_get_default ();

  if (state != NULL)
    g_signal_handlers_disconnect_by_data (state, self);

  gtk_widget_dispose_template (GTK_WIDGET (object), BZ_TYPE_SEARCH_FILTER_POPOVER);

  G_OBJECT_CLASS (bz_search_filter_popover_parent_class)->dispose (object);
}

static void
bz_search_filter_popover_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  BzSearchFilterPopover *self = BZ_SEARCH_FILTER_POPOVER (object);

  switch (prop_id)
    {
    case PROP_SELECTED_CATEGORIES:
      g_value_set_flags (value, self->selected_categories);
      break;
    case PROP_ONLY_VERIFIED:
      g_value_set_boolean (value, self->only_verified);
      break;
    case PROP_ONLY_FREE:
      g_value_set_boolean (value, self->only_free);
      break;
    case PROP_ONLY_NON_EOL:
      g_value_set_boolean (value, self->only_non_eol);
      break;
    case PROP_ONLY_MOBILE:
      g_value_set_boolean (value, self->only_mobile);
      break;
    case PROP_HAS_ACTIVE_FILTERS:
      g_value_set_boolean (value, self->has_active_filters);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_filter_popover_class_init (BzSearchFilterPopoverClass *klass)
{
  GObjectClass   *object_class = NULL;
  GtkWidgetClass *widget_class = NULL;

  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose      = bz_search_filter_popover_dispose;
  object_class->get_property = bz_search_filter_popover_get_property;

  props[PROP_SELECTED_CATEGORIES] =
      g_param_spec_flags (
          "selected-categories",
          NULL, NULL,
          BZ_TYPE_CATEGORY_FLAGS,
          BZ_CATEGORY_FLAGS_NONE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ONLY_VERIFIED] =
      g_param_spec_boolean (
          "only-verified",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ONLY_FREE] =
      g_param_spec_boolean (
          "only-free",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ONLY_NON_EOL] =
      g_param_spec_boolean (
          "only-non-eol",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ONLY_MOBILE] =
      g_param_spec_boolean (
          "only-mobile",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_HAS_ACTIVE_FILTERS] =
      g_param_spec_boolean (
          "has-active-filters",
          NULL, NULL,
          FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-search-filter-popover.ui");
  gtk_widget_class_bind_template_child (widget_class, BzSearchFilterPopover, wrap_box);
  gtk_widget_class_bind_template_child (widget_class, BzSearchFilterPopover, verified_button);
  gtk_widget_class_bind_template_child (widget_class, BzSearchFilterPopover, free_button);
  gtk_widget_class_bind_template_child (widget_class, BzSearchFilterPopover, non_eol_button);
  gtk_widget_class_bind_template_child (widget_class, BzSearchFilterPopover, mobile_button);
  gtk_widget_class_bind_template_callback (widget_class, on_filter_button_clicked);
}

static void
bz_search_filter_popover_init (BzSearchFilterPopover *self)
{
  BzStateInfo *state = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self, "show", G_CALLBACK (on_show), NULL);

  state = bz_state_info_get_default ();
  if (state != NULL)
    {
      g_signal_connect_swapped (state, "notify::show-only-verified",
                                G_CALLBACK (on_state_setting_changed), self);
      g_signal_connect_swapped (state, "notify::show-only-foss",
                                G_CALLBACK (on_state_setting_changed), self);
      g_signal_connect_swapped (state, "notify::hide-eol",
                                G_CALLBACK (on_state_setting_changed), self);
    }
}

GtkWidget *
bz_search_filter_popover_new (void)
{
  return g_object_new (BZ_TYPE_SEARCH_FILTER_POPOVER, NULL);
}

BzCategoryFlags
bz_search_filter_popover_get_selected_categories (BzSearchFilterPopover *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self), BZ_CATEGORY_FLAGS_NONE);
  return self->selected_categories;
}

gboolean
bz_search_filter_popover_get_only_verified (BzSearchFilterPopover *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self), FALSE);
  return self->only_verified;
}

gboolean
bz_search_filter_popover_get_only_free (BzSearchFilterPopover *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self), FALSE);
  return self->only_free;
}

gboolean
bz_search_filter_popover_get_only_non_eol (BzSearchFilterPopover *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self), FALSE);
  return self->only_non_eol;
}

gboolean
bz_search_filter_popover_get_only_mobile (BzSearchFilterPopover *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self), FALSE);
  return self->only_mobile;
}

void
bz_search_filter_popover_clear (BzSearchFilterPopover *self)
{
  GtkWidget *child = NULL;

  g_return_if_fail (BZ_IS_SEARCH_FILTER_POPOVER (self));

  if (!self->state_forced_verified)
    apply_filter_button (self, PROP_ONLY_VERIFIED, FALSE);
  if (!self->state_forced_free)
    apply_filter_button (self, PROP_ONLY_FREE, FALSE);
  if (!self->state_forced_non_eol)
    apply_filter_button (self, PROP_ONLY_NON_EOL, FALSE);
  apply_filter_button (self, PROP_ONLY_MOBILE, FALSE);

  for (child = gtk_widget_get_first_child (GTK_WIDGET (self->wrap_box));
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    gtk_widget_remove_css_class (child, "accent");

  if (self->selected_categories != BZ_CATEGORY_FLAGS_NONE)
    {
      self->selected_categories = BZ_CATEGORY_FLAGS_NONE;
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_CATEGORIES]);
      update_has_active_filters (self);
    }
}

static void
update_has_active_filters (BzSearchFilterPopover *self)
{
  gboolean active = FALSE;

  active = (self->only_verified && !self->state_forced_verified) ||
           (self->only_free && !self->state_forced_free) ||
           (self->only_non_eol && !self->state_forced_non_eol) ||
           self->only_mobile ||
           self->selected_categories != BZ_CATEGORY_FLAGS_NONE;

  if (self->has_active_filters == active)
    return;

  self->has_active_filters = active;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_ACTIVE_FILTERS]);
}

static void
apply_filter_button (BzSearchFilterPopover *self,
                     guint                  prop,
                     gboolean               value)
{
  struct
  {
    guint       prop;
    gboolean   *field;
    GtkWidget **button;
  } map[] = {
    { PROP_ONLY_VERIFIED, &self->only_verified, &self->verified_button },
    {     PROP_ONLY_FREE,     &self->only_free,     &self->free_button },
    {  PROP_ONLY_NON_EOL,  &self->only_non_eol,  &self->non_eol_button },
    {   PROP_ONLY_MOBILE,   &self->only_mobile,   &self->mobile_button },
  };

  for (guint i = 0; i < G_N_ELEMENTS (map); i++)
    {
      if (map[i].prop != prop)
        continue;

      *map[i].field = value;

      if (value)
        gtk_widget_add_css_class (*map[i].button, "accent");
      else
        gtk_widget_remove_css_class (*map[i].button, "accent");

      g_object_notify_by_pspec (G_OBJECT (self), props[prop]);
      update_has_active_filters (self);
      return;
    }
}

static void
sync_from_state (BzSearchFilterPopover *self)
{
  BzStateInfo *state          = NULL;
  gboolean     state_verified = FALSE;
  gboolean     state_free     = FALSE;
  gboolean     state_hide_eol = FALSE;

  state = bz_state_info_get_default ();
  if (state == NULL)
    return;

  g_object_get (state,
                "show-only-verified", &state_verified,
                "show-only-foss", &state_free,
                "hide-eol", &state_hide_eol,
                NULL);

  self->state_forced_verified = state_verified;
  self->state_forced_free     = state_free;
  self->state_forced_non_eol  = state_hide_eol;

  if (state_verified)
    apply_filter_button (self, PROP_ONLY_VERIFIED, TRUE);

  if (self->verified_button != NULL)
    gtk_widget_set_sensitive (self->verified_button, !state_verified);

  if (state_free)
    apply_filter_button (self, PROP_ONLY_FREE, TRUE);

  if (self->free_button != NULL)
    gtk_widget_set_sensitive (self->free_button, !state_free);

  if (state_hide_eol)
    apply_filter_button (self, PROP_ONLY_NON_EOL, TRUE);

  if (self->non_eol_button != NULL)
    gtk_widget_set_sensitive (self->non_eol_button, !state_hide_eol);
}

static void
on_state_setting_changed (BzSearchFilterPopover *self,
                          GParamSpec            *pspec,
                          BzStateInfo           *state)
{
  gboolean state_verified = FALSE;
  gboolean state_free     = FALSE;
  gboolean state_hide_eol = FALSE;

  g_object_get (state,
                "show-only-verified", &state_verified,
                "show-only-foss", &state_free,
                "hide-eol", &state_hide_eol,
                NULL);

  if (!state_verified)
    apply_filter_button (self, PROP_ONLY_VERIFIED, FALSE);

  if (!state_free)
    apply_filter_button (self, PROP_ONLY_FREE, FALSE);

  if (!state_hide_eol)
    apply_filter_button (self, PROP_ONLY_NON_EOL, FALSE);

  sync_from_state (self);
}

static void
on_filter_button_clicked (GtkButton *button,
                          gpointer   user_data)
{
  BzSearchFilterPopover *self = BZ_SEARCH_FILTER_POPOVER (user_data);
  const char            *name = NULL;

  name = gtk_widget_get_name (GTK_WIDGET (button));

  if (g_str_equal (name, "verified"))
    apply_filter_button (self, PROP_ONLY_VERIFIED, !self->only_verified);
  else if (g_str_equal (name, "free"))
    apply_filter_button (self, PROP_ONLY_FREE, !self->only_free);
  else if (g_str_equal (name, "non-eol"))
    apply_filter_button (self, PROP_ONLY_NON_EOL, !self->only_non_eol);
  else if (g_str_equal (name, "mobile"))
    apply_filter_button (self, PROP_ONLY_MOBILE, !self->only_mobile);
}

static void
on_category_button_clicked (GtkButton *button,
                            gpointer   user_data)
{
  BzSearchFilterPopover *self = BZ_SEARCH_FILTER_POPOVER (user_data);
  BzCategoryFlags        flag = BZ_CATEGORY_FLAGS_NONE;

  flag = (BzCategoryFlags) GPOINTER_TO_SIZE (
      g_object_get_data (G_OBJECT (button), "category-flag"));

  self->selected_categories ^= flag;

  if (self->selected_categories & flag)
    gtk_widget_add_css_class (GTK_WIDGET (button), "accent");
  else
    gtk_widget_remove_css_class (GTK_WIDGET (button), "accent");

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED_CATEGORIES]);
  update_has_active_filters (self);
}

static void
rebuild_category_buttons (BzSearchFilterPopover *self)
{
  g_autoptr (BzFlathubState) flathub = NULL;
  g_autoptr (GListModel) categories  = NULL;
  BzStateInfo *state                 = NULL;
  GtkWidget   *child                 = NULL;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->wrap_box))) != NULL)
    adw_wrap_box_remove (self->wrap_box, child);

  sync_from_state (self);

  state = bz_state_info_get_default ();
  if (state == NULL)
    return;

  g_object_get (state, "flathub", &flathub, NULL);
  if (flathub == NULL)
    return;

  g_object_get (flathub, "categories", &categories, NULL);
  if (categories == NULL)
    return;

  for (guint i = 0; i < g_list_model_get_n_items (categories); i++)
    {
      g_autoptr (BzFlathubCategory) category = NULL;
      const char     *name                   = NULL;
      const char     *label                  = NULL;
      const char     *icon                   = NULL;
      GtkWidget      *button                 = NULL;
      GtkWidget      *content                = NULL;
      BzCategoryFlags flag                   = BZ_CATEGORY_FLAGS_NONE;

      category = g_list_model_get_item (categories, i);

      if (!bz_flathub_category_get_is_xdg (category))
        continue;

      name  = bz_flathub_category_get_name (category);
      label = bz_flathub_category_get_short_name (category);
      icon  = bz_flathub_category_get_symbolic_icon_name (category);
      flag  = bz_category_flags_from_name (name);

      content = g_object_new (ADW_TYPE_BUTTON_CONTENT,
                              "label", label,
                              "icon-name", icon ? icon : "",
                              NULL);

      button = gtk_button_new ();
      gtk_button_set_child (GTK_BUTTON (button), content);
      gtk_accessible_update_property (GTK_ACCESSIBLE (button),
                                      GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                      -1);

      g_object_set_data (G_OBJECT (button), "category-flag",
                         GSIZE_TO_POINTER ((gsize) flag));

      if (self->selected_categories & flag)
        gtk_widget_add_css_class (button, "accent");

      g_signal_connect (button, "clicked",
                        G_CALLBACK (on_category_button_clicked), self);

      adw_wrap_box_append (self->wrap_box, button);
    }
}

static void
on_show (GtkPopover *popover,
         gpointer    user_data)
{
  rebuild_category_buttons (BZ_SEARCH_FILTER_POPOVER (popover));
}
