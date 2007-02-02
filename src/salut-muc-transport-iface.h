/*
 * salut-muc-transport-iface.h - Headers for the muc transport interface
 *
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __SALUT_MUC_TRANSPORT_IFACE_H__
#define __SALUT_MUC_TRANSPORT_IFACE_H__

#include <glib-object.h>
#include <gibber/gibber-xmpp-node.h>

G_BEGIN_DECLS

#define SALUT_TYPE_MUC_TRANSPORT_IFACE salut_muc_transport_iface_get_type()

#define SALUT_MUC_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  SALUT_TYPE_MUC_TRANSPORT_IFACE, SalutMucTransportIface))

#define SALUT_MUC_TRANSPORT_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  SALUT_TYPE_MUC_TRANSPORT_IFACE, SalutMucTransportIfaceClass))

#define SALUT_IS_MUC_TRANSPORT_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  SALUT_TYPE_MUC_TRANSPORT_IFACE))

#define SALUT_IS_MUC_TRANSPORT_IFACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  SALUT_TYPE_MUC_TRANSPORT_IFACE))

#define SALUT_MUC_TRANSPORT_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), \
  SALUT_TYPE_MUC_TRANSPORT_IFACE, SalutMucTransportIfaceClass))

typedef struct _SalutMucTransportIface SalutMucTransportIface;
typedef struct _SalutMucTransportIfaceClass SalutMucTransportIfaceClass;

struct _SalutMucTransportIfaceClass {
  GTypeInterface parent_class;
  gboolean (*connect) (SalutMucTransportIface *iface, GError **error);
  const gchar *(*get_protocol)(SalutMucTransportIface *iface);
  const GHashTable *(*get_parameters) (SalutMucTransportIface *iface);
};

GType salut_muc_transport_iface_get_type (void);

/* Hash table contains the needed transport to join a channel, 
 * if parameters == NULL the transport needs to ``create'' a new channel */
gboolean salut_muc_transport_iface_connect(SalutMucTransportIface *iface, 
                                           GError **error);

const gchar *
salut_muc_transport_get_protocol(SalutMucTransportIface *iface);

/* Current parameters of the transport. str -> str */
const GHashTable *
salut_muc_transport_get_parameters(SalutMucTransportIface *iface);
G_END_DECLS

#endif /* __SALUT_MUC_TRANSPORT_IFACE_H__ */
