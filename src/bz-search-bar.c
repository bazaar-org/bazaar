/*
 * bz-search-bar.c
 *
 * Copyright 2026 Zelda Ahmed <zoeyahmed10@proton.me>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bz-search-bar.h"

struct _BzSearchBar
{
  GtkWidget parent_instance;

  GtkImage   *search_icon;
  GtkText    *search_text;
  AdwSpinner *search_busy;
  GtkButton  *clear_button;
  AdwBin     *end_widget_bin;

  gboolean busy;
  gchar   *placeholder;
};

static void bz_search_bar_accessible_init (GtkAccessibleInterface *iface);
static void bz_search_bar_editable_init (GtkEditableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (BzSearchBar, bz_search_bar, GTK_TYPE_WIDGET, G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE, bz_search_bar_editable_init) G_IMPLEMENT_INTERFACE (GTK_TYPE_ACCESSIBLE, bz_search_bar_accessible_init))

enum
{
  PROP_0,
  PROP_BUSY,
  PROP_PLACEHOLDER,
  PROP_END_WIDGET,
  N_PROPS
};

enum
{
  SIGNAL_SEARCH_ACTIVATED,
  SIGNAL_SEARCH_CHANGED,
  N_SIGNALS
};

static guint       signals[N_SIGNALS];
static GParamSpec *properties[N_PROPS];

static void
text_activated_cb (BzSearchBar *self,
                   GtkText     *text)
{
  g_signal_emit (self, signals[SIGNAL_SEARCH_ACTIVATED], 0);
}

static void
text_changed_cb (BzSearchBar *self,
                 GtkText     *text)
{
  g_signal_emit (self, signals[SIGNAL_SEARCH_CHANGED], 0);
}

static gboolean
has_text_cb (BzSearchBar *self,
             GtkButton   *clear_button,
             const gchar *value)
{
  const gchar *text;

  if (!self->search_text)
    return FALSE;

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_text));

  return strlen (text) != 0;
}

static void
clear_text_cb (BzSearchBar *self,
               GtkButton   *clear_button)
{
  gtk_editable_set_text (GTK_EDITABLE (self->search_text), "");
}

static gboolean
bz_search_bar_grab_focus (GtkWidget *widget)
{
  BzSearchBar *self = BZ_SEARCH_BAR (widget);

  return gtk_widget_grab_focus (GTK_WIDGET (self->search_text));
}

static gboolean
bz_search_bar_focus (GtkWidget *widget,
                     GtkDirectionType direction)
{
  BzSearchBar *self = BZ_SEARCH_BAR (widget);

  return gtk_widget_child_focus (GTK_WIDGET (self->search_text), direction);
}

static void
end_widget_focus_enter_cb (BzSearchBar *self)
{
  gtk_widget_add_css_class (GTK_WIDGET (self), "hide-focus");
}

static void
end_widget_focus_leave_cb (BzSearchBar *self)
{
  gtk_widget_remove_css_class (GTK_WIDGET (self), "hide-focus");
}

BzSearchBar *
bz_search_bar_new (void)
{
  return g_object_new (BZ_TYPE_SEARCH_BAR, NULL);
}

static void
bz_search_bar_dispose (GObject *object)
{
  BzSearchBar *self = BZ_SEARCH_BAR (object);

  gtk_editable_finish_delegate (GTK_EDITABLE (self));
  gtk_widget_dispose_template (GTK_WIDGET (self), BZ_TYPE_SEARCH_BAR);

  G_OBJECT_CLASS (bz_search_bar_parent_class)->dispose (object);
}

static void
bz_search_bar_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BzSearchBar *self = BZ_SEARCH_BAR (object);

  if (gtk_editable_delegate_get_property (object, prop_id, value, pspec))
    return;

  switch (prop_id)
    {
    case PROP_BUSY:
      g_value_set_boolean (value, bz_search_bar_get_busy (self));
      break;

    case PROP_END_WIDGET:
      g_value_set_object (value, bz_search_bar_get_end_widget (self));
      break;

    case PROP_PLACEHOLDER:
      g_value_set_string (value, bz_search_bar_get_placeholder (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_bar_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BzSearchBar *self = BZ_SEARCH_BAR (object);

  if (gtk_editable_delegate_set_property (object, prop_id, value, pspec))
    {
      if (prop_id == N_PROPS + GTK_EDITABLE_PROP_EDITABLE)
        {
          gtk_accessible_update_property (GTK_ACCESSIBLE (self->search_text),
                                          GTK_ACCESSIBLE_PROPERTY_READ_ONLY, !g_value_get_boolean (value),
                                          -1);
        }

      return;
    }

  switch (prop_id)
    {
    case PROP_BUSY:
      bz_search_bar_set_busy (self, g_value_get_boolean (value));
      break;

    case PROP_END_WIDGET:
      bz_search_bar_set_end_widget (self, GTK_WIDGET (g_value_get_object (value)));
      break;

    case PROP_PLACEHOLDER:
      bz_search_bar_set_placeholder (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_search_bar_class_init (BzSearchBarClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/Bazaar/bz-search-bar.ui");

  object_class->dispose      = bz_search_bar_dispose;
  object_class->get_property = bz_search_bar_get_property;
  object_class->set_property = bz_search_bar_set_property;

  widget_class->grab_focus = bz_search_bar_grab_focus;
  widget_class->focus = bz_search_bar_focus;

  properties[PROP_BUSY] =
      g_param_spec_boolean ("busy", NULL, NULL, FALSE,
                            G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  properties[PROP_END_WIDGET] =
      g_param_spec_object ("end-widget", NULL, NULL, GTK_TYPE_WIDGET,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  properties[PROP_PLACEHOLDER] =
      g_param_spec_string ("placeholder", NULL, NULL, "",
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  signals[SIGNAL_SEARCH_ACTIVATED] =
      g_signal_new (
          "search-activated",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL, NULL,
          G_TYPE_NONE, 0);

  signals[SIGNAL_SEARCH_CHANGED] =
      g_signal_new (
          "search-changed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_FIRST,
          0,
          NULL, NULL, NULL,
          G_TYPE_NONE, 0);

  g_object_class_install_properties (object_class, N_PROPS, properties);
  gtk_editable_install_properties (object_class, N_PROPS);

  gtk_widget_class_bind_template_child (widget_class, BzSearchBar, search_icon);
  gtk_widget_class_bind_template_child (widget_class, BzSearchBar, search_text);
  gtk_widget_class_bind_template_child (widget_class, BzSearchBar, search_busy);
  gtk_widget_class_bind_template_child (widget_class, BzSearchBar, clear_button);
  gtk_widget_class_bind_template_child (widget_class, BzSearchBar, end_widget_bin);

  gtk_widget_class_bind_template_callback (widget_class, text_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, has_text_cb);
  gtk_widget_class_bind_template_callback (widget_class, clear_text_cb);
  gtk_widget_class_bind_template_callback (widget_class, end_widget_focus_enter_cb);
  gtk_widget_class_bind_template_callback (widget_class, end_widget_focus_leave_cb);
}

static GtkEditable *
bz_search_bar_get_delegate (GtkEditable *editable)
{
  return GTK_EDITABLE (BZ_SEARCH_BAR (editable)->search_text);
}

static void
bz_search_bar_editable_init (GtkEditableInterface *iface)
{
  iface->get_delegate = bz_search_bar_get_delegate;
}

static gboolean
bz_search_bar_accessible_get_platform_state (GtkAccessible             *self,
                                             GtkAccessiblePlatformState state)
{
  return gtk_editable_delegate_get_accessible_platform_state (GTK_EDITABLE (self), state);
}

static void
bz_search_bar_accessible_init (GtkAccessibleInterface *iface)
{
  GtkAccessibleInterface *parent_iface = g_type_interface_peek_parent (iface);
  iface->get_at_context                = parent_iface->get_at_context;
  iface->get_platform_state            = bz_search_bar_accessible_get_platform_state;
}

static void
bz_search_bar_init (BzSearchBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_editable_init_delegate (GTK_EDITABLE (self));
}

gboolean
bz_search_bar_get_busy (BzSearchBar *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_BAR (self), FALSE);

  return self->busy;
}

void
bz_search_bar_set_busy (BzSearchBar *self,
                        gboolean     busy)
{
  g_return_if_fail (BZ_IS_SEARCH_BAR (self));

  if (self->busy == busy)
    return;

  self->busy = busy;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
}

GtkWidget *
bz_search_bar_get_end_widget (BzSearchBar *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_BAR (self), NULL);

  return adw_bin_get_child (self->end_widget_bin);
}

void
bz_search_bar_set_end_widget (BzSearchBar *self,
                              GtkWidget   *widget)
{
  GtkWidget *end_widget;

  g_return_if_fail (BZ_IS_SEARCH_BAR (self));
  g_return_if_fail (!widget || GTK_IS_WIDGET (widget));

  end_widget = adw_bin_get_child (self->end_widget_bin);

  if (end_widget == widget)
    return;

  adw_bin_set_child (self->end_widget_bin, widget);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END_WIDGET]);
}

const gchar *
bz_search_bar_get_placeholder (BzSearchBar *self)
{
  g_return_val_if_fail (BZ_IS_SEARCH_BAR (self), NULL);

  if (!self->search_text)
    return NULL;

  return gtk_text_get_placeholder_text (self->search_text);
}

void
bz_search_bar_set_placeholder (BzSearchBar *self,
                               const gchar *text)
{
  g_return_if_fail (BZ_IS_SEARCH_BAR (self));

  if (g_strcmp0 (bz_search_bar_get_placeholder (self), text) == 0)
    return;

  gtk_text_set_placeholder_text (self->search_text, text);
  gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                  GTK_ACCESSIBLE_PROPERTY_PLACEHOLDER, text,
                                  -1);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}
