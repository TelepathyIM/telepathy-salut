/*
 * gibber-xmpp-stanza.c - Source for GibberXmppStanza
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#include "gibber-xmpp-stanza.h"

G_DEFINE_TYPE(GibberXmppStanza, gibber_xmpp_stanza, G_TYPE_OBJECT)

/* private structure */
typedef struct _GibberXmppStanzaPrivate GibberXmppStanzaPrivate;

struct _GibberXmppStanzaPrivate
{
  gboolean dispose_has_run;
};

#define GIBBER_XMPP_STANZA_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_XMPP_STANZA, GibberXmppStanzaPrivate))

static void
gibber_xmpp_stanza_init (GibberXmppStanza *obj)
{
  /* allocate any data required by the object here */
  obj->node = NULL;
}

static void gibber_xmpp_stanza_dispose (GObject *object);
static void gibber_xmpp_stanza_finalize (GObject *object);

static void
gibber_xmpp_stanza_class_init (GibberXmppStanzaClass *gibber_xmpp_stanza_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_xmpp_stanza_class);

  g_type_class_add_private (gibber_xmpp_stanza_class, sizeof (GibberXmppStanzaPrivate));

  object_class->dispose = gibber_xmpp_stanza_dispose;
  object_class->finalize = gibber_xmpp_stanza_finalize;

}

void
gibber_xmpp_stanza_dispose (GObject *object)
{
  GibberXmppStanza *self = GIBBER_XMPP_STANZA (object);
  GibberXmppStanzaPrivate *priv = GIBBER_XMPP_STANZA_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_xmpp_stanza_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_xmpp_stanza_parent_class)->dispose (object);
}

void
gibber_xmpp_stanza_finalize (GObject *object)
{
  GibberXmppStanza *self = GIBBER_XMPP_STANZA (object);

  /* free any data held directly by the object here */
  gibber_xmpp_node_free(self->node);

  G_OBJECT_CLASS (gibber_xmpp_stanza_parent_class)->finalize (object);
}


GibberXmppStanza *
gibber_xmpp_stanza_new(gchar *name) {
  GibberXmppStanza *result;

  result = GIBBER_XMPP_STANZA(g_object_new(GIBBER_TYPE_XMPP_STANZA, NULL));
  result->node = gibber_xmpp_node_new(name); 

  return result;
}

