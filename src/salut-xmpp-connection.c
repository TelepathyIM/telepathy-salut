/*
 * salut-xmpp-connection.c - Source for SalutXmppConnection
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/xmlwriter.h>

#include "salut-xmpp-connection.h"
#include "salut-xmpp-connection-signals-marshal.h"
#include "salut-transport.h"
#include "salut-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

static void _xmpp_connection_received_data(SalutTransport *transport,
                                           const guint8 *data,
                                           gsize length,
                                           gpointer user_data);

G_DEFINE_TYPE(SalutXmppConnection, salut_xmpp_connection, G_TYPE_OBJECT)

/* signal enum */
enum
{
  STREAM_OPENED,
  STREAM_CLOSED,
  PARSE_ERROR,
  RECEIVED_STANZA,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutXmppConnectionPrivate SalutXmppConnectionPrivate;

static void _start_element_ns(void *user_data,
                              const xmlChar *localname,
                              const xmlChar *prefix,
                              const xmlChar *uri,
                              int nb_namespaces,
                              const xmlChar **namespaces,
                              int nb_attributes,
                              int nb_defaulted,
                              const xmlChar **attributes);

static void _end_element_ns(void *user_data, const xmlChar *localname,
                            const xmlChar *prefix, const xmlChar *URI);

static void _characters (void *user_data, const xmlChar *ch, int len);

static void _error(void *user_data, xmlErrorPtr error);

static xmlSAXHandler parser_handler = {
  .initialized = XML_SAX2_MAGIC,
  .startElementNs = _start_element_ns,
  .endElementNs   = _end_element_ns,
  .characters     = _characters,
  .serror         = _error,
};

struct _SalutXmppConnectionPrivate
{
  gboolean dispose_has_run;
  xmlParserCtxtPtr parser;
  guint depth;
  SalutXmppStanza *stanza;
  SalutXmppNode *node;
  GQueue *nodes;
};

#define SALUT_XMPP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_XMPP_CONNECTION, SalutXmppConnectionPrivate))

static void
salut_xmpp_connection_init (SalutXmppConnection *obj) {
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (obj);
  obj->transport = NULL;
  priv->parser = xmlCreatePushParserCtxt(&parser_handler, obj, NULL, 0, NULL);
  priv->depth = 0;
  priv->stanza = NULL;
  priv->nodes = g_queue_new();
  priv->node = NULL;
}

static void salut_xmpp_connection_dispose (GObject *object);
static void salut_xmpp_connection_finalize (GObject *object);

static void
salut_xmpp_connection_class_init (SalutXmppConnectionClass *salut_xmpp_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_xmpp_connection_class);

  g_type_class_add_private (salut_xmpp_connection_class, sizeof (SalutXmppConnectionPrivate));

  object_class->dispose = salut_xmpp_connection_dispose;
  object_class->finalize = salut_xmpp_connection_finalize;

  signals[STREAM_OPENED] = 
    g_signal_new("stream-opened", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 salut_xmpp_connection_marshal_VOID__STRING_STRING,
                 G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
  signals[STREAM_CLOSED] = 
    g_signal_new("stream-closed", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, SALUT_TYPE_XMPP_STANZA);
  signals[PARSE_ERROR] = 
    g_signal_new("parse-error", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_connection_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
salut_xmpp_connection_dispose (GObject *object)
{
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (object);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;
  if (self->transport != NULL) {
    g_object_unref(self->transport);
    self->transport = NULL;
  }

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->dispose (object);
}

void
salut_xmpp_connection_finalize (GObject *object) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (object);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);
  /* free any data held directly by the object here */
  if (priv->parser != NULL) {
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;
  }

  G_OBJECT_CLASS (salut_xmpp_connection_parent_class)->finalize (object);
}


SalutXmppConnection *
salut_xmpp_connection_new(SalutTransport *transport)  {
  SalutXmppConnection * result;

  result = g_object_new(SALUT_TYPE_XMPP_CONNECTION, NULL);
  result->transport = g_object_ref(transport);

  g_signal_connect(transport, "received",
                    G_CALLBACK(_xmpp_connection_received_data), result);
  return result;
}

void 
salut_xmpp_connection_open(SalutXmppConnection *connection,
                                const gchar *to, const gchar *from) {
#define XML_STREAM_INIT "<?xml version='1.0' encoding='UTF-8'?>\n"   \
                          "<stream:stream xmlns='jabber:client' "  \
                          "xmlns:stream='http://etherx.jabber.org/streams'>\n"
  salut_transport_send(connection->transport, 
                        (const guint8 *)XML_STREAM_INIT,
                        strlen(XML_STREAM_INIT), NULL);
}

void 
salut_xmpp_connection_close(SalutXmppConnection *connection) {
#define XML_STREAM_CLOSE "</stream:stream>\n"
  salut_transport_send(connection->transport, 
                        (const guint8 *)XML_STREAM_CLOSE,
                        strlen(XML_STREAM_CLOSE), NULL);
}

static void
_xml_write_node(xmlTextWriterPtr xmlwriter, SalutXmppNode *node);

gboolean
_write_attr(const gchar *key, const gchar *value, const gchar *ns,
            gpointer user_data) {
  xmlTextWriterPtr xmlwriter = (xmlTextWriterPtr)user_data;

  xmlTextWriterWriteAttribute(xmlwriter, (const xmlChar *)key, 
                                         (const xmlChar *)value);
  return TRUE;
}

gboolean 
_write_child(SalutXmppNode *node, gpointer user_data) {
  _xml_write_node((xmlTextWriterPtr) user_data, node);
  return TRUE;
}


static void
_xml_write_node(xmlTextWriterPtr xmlwriter, SalutXmppNode *node) {
  xmlTextWriterStartElement(xmlwriter, (const xmlChar*) node->name);

  salut_xmpp_node_each_attribute(node, _write_attr, xmlwriter);
  salut_xmpp_node_each_child(node, _write_child, xmlwriter);

  if (node->content) {
    xmlTextWriterWriteString(xmlwriter, (const xmlChar*)node->content);
  }
  xmlTextWriterEndElement(xmlwriter);
}

gboolean
salut_xmpp_connection_send(SalutXmppConnection *connection, 
                                SalutXmppStanza *stanza, GError **error) {
  xmlBufferPtr xmlbuffer;
  xmlTextWriterPtr xmlwriter;
  gboolean ret;

  xmlbuffer = xmlBufferCreate();
  xmlwriter = xmlNewTextWriterMemory(xmlbuffer, 0);

  xmlTextWriterSetIndent(xmlwriter, 1);

  _xml_write_node(xmlwriter, stanza->node);
  xmlTextWriterFlush(xmlwriter);

  ret = salut_transport_send(connection->transport, 
                              (const guint8 *)xmlbuffer->content,
                              xmlbuffer->use, error);
  xmlFreeTextWriter(xmlwriter);
  xmlBufferFree(xmlbuffer);

  return ret;
}

static void _start_element_ns(void *user_data,
                             const xmlChar *localname,
                             const xmlChar *prefix,
                             const xmlChar *uri,
                             int nb_namespaces,
                             const xmlChar **namespaces,
                             int nb_attributes,
                             int nb_defaulted,
                             const xmlChar **attributes) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);
  int i;

  if (G_UNLIKELY(priv->depth == 0)) {
    if (strcmp("stream", (gchar *)localname)
         || strcmp(XMPP_STREAM_NAMESPACE, (gchar *)uri)) {
      g_signal_emit(self, signals[PARSE_ERROR], 0); 
      return;
    }
    g_signal_emit(self, signals[STREAM_OPENED], 0, NULL, NULL);
    priv->depth++;
    return;
  } 
  if (priv->stanza == NULL) {
    priv->stanza = salut_xmpp_stanza_new((gchar *)localname);
    priv->node = priv->stanza->node;
  } else {
    g_queue_push_tail(priv->nodes, priv->node);
    priv->node = salut_xmpp_node_add_child_ns(priv->node, 
                                              (gchar *)localname, 
                                              (gchar *)uri);
  }
  for (i = 0; i < nb_attributes * 5; i+=5) {
    salut_xmpp_node_set_attribute_n(priv->node, 
                                    (gchar *)attributes[i], 
                                    (gchar *)attributes[i+3],
                                    (gsize)(attributes[i+4] - attributes[i+3]));
  }
  priv->depth++;
}

static void 
_characters (void *user_data, const xmlChar *ch, int len) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);

  if (priv->node != NULL) { 
    salut_xmpp_node_append_content_n(priv->node, (const gchar *)ch, (gsize)len);
  }
}

static void 
_end_element_ns(void *user_data, const xmlChar *localname, 
                const xmlChar *prefix, const xmlChar *uri) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);

  priv->depth--;
  if (priv->depth == 0) {
    g_signal_emit(self, signals[STREAM_CLOSED], 0);
  } else if (priv->depth == 1) {
    g_assert(g_queue_get_length(priv->nodes) == 0);
    g_signal_emit(self, signals[RECEIVED_STANZA], 0, priv->stanza);
    g_object_unref(priv->stanza);
    priv->stanza = NULL;
    priv->node = NULL;
  } else {
    priv->node = (SalutXmppNode *)g_queue_pop_tail(priv->nodes);
  }
}

static void 
_error(void *user_data, xmlErrorPtr error) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  g_signal_emit(self, signals[PARSE_ERROR], 0); 
}


static void 
_xmpp_connection_received_data(SalutTransport *transport,
                               const guint8 *data, gsize length,
                               gpointer user_data) {
  SalutXmppConnection *self = SALUT_XMPP_CONNECTION (user_data);
  SalutXmppConnectionPrivate *priv = SALUT_XMPP_CONNECTION_GET_PRIVATE (self);
  size_t ret;

  g_assert(length > 0);

  /* Temporarily ref myself to ensure we aren't disposed inside inside the xml
   * callbacks */
  g_object_ref(self);
  ret = xmlParseChunk(priv->parser, (const char*)data, length, FALSE);
  if (ret < 0) {
    g_signal_emit(self, signals[PARSE_ERROR], 0); 
  } 
  g_object_unref(self);
}
