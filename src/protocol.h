/*
 * protocol.h - SalutProtocol
 * Copyright © 2007-2010 Collabora Ltd.
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

#ifndef SALUT_PROTOCOL_H
#define SALUT_PROTOCOL_H

#include <glib-object.h>
#include <telepathy-glib/base-protocol.h>

G_BEGIN_DECLS

typedef struct _SalutProtocol SalutProtocol;
typedef struct _SalutProtocolPrivate SalutProtocolPrivate;
typedef struct _SalutProtocolClass SalutProtocolClass;
typedef struct _SalutProtocolClassPrivate SalutProtocolClassPrivate;

struct _SalutProtocolClass {
    TpBaseProtocolClass parent_class;

    SalutProtocolClassPrivate *priv;
};

struct _SalutProtocol {
    TpBaseProtocol parent;

    SalutProtocolPrivate *priv;
};

GType salut_protocol_get_type (void);

#define SALUT_TYPE_PROTOCOL \
    (salut_protocol_get_type ())
#define SALUT_PROTOCOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
        SALUT_TYPE_PROTOCOL, \
        SalutProtocol))
#define SALUT_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
        SALUT_TYPE_PROTOCOL, \
        SalutProtocolClass))
#define GABBLE_IS_JABBER_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
        SALUT_TYPE_PROTOCOL))
#define SALUT_PROTOCOL_GET_CLASS(klass) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
        SALUT_TYPE_PROTOCOL, \
        SalutProtocolClass))

TpBaseProtocol *salut_protocol_new (GType backend_type);

G_END_DECLS

#endif