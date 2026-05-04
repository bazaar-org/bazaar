/* bz-list-tile.c
 *
 * Copyright 2025 Hari Rana <theevilskeleton@riseup.net>
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

#include "bz-list-tile.h"

typedef struct
{
  GtkWidget *child;
  gboolean   activatable;
} BzListTilePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (BzListTile, bz_list_tile, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_CHILD,
  PROP_ACTIVATABLE,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

enum
{
  ACTIVATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {
  0,
};

static void
on_gesture_click_released (BzListTile      *self,
                           gint             n_press,
                           gdouble          x,
                           gdouble          y,
                           GtkGestureClick *gesture);

static void
bz_list_tile_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzListTile        *self = BZ_LIST_TILE (object);
  BzListTilePrivate *priv = bz_list_tile_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, bz_list_tile_get_child (self));
      break;
    case PROP_ACTIVATABLE:
      g_value_set_boolean (value, priv->activatable);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_list_tile_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzListTile        *self = BZ_LIST_TILE (object);
  BzListTilePrivate *priv = bz_list_tile_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_CHILD:
      bz_list_tile_set_child (self, g_value_get_object (value));
      break;
    case PROP_ACTIVATABLE:
      priv->activatable = g_value_get_boolean (value);
      if (priv->activatable)
        gtk_widget_add_css_class (GTK_WIDGET (self), "activatable");
      else
        gtk_widget_remove_css_class (GTK_WIDGET (self), "activatable");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_list_tile_dispose (GObject *object)
{
  BzListTile        *self = BZ_LIST_TILE (object);
  BzListTilePrivate *priv = bz_list_tile_get_instance_private (self);

  g_clear_pointer (&priv->child, gtk_widget_unparent);

  G_OBJECT_CLASS (bz_list_tile_parent_class)->dispose (object);
}

static void
bz_list_tile_class_init (BzListTileClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = bz_list_tile_get_property;
  object_class->set_property = bz_list_tile_set_property;
  object_class->dispose      = bz_list_tile_dispose;

  /**
   * BzListTile:child:
   *
   * The child widget.
   */
  props[PROP_CHILD] =
      g_param_spec_object ("child", NULL, NULL,
                           GTK_TYPE_WIDGET,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * BzListTile:activatable:
   *
   * Whether the tile responds to click and emits the activated signal.
   */
  props[PROP_ACTIVATABLE] =
      g_param_spec_boolean ("activatable", NULL, NULL,
                            TRUE,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * BzListTile::activated:
   *
   * This signal is emitted after the tile has been activated.
   */
  signals[ACTIVATED] =
      g_signal_new ("activated",
                    G_TYPE_FROM_CLASS (klass),
                    G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                    0, NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE,
                    0);

  {
    g_autoptr (GtkShortcutAction) activate_action = NULL;
    const guint activate_keyvals[]                = {
      GDK_KEY_space,
      GDK_KEY_KP_Space,
      GDK_KEY_Return,
      GDK_KEY_ISO_Enter,
      GDK_KEY_KP_Enter,
    };

    activate_action = gtk_signal_action_new ("activated");

    for (size_t i = 0; i < G_N_ELEMENTS (activate_keyvals); i++)
      {
        g_autoptr (GtkShortcut) activate_shortcut = NULL;

        activate_shortcut = gtk_shortcut_new (gtk_keyval_trigger_new (activate_keyvals[i], 0),
                                              g_object_ref (activate_action));

        gtk_widget_class_add_shortcut (widget_class, activate_shortcut);
      }
  }

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
bz_list_tile_init (BzListTile *self)
{
  BzListTilePrivate *priv = bz_list_tile_get_instance_private (self);
  GtkGesture        *gesture_click;

  priv->activatable = TRUE;

  gtk_widget_add_css_class (GTK_WIDGET (self), "card");
  gtk_widget_add_css_class (GTK_WIDGET (self), "activatable");

  gesture_click = gtk_gesture_click_new ();

  g_signal_connect_swapped (gesture_click, "released", G_CALLBACK (on_gesture_click_released), self);

  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (gesture_click));

  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_set_receives_default (GTK_WIDGET (self), TRUE);
}

/**
 * bz_list_tile_new:
 *
 * Create a new #BzListTile.
 *
 * Returns: (transfer full): a newly created #BzListTile
 */
BzListTile *
bz_list_tile_new (void)
{
  return g_object_new (BZ_TYPE_LIST_TILE, NULL);
}

/**
 * bz_list_tile_get_child:
 * @self: a #BzListTile
 *
 * Get the child associated with @self.
 *
 * Returns: (transfer none) (nullable): the child
 */
GtkWidget *
bz_list_tile_get_child (BzListTile *self)
{
  BzListTilePrivate *priv;

  g_assert (BZ_IS_LIST_TILE (self));

  priv = bz_list_tile_get_instance_private (self);

  return priv->child;
}

/**
 * bz_list_tile_set_child:
 * @self: a #BzListTile
 * @child: (transfer none): the child
 *
 * Set the child to associate with @self.
 */
void
bz_list_tile_set_child (BzListTile *self,
                        GtkWidget  *child)
{
  BzListTilePrivate *priv;

  g_assert (BZ_IS_LIST_TILE (self));

  priv = bz_list_tile_get_instance_private (self);

  /* Since this is not a library, there is no point in safeguarding it */
  priv->child = child;
  gtk_widget_set_parent (child, GTK_WIDGET (self));
}

static void
on_gesture_click_released (BzListTile      *self,
                           gint             n_press,
                           gdouble          x,
                           gdouble          y,
                           GtkGestureClick *gesture)
{
  BzListTilePrivate *priv = bz_list_tile_get_instance_private (self);

  if (!priv->activatable)
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
      return;
    }

  if (gtk_widget_contains (GTK_WIDGET (self), x, y))
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);

      if (!gtk_widget_grab_focus (GTK_WIDGET (self)))
        g_assert_not_reached ();

      g_signal_emit (self, signals[ACTIVATED], 0);
    }
  else
    {
      gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
    }
}
