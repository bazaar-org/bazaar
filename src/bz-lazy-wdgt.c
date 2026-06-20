/* bz-lazy-wdgt.c
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

/**
 * BzLazyWdgt:
 *
 * Initializing a BgeWdgtRenderer can be expensive, so this allows you to have a
 * placeholder widget until the state changes from a non-NULL value to another
 * non-NULL value, at which point a proper renderer will be swapped in.
 */

#include "bz-lazy-wdgt.h"

struct _BzLazyWdgt
{
  AdwBin parent_instance;

  BgeWdgtSpec *wdgt_spec;
  char        *wdgt_resource;
  char        *wdgt_state;
  GObject     *wdgt_reference;
  gboolean     activated;
};

G_DEFINE_FINAL_TYPE (BzLazyWdgt, bz_lazy_wdgt, ADW_TYPE_BIN);

enum
{
  PROP_0,

  PROP_WDGT_SPEC,
  PROP_WDGT_RESOURCE,
  PROP_WDGT_STATE,
  PROP_WDGT_REFERENCE,
  PROP_WDGT_ACTIVATED,

  LAST_PROP
};
static GParamSpec *props[LAST_PROP] = { 0 };

static void
maybe_activate (BzLazyWdgt *self);

static void
bz_lazy_wdgt_dispose (GObject *object)
{
  BzLazyWdgt *self = BZ_LAZY_WDGT (object);

  g_clear_pointer (&self->wdgt_spec, g_object_unref);
  g_clear_pointer (&self->wdgt_resource, g_free);
  g_clear_pointer (&self->wdgt_state, g_free);
  g_clear_pointer (&self->wdgt_reference, g_object_unref);

  G_OBJECT_CLASS (bz_lazy_wdgt_parent_class)->dispose (object);
}

static void
bz_lazy_wdgt_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BzLazyWdgt *self = BZ_LAZY_WDGT (object);

  switch (prop_id)
    {
    case PROP_WDGT_SPEC:
      g_value_set_object (value, bz_lazy_wdgt_get_wdgt_spec (self));
      break;
    case PROP_WDGT_RESOURCE:
      g_value_set_string (value, bz_lazy_wdgt_get_wdgt_resource (self));
      break;
    case PROP_WDGT_STATE:
      g_value_set_string (value, bz_lazy_wdgt_get_wdgt_state (self));
      break;
    case PROP_WDGT_REFERENCE:
      g_value_set_object (value, bz_lazy_wdgt_get_wdgt_reference (self));
      break;
    case PROP_WDGT_ACTIVATED:
      g_value_set_boolean (value, bz_lazy_wdgt_get_activated (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_lazy_wdgt_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BzLazyWdgt *self = BZ_LAZY_WDGT (object);

  switch (prop_id)
    {
    case PROP_WDGT_SPEC:
      bz_lazy_wdgt_set_wdgt_spec (self, g_value_get_object (value));
      break;
    case PROP_WDGT_RESOURCE:
      bz_lazy_wdgt_set_wdgt_resource (self, g_value_get_string (value));
      break;
    case PROP_WDGT_STATE:
      bz_lazy_wdgt_set_wdgt_state (self, g_value_get_string (value));
      break;
    case PROP_WDGT_REFERENCE:
      bz_lazy_wdgt_set_wdgt_reference (self, g_value_get_object (value));
      break;
    case PROP_WDGT_ACTIVATED:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bz_lazy_wdgt_class_init (BzLazyWdgtClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = bz_lazy_wdgt_set_property;
  object_class->get_property = bz_lazy_wdgt_get_property;
  object_class->dispose      = bz_lazy_wdgt_dispose;

  props[PROP_WDGT_SPEC] =
      g_param_spec_object (
          "wdgt-spec",
          NULL, NULL,
          BGE_TYPE_WDGT_SPEC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_WDGT_RESOURCE] =
      g_param_spec_string (
          "wdgt-resource",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_WDGT_STATE] =
      g_param_spec_string (
          "wdgt-state",
          NULL, NULL, NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_WDGT_REFERENCE] =
      g_param_spec_object (
          "wdgt-reference",
          NULL, NULL,
          G_TYPE_OBJECT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_WDGT_ACTIVATED] =
      g_param_spec_boolean (
          "activated",
          NULL, NULL, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
bz_lazy_wdgt_init (BzLazyWdgt *self)
{
}

BzLazyWdgt *
bz_lazy_wdgt_new (void)
{
  return g_object_new (BZ_TYPE_LAZY_WDGT, NULL);
}

BgeWdgtSpec *
bz_lazy_wdgt_get_wdgt_spec (BzLazyWdgt *self)
{
  g_return_val_if_fail (BZ_IS_LAZY_WDGT (self), NULL);
  return self->wdgt_spec;
}

const char *
bz_lazy_wdgt_get_wdgt_resource (BzLazyWdgt *self)
{
  g_return_val_if_fail (BZ_IS_LAZY_WDGT (self), NULL);
  return self->wdgt_resource;
}

const char *
bz_lazy_wdgt_get_wdgt_state (BzLazyWdgt *self)
{
  g_return_val_if_fail (BZ_IS_LAZY_WDGT (self), NULL);
  return self->wdgt_state;
}

GObject *
bz_lazy_wdgt_get_wdgt_reference (BzLazyWdgt *self)
{
  g_return_val_if_fail (BZ_IS_LAZY_WDGT (self), NULL);
  return self->wdgt_reference;
}

gboolean
bz_lazy_wdgt_get_activated (BzLazyWdgt *self)
{
  g_return_val_if_fail (BZ_IS_LAZY_WDGT (self), FALSE);
  return self->activated;
}

void
bz_lazy_wdgt_set_wdgt_spec (BzLazyWdgt  *self,
                            BgeWdgtSpec *wdgt_spec)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));
  g_return_if_fail (wdgt_spec == NULL || BGE_IS_WDGT_SPEC (wdgt_spec));

  if (wdgt_spec == self->wdgt_spec)
    return;

  g_clear_pointer (&self->wdgt_spec, g_object_unref);
  if (wdgt_spec != NULL)
    self->wdgt_spec = g_object_ref (wdgt_spec);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_SPEC]);
}

void
bz_lazy_wdgt_set_wdgt_resource (BzLazyWdgt *self,
                                const char *wdgt_resource)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));

  if (wdgt_resource == self->wdgt_resource || (wdgt_resource != NULL && self->wdgt_resource != NULL && g_strcmp0 (wdgt_resource, self->wdgt_resource) == 0))
    return;

  g_clear_pointer (&self->wdgt_resource, g_free);
  if (wdgt_resource != NULL)
    self->wdgt_resource = g_strdup (wdgt_resource);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_RESOURCE]);
}

void
bz_lazy_wdgt_set_wdgt_state (BzLazyWdgt *self,
                             const char *wdgt_state)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));

  if (wdgt_state == self->wdgt_state || (wdgt_state != NULL && self->wdgt_state != NULL && g_strcmp0 (wdgt_state, self->wdgt_state) == 0))
    return;

  g_clear_pointer (&self->wdgt_state, g_free);
  if (wdgt_state != NULL)
    {
      self->wdgt_state = g_strdup (wdgt_state);
      maybe_activate (self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_STATE]);
}

void
bz_lazy_wdgt_set_wdgt_reference (BzLazyWdgt *self,
                                 GObject    *wdgt_reference)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));
  g_return_if_fail (wdgt_reference == NULL || G_IS_OBJECT (wdgt_reference));

  if (wdgt_reference == self->wdgt_reference)
    return;

  g_clear_pointer (&self->wdgt_reference, g_object_unref);
  if (wdgt_reference != NULL)
    self->wdgt_reference = g_object_ref (wdgt_reference);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_REFERENCE]);
}

void
bz_lazy_wdgt_set_wdgt_resource_take (BzLazyWdgt *self,
                                     char       *wdgt_resource)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));

  if (wdgt_resource != NULL && self->wdgt_resource != NULL && g_strcmp0 (wdgt_resource, self->wdgt_resource) == 0)
    {
      g_free (wdgt_resource);
      return;
    }

  g_clear_pointer (&self->wdgt_resource, g_free);
  if (wdgt_resource != NULL)
    self->wdgt_resource = wdgt_resource;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_RESOURCE]);
}

void
bz_lazy_wdgt_set_wdgt_state_take (BzLazyWdgt *self,
                                  char       *wdgt_state)
{
  g_return_if_fail (BZ_IS_LAZY_WDGT (self));

  if (wdgt_state != NULL && self->wdgt_state != NULL && g_strcmp0 (wdgt_state, self->wdgt_state) == 0)
    {
      g_free (wdgt_state);
      return;
    }

  g_clear_pointer (&self->wdgt_state, g_free);
  if (wdgt_state != NULL)
    {
      self->wdgt_state = wdgt_state;
      maybe_activate (self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WDGT_STATE]);
}

static void
maybe_activate (BzLazyWdgt *self)
{
  BgeWdgtRenderer *renderer = NULL;

  if (self->activated)
    return;

  renderer = bge_wdgt_renderer_new ();
  g_object_bind_property (self, "wdgt-spec", renderer, "spec", G_BINDING_SYNC_CREATE);
  g_object_bind_property (self, "wdgt-resource", renderer, "resource", G_BINDING_SYNC_CREATE);
  g_object_bind_property (self, "wdgt-state", renderer, "state", G_BINDING_SYNC_CREATE);
  g_object_bind_property (self, "wdgt-reference", renderer, "reference", G_BINDING_SYNC_CREATE);

  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (renderer));
  self->activated = TRUE;
}

/* End of bz-lazy-wdgt.c */
