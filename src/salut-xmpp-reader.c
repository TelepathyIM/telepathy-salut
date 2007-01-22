/*
 * salut-xmpp-reader.c - Source for SalutXmppReader
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
#include <string.h>

#include <libxml/parser.h>

#include "salut-xmpp-reader.h"
#include "salut-xmpp-reader-signals-marshal.h"

#include "salut-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

G_DEFINE_TYPE(SalutXmppReader, salut_xmpp_reader, G_TYPE_OBJECT)

/* signal enum */
enum {
  RECEIVED_STANZA,
  STREAM_OPENED,
  STREAM_CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* Parser prototypes */
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

/* private structure */
typedef struct _SalutXmppReaderPrivate SalutXmppReaderPrivate;

struct _SalutXmppReaderPrivate
{
  xmlParserCtxtPtr parser;
  guint depth;
  SalutXmppStanza *stanza;
  SalutXmppNode *node;
  GQueue *nodes;
  gboolean dispose_has_run;
  gboolean error;
};

#define SALUT_XMPP_READER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_XMPP_READER, SalutXmppReaderPrivate))

static void
salut_xmpp_reader_init (SalutXmppReader *obj)
{
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->parser = xmlCreatePushParserCtxt(&parser_handler, obj, NULL, 0, NULL);
  priv->depth = 0;
  priv->stanza = NULL;
  priv->nodes = g_queue_new();
  priv->node = NULL;
  priv->error = FALSE;
}

static void salut_xmpp_reader_dispose (GObject *object);
static void salut_xmpp_reader_finalize (GObject *object);

static void
salut_xmpp_reader_class_init (SalutXmppReaderClass *salut_xmpp_reader_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_xmpp_reader_class);

  g_type_class_add_private (salut_xmpp_reader_class, sizeof (SalutXmppReaderPrivate));

  object_class->dispose = salut_xmpp_reader_dispose;
  object_class->finalize = salut_xmpp_reader_finalize;

  signals[RECEIVED_STANZA] = 
    g_signal_new("received-stanza", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__OBJECT,
                 G_TYPE_NONE, 1, SALUT_TYPE_XMPP_STANZA);
  signals[STREAM_OPENED] = 
    g_signal_new("stream-opened", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 salut_xmpp_reader_marshal_VOID__STRING_STRING,
                 G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
  signals[STREAM_CLOSED] = 
    g_signal_new("stream-closed", 
                 G_OBJECT_CLASS_TYPE(salut_xmpp_reader_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                 0,
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_NONE, 0);
}

void
salut_xmpp_reader_dispose (GObject *object)
{
  SalutXmppReader *self = SALUT_XMPP_READER (object);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_xmpp_reader_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_reader_parent_class)->dispose (object);
}

void
salut_xmpp_reader_finalize (GObject *object)
{
  SalutXmppReader *self = SALUT_XMPP_READER (object);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  if (priv->parser != NULL) {
    xmlFreeParserCtxt(priv->parser);
    priv->parser = NULL;
  }

  G_OBJECT_CLASS (salut_xmpp_reader_parent_class)->finalize (object);
}


SalutXmppReader * 
salut_xmpp_reader_new(void) {
  return g_object_new(SALUT_TYPE_XMPP_READER, NULL);
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
  SalutXmppReader *self = SALUT_XMPP_READER (user_data);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);
  int i;

  if (G_UNLIKELY(priv->depth == 0)) {
    gchar *to = NULL;
    gchar *from = NULL;

    if (strcmp("stream", (gchar *)localname)
         || strcmp(XMPP_STREAM_NAMESPACE, (gchar *)uri)) {
      priv->error = TRUE;
      g_assert_not_reached();
      return;
    }
    for (i = 0; i < nb_attributes * 5; i+=5) {
      if (!strcmp((gchar *)attributes[i], "to")) {
        to = g_strndup((gchar *)attributes[i+3],
                         (gsize) (attributes[i+4] - attributes[i+3]));
      }
      if (!strcmp((gchar *)attributes[i], "from")) {
        from = g_strndup((gchar *)attributes[i+3],
                         (gsize) (attributes[i+4] - attributes[i+3]));
      }
    }
    g_signal_emit(self, signals[STREAM_OPENED], 0, to, from);
    priv->depth++;
    return;
  } 

  if (priv->stanza == NULL) {
    priv->stanza = salut_xmpp_stanza_new((gchar *)localname);
    priv->node = priv->stanza->node;
  } else {
    g_queue_push_tail(priv->nodes, priv->node);
    priv->node = salut_xmpp_node_add_child(priv->node, (gchar *)localname);
  }
  salut_xmpp_node_set_ns(priv->node, (gchar *)uri);

  for (i = 0; i < nb_attributes * 5; i+=5) {
    /* Node is localname, prefix, uri, valuestart, valueend */
    if (attributes[i+1] != NULL
        && !strcmp((gchar *)attributes[i+1], "xml") 
        && !strcmp((gchar *)attributes[i], "lang")) {
      salut_xmpp_node_set_language_n(priv->node, 
                                   (gchar *)attributes[i+3],
                                   (gsize) (attributes[i+4] - attributes[i+3]));
    } else {
      salut_xmpp_node_set_attribute_n_ns(priv->node, 
                                   (gchar *)attributes[i], 
                                   (gchar *)attributes[i+3],
                                   (gsize)(attributes[i+4] - attributes[i+3]),
                                   (gchar *)attributes[i+2]);
    }
  }
  priv->depth++;
}

static void 
_characters (void *user_data, const xmlChar *ch, int len) {
  SalutXmppReader *self = SALUT_XMPP_READER (user_data);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);

  if (priv->node != NULL) { 
    salut_xmpp_node_append_content_n(priv->node, (const gchar *)ch, (gsize)len);
  }
}

static void 
_end_element_ns(void *user_data, const xmlChar *localname, 
                const xmlChar *prefix, const xmlChar *uri) {
  SalutXmppReader *self = SALUT_XMPP_READER (user_data);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);

  priv->depth--;

  if (priv->node && priv->node->content) {
    /* Remove content if it's purely whitespace */
    const char *c;
    for (c = priv->node->content;*c != '\0' && g_ascii_isspace(*c); c++) 
      ;
    if (*c == '\0') 
      salut_xmpp_node_set_content(priv->node, NULL);
  }

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
  SalutXmppReader *self = SALUT_XMPP_READER (user_data);
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (self);
  priv->error = TRUE;
}

gboolean 
salut_xmpp_reader_push(SalutXmppReader *reader, 
                       const guint8 *data, gsize length,
                       GError **error) {
  SalutXmppReaderPrivate *priv = SALUT_XMPP_READER_GET_PRIVATE (reader);

  g_assert(!priv->error);
  xmlParseChunk(priv->parser, (const char*)data, length, FALSE);

  return priv->error;
}
