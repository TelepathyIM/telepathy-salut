/*
 * caps-channel-manager.h - interface holding capabilities functions for
 * channel managers
 *
 * Copyright (C) 2008 Collabora Ltd.
 * Copyright (C) 2008 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef SALUT_CAPS_CHANNEL_MANAGER_H
#define SALUT_CAPS_CHANNEL_MANAGER_H

#include <glib-object.h>
#include <gibber/gibber-xmpp-stanza.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/handle.h>

#include "salut-connection.h"

G_BEGIN_DECLS

#define SALUT_TYPE_CAPS_CHANNEL_MANAGER \
  (salut_caps_channel_manager_get_type ())

#define SALUT_CAPS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  SALUT_TYPE_CAPS_CHANNEL_MANAGER, SalutCapsChannelManager))

#define SALUT_IS_CAPS_CHANNEL_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  SALUT_TYPE_CAPS_CHANNEL_MANAGER))

#define SALUT_CAPS_CHANNEL_MANAGER_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  SALUT_TYPE_CAPS_CHANNEL_MANAGER, SalutCapsChannelManagerIface))

typedef struct _SalutCapsChannelManager SalutCapsChannelManager;
typedef struct _SalutCapsChannelManagerIface SalutCapsChannelManagerIface;


/* virtual methods */

/* May be moved to TpChannelManager later */
typedef void (*SalutCapsChannelManagerGetContactCapsFunc) (
    SalutCapsChannelManager *manager, SalutConnection *conn, TpHandle handle,
    GPtrArray *arr);

typedef void (*SalutCapsChannelManagerAddCapFunc) (
    SalutCapsChannelManager *manager, SalutConnection *conn, TpHandle handle,
    GHashTable *cap);

/* Specific to Salut */
typedef void (*SalutCapsChannelManagerGetFeatureListFunc) (
    SalutCapsChannelManager *manager, gpointer specific_caps,
    GSList **features);

typedef gpointer (*SalutCapsChannelManagerParseCapsFunc) (
    SalutCapsChannelManager *manager, GibberXmppNode *children);

typedef void (*SalutCapsChannelManagerFreeCapsFunc) (
    SalutCapsChannelManager *manager, gpointer specific_caps);

typedef void (*SalutCapsChannelManagerCopyCapsFunc) (
    SalutCapsChannelManager *manager, gpointer *specific_caps_out,
    gpointer specific_caps_in);

typedef void (*SalutCapsChannelManagerUpdateCapsFunc) (
    SalutCapsChannelManager *manager, gpointer *specific_caps_out,
    gpointer specific_caps_in);

typedef gboolean (*SalutCapsChannelManagerCapsDiffFunc) (
    SalutCapsChannelManager *manager, TpHandle handle,
    gpointer specific_old_caps, gpointer specific_new_caps);


void salut_caps_channel_manager_get_contact_capabilities (
    SalutCapsChannelManager *manager, SalutConnection *conn, TpHandle handle,
    GPtrArray *arr);

void salut_caps_channel_manager_get_feature_list (
    SalutCapsChannelManager *manager, gpointer specific_caps,
    GSList **features);

gpointer salut_caps_channel_manager_parse_capabilities (
    SalutCapsChannelManager *manager, GibberXmppNode *children);

void salut_caps_channel_manager_free_capabilities (SalutCapsChannelManager *manager,
    gpointer specific_caps);

void salut_caps_channel_manager_copy_capabilities (SalutCapsChannelManager *manager,
    gpointer *specific_caps_out, gpointer specific_caps_in);

void salut_caps_channel_manager_update_capabilities (
    SalutCapsChannelManager *manager, gpointer specific_caps_out,
    gpointer specific_caps_in);

gboolean salut_caps_channel_manager_capabilities_diff (
    SalutCapsChannelManager *manager, TpHandle handle,
    gpointer specific_old_caps, gpointer specific_new_caps);

void salut_caps_channel_manager_add_capability (
    SalutCapsChannelManager *manager, SalutConnection *conn, TpHandle handle,
    GHashTable *cap);


struct _SalutCapsChannelManagerIface {
    GTypeInterface parent;

    SalutCapsChannelManagerGetContactCapsFunc get_contact_caps;
    SalutCapsChannelManagerAddCapFunc add_cap;

    SalutCapsChannelManagerGetFeatureListFunc get_feature_list;
    SalutCapsChannelManagerParseCapsFunc parse_caps;
    SalutCapsChannelManagerFreeCapsFunc free_caps;
    SalutCapsChannelManagerCopyCapsFunc copy_caps;
    SalutCapsChannelManagerUpdateCapsFunc update_caps;
    SalutCapsChannelManagerCapsDiffFunc caps_diff;

    gpointer priv;
};

GType salut_caps_channel_manager_get_type (void);

G_END_DECLS

#endif
