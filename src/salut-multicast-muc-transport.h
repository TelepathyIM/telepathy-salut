/*
 * salut-multicast-muc-transport.h - Header for SalutMulticastMucTransport
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

#ifndef __SALUT_MULTICAST_MUC_TRANSPORT_H__
#define __SALUT_MULTICAST_MUC_TRANSPORT_H__

#include <glib-object.h>
#include "salut-transport.h"

G_BEGIN_DECLS

typedef struct _SalutMulticastMucTransport SalutMulticastMucTransport;
typedef struct _SalutMulticastMucTransportClass SalutMulticastMucTransportClass;

struct _SalutMulticastMucTransportClass {
    SalutTransportClass parent_class;
};

struct _SalutMulticastMucTransport {
    SalutTransport parent;
};

SalutMulticastMucTransport *
salut_multicast_muc_transport_new(SalutConnection *connection, 
                                  const gchar *name, 
                                  GHashTable *parameters,
                                  GError **error);

const gchar **
salut_multicast_muc_transport_get_required_parameters(void);

GType salut_multicast_muc_transport_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_MULTICAST_MUC_TRANSPORT \
  (salut_multicast_muc_transport_get_type())
#define SALUT_MULTICAST_MUC_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MULTICAST_MUC_TRANSPORT, SalutMulticastMucTransport))
#define SALUT_MULTICAST_MUC_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MULTICAST_MUC_TRANSPORT, SalutMulticastMucTransportClass))
#define SALUT_IS_MULTICAST_MUC_TRANSPORT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MULTICAST_MUC_TRANSPORT))
#define SALUT_IS_MULTICAST_MUC_TRANSPORT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MULTICAST_MUC_TRANSPORT))
#define SALUT_MULTICAST_MUC_TRANSPORT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MULTICAST_MUC_TRANSPORT, SalutMulticastMucTransportClass))


G_END_DECLS

#endif /* #ifndef __SALUT_MULTICAST_MUC_TRANSPORT_H__*/
