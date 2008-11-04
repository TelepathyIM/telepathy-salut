/*
 * gibber-linklocal-transport.c - Source for GibberLLTransport
 * Copyright (C) 2006 Collabora Ltd.
 *   @author: Sjoerd Simons <sjoerd@luon.net>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "gibber-unix-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_NET
#include "gibber-debug.h"

/* Buffer size used for reading input */
#define BUFSIZE 1024

G_DEFINE_TYPE(GibberUnixTransport, gibber_unix_transport, \
    GIBBER_TYPE_FD_TRANSPORT)

GQuark
gibber_unix_transport_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gibber_unix_transport_error");

  return quark;
}

/* private structure */
typedef struct _GibberUnixTransportPrivate GibberUnixTransportPrivate;

struct _GibberUnixTransportPrivate
{
  gboolean incoming;
  gboolean dispose_has_run;
};

#define GIBBER_UNIX_TRANSPORT_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_UNIX_TRANSPORT, \
     GibberUnixTransportPrivate))

static void gibber_unix_transport_finalize (GObject *object);

static void
gibber_unix_transport_init (GibberUnixTransport *self)
{
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);
  priv->incoming = FALSE;
}

static void gibber_unix_transport_dispose (GObject *object);
static void
gibber_unix_transport_class_init (
    GibberUnixTransportClass *gibber_unix_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_unix_transport_class);

  g_type_class_add_private (gibber_unix_transport_class,
                            sizeof (GibberUnixTransportPrivate));

  object_class->dispose = gibber_unix_transport_dispose;
  object_class->finalize = gibber_unix_transport_finalize;
}

void
gibber_unix_transport_dispose (GObject *object)
{
  GibberUnixTransport *self = GIBBER_UNIX_TRANSPORT (object);
  GibberUnixTransportPrivate *priv = GIBBER_UNIX_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (gibber_unix_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_unix_transport_parent_class)->dispose (object);
}

void
gibber_unix_transport_finalize (GObject *object)
{
  G_OBJECT_CLASS (gibber_unix_transport_parent_class)->finalize (object);
}

GibberUnixTransport *
gibber_unix_transport_new (void)
{
  return g_object_new (GIBBER_TYPE_UNIX_TRANSPORT, NULL);
}
