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
#include <string.h>

#include "gibber-xmpp-stanza.h"
#include "gibber-namespaces.h"
#include "gibber-debug.h"

G_DEFINE_TYPE(GibberXmppStanza, gibber_xmpp_stanza, G_TYPE_OBJECT)

/* private structure */
typedef struct _GibberXmppStanzaPrivate GibberXmppStanzaPrivate;

struct _GibberXmppStanzaPrivate
{
  gboolean dispose_has_run;
};

#define GIBBER_XMPP_STANZA_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_XMPP_STANZA, GibberXmppStanzaPrivate))

typedef struct
{
    GibberStanzaType type;
    const gchar *name;
    const gchar *ns;
} StanzaTypeName;

static const StanzaTypeName type_names[NUM_GIBBER_STANZA_TYPE] =
{
    { GIBBER_STANZA_TYPE_NONE,            NULL,        NULL },
    { GIBBER_STANZA_TYPE_MESSAGE,         "message",   NULL },
    { GIBBER_STANZA_TYPE_PRESENCE,        "presence",  NULL },
    { GIBBER_STANZA_TYPE_IQ,              "iq",        NULL },
    { GIBBER_STANZA_TYPE_STREAM,          "stream",    GIBBER_XMPP_NS_STREAM },
    { GIBBER_STANZA_TYPE_STREAM_FEATURES, "features",  GIBBER_XMPP_NS_STREAM },
    { GIBBER_STANZA_TYPE_AUTH,            "auth",      NULL },
    { GIBBER_STANZA_TYPE_CHALLENGE,       "challenge", NULL },
    { GIBBER_STANZA_TYPE_RESPONSE,        "response",  NULL },
    { GIBBER_STANZA_TYPE_SUCCESS,         "success",   NULL },
    { GIBBER_STANZA_TYPE_FAILURE,         "failure",   NULL },
    { GIBBER_STANZA_TYPE_STREAM_ERROR,    "error",     GIBBER_XMPP_NS_STREAM },
    { GIBBER_STANZA_TYPE_UNKNOWN,         NULL,        NULL },
};

typedef struct
{
  GibberStanzaSubType sub_type;
  const gchar *name;
  GibberStanzaType type;
} StanzaSubTypeName;

static const StanzaSubTypeName sub_type_names[NUM_GIBBER_STANZA_SUB_TYPE] =
{
    { GIBBER_STANZA_SUB_TYPE_NONE,           NULL,
        GIBBER_STANZA_TYPE_NONE },
    { GIBBER_STANZA_SUB_TYPE_AVAILABLE,
        NULL, GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_NORMAL,         "normal",
        GIBBER_STANZA_TYPE_NONE },
    { GIBBER_STANZA_SUB_TYPE_CHAT,           "chat",
        GIBBER_STANZA_TYPE_MESSAGE },
    { GIBBER_STANZA_SUB_TYPE_GROUPCHAT,      "groupchat",
        GIBBER_STANZA_TYPE_MESSAGE },
    { GIBBER_STANZA_SUB_TYPE_HEADLINE,       "headline",
        GIBBER_STANZA_TYPE_MESSAGE },
    { GIBBER_STANZA_SUB_TYPE_UNAVAILABLE,    "unavailable",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_PROBE,          "probe",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_SUBSCRIBE,      "subscribe",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBE,    "unsubscribe",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_SUBSCRIBED,     "subscribed",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_UNSUBSCRIBED,   "unsubscribed",
        GIBBER_STANZA_TYPE_PRESENCE },
    { GIBBER_STANZA_SUB_TYPE_GET,            "get",
        GIBBER_STANZA_TYPE_IQ },
    { GIBBER_STANZA_SUB_TYPE_SET,            "set",
        GIBBER_STANZA_TYPE_IQ },
    { GIBBER_STANZA_SUB_TYPE_RESULT,         "result",
        GIBBER_STANZA_TYPE_IQ },
    { GIBBER_STANZA_SUB_TYPE_ERROR,          "error",
        GIBBER_STANZA_TYPE_NONE },
    { GIBBER_STANZA_SUB_TYPE_UNKNOWN,        NULL,
        GIBBER_STANZA_TYPE_UNKNOWN },
};

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
  gibber_xmpp_node_free (self->node);

  G_OBJECT_CLASS (gibber_xmpp_stanza_parent_class)->finalize (object);
}

GibberXmppStanza *
gibber_xmpp_stanza_new_ns (const gchar *name,
    const gchar *ns)
{
  GibberXmppStanza *result;

  result = GIBBER_XMPP_STANZA (g_object_new (GIBBER_TYPE_XMPP_STANZA, NULL));
  result->node = gibber_xmpp_node_new_ns (name, ns);

  return result;
}

static gboolean
gibber_xmpp_stanza_add_build_va (GibberXmppNode *node,
                                 GibberBuildTag arg,
                                 va_list ap)
{
  GSList *stack = NULL;

  stack = g_slist_prepend (stack, node);

  while (arg != GIBBER_STANZA_END)
    {
      switch (arg)
        {
        case GIBBER_NODE_ATTRIBUTE:
          {
            gchar *key = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);

            g_assert (key != NULL);
            g_assert (value != NULL);
            gibber_xmpp_node_set_attribute (stack->data, key, value);
          }
          break;

        case GIBBER_NODE:
          {
            gchar *name = va_arg (ap, gchar *);
            GibberXmppNode *child;

            g_assert (name != NULL);
            child = gibber_xmpp_node_add_child (stack->data, name);
            stack = g_slist_prepend (stack, child);
          }
          break;

        case GIBBER_NODE_TEXT:
          {
            gchar *txt = va_arg (ap, gchar *);

            g_assert (txt != NULL);
            gibber_xmpp_node_set_content (stack->data, txt);
          }
          break;

        case GIBBER_NODE_XMLNS:
          {
            gchar *ns = va_arg (ap, gchar *);

            g_assert (ns != NULL);
            gibber_xmpp_node_set_ns (stack->data, ns);
          }
          break;

        case GIBBER_NODE_END:
          {
            /* delete the top of the stack */
            stack = g_slist_delete_link (stack, stack);
          }
          break;

        case GIBBER_NODE_ASSIGN_TO:
          {
            GibberXmppNode **dest = va_arg (ap, GibberXmppNode **);

            g_assert (dest != NULL);
            *dest = stack->data;
          }
          break;

        default:
          g_assert_not_reached ();
        }

      arg = va_arg (ap, GibberBuildTag);
    }

  g_slist_free (stack);
  return TRUE;
}

static const gchar *
get_type_name (GibberStanzaType type)
{
  if (type <= GIBBER_STANZA_TYPE_NONE ||
      type >= NUM_GIBBER_STANZA_TYPE)
    return NULL;

  g_assert (type_names[type].type == type);
  return type_names[type].name;
}

static const gchar *
get_type_ns (GibberStanzaType type)
{
  if (type <= GIBBER_STANZA_TYPE_NONE ||
      type >= NUM_GIBBER_STANZA_TYPE)
    return NULL;

  g_assert (type_names[type].type == type);
  return type_names[type].ns;
}

static const gchar *
get_sub_type_name (GibberStanzaSubType sub_type)
{
  if (sub_type <= GIBBER_STANZA_SUB_TYPE_NONE ||
      sub_type >= NUM_GIBBER_STANZA_SUB_TYPE)
    return NULL;

  g_assert (sub_type_names[sub_type].sub_type == sub_type);
  return sub_type_names[sub_type].name;
}

static gboolean
check_sub_type (GibberStanzaType type,
                GibberStanzaSubType sub_type)
{
  g_return_val_if_fail (type > GIBBER_STANZA_TYPE_NONE &&
      type < NUM_GIBBER_STANZA_TYPE, FALSE);
  g_return_val_if_fail (sub_type < NUM_GIBBER_STANZA_SUB_TYPE, FALSE);

  g_assert (sub_type_names[sub_type].sub_type == sub_type);
  g_return_val_if_fail (
      sub_type_names[sub_type].type == GIBBER_STANZA_TYPE_NONE ||
      sub_type_names[sub_type].type == type, FALSE);

  return TRUE;
}

static GibberXmppStanza *
gibber_xmpp_stanza_new_with_sub_type (GibberStanzaType type,
                                      GibberStanzaSubType sub_type)
{
  GibberXmppStanza *stanza = NULL;
  const gchar *sub_type_name;

  if (!check_sub_type (type, sub_type))
    return NULL;

  stanza = gibber_xmpp_stanza_new_ns (get_type_name (type),
      get_type_ns (type));

  sub_type_name = get_sub_type_name (sub_type);
  if (sub_type_name != NULL)
    gibber_xmpp_node_set_attribute (stanza->node, "type", sub_type_name);

  return stanza;
}

/**
 * gibber_xmpp_stanza_build
 *
 * Build a XMPP stanza from a list of arguments.
 * Example:
 *
 * gibber_xmpp_stanza_build (
 *    GIBBER_STANZA_TYPE_MESSAGE, GIBBER_STANZA_SUB_TYPE_NONE,
 *    "alice@collabora.co.uk", "bob@collabora.co.uk",
 *    GIBBER_NODE, "html",
 *      GIBBER_NODE_XMLNS, "http://www.w3.org/1999/xhtml",
 *      GIBBER_NODE, "body",
 *        GIBBER_NODE_ATTRIBUTE, "textcolor", "red",
 *        GIBBER_NODE_TEXT, "Telepathy rocks!",
 *      GIBBER_NODE_END,
 *    GIBBER_NODE_END,
 *   GIBBER_STANZA_END);
 *
 * -->
 *
 * <message from='alice@collabora.co.uk' to='bob@collabora.co.uk'>
 *   <html xmlns='http://www.w3.org/1999/xhtml'>
 *     <body textcolor='red'>
 *       Telepathy rocks!
 *     </body>
 *   </html>
 * </message>
 **/
GibberXmppStanza *
gibber_xmpp_stanza_build (GibberStanzaType type,
                          GibberStanzaSubType sub_type,
                          const gchar *from,
                          const gchar *to,
                          GibberBuildTag spec,
                          ...)

{
  GibberXmppStanza *stanza;
  va_list ap;

  g_return_val_if_fail (type < NUM_GIBBER_STANZA_TYPE, NULL);
  g_return_val_if_fail (sub_type < NUM_GIBBER_STANZA_SUB_TYPE, NULL);

  stanza = gibber_xmpp_stanza_new_with_sub_type (type, sub_type);
  if (stanza == NULL)
    return NULL;

  if (from != NULL)
    gibber_xmpp_node_set_attribute (stanza->node, "from", from);

  if (to != NULL)
    gibber_xmpp_node_set_attribute (stanza->node, "to", to);

  va_start (ap, spec);
  if (!gibber_xmpp_stanza_add_build_va (stanza->node, spec, ap))
    {
      g_object_unref (stanza);
      stanza = NULL;
    }
  va_end (ap);

  return stanza;
}

static GibberStanzaType
get_type_from_name (const gchar *name)
{
  guint i;

  if (name == NULL)
    return GIBBER_STANZA_TYPE_NONE;

  /* We skip the first entry as it's NONE */
  for (i = 1; i < GIBBER_STANZA_TYPE_UNKNOWN; i++)
    {
       if (type_names[i].name != NULL &&
           strcmp (name, type_names[i].name) == 0)
         {
           return type_names[i].type;
         }
    }

  return GIBBER_STANZA_TYPE_UNKNOWN;
}

static GibberStanzaSubType
get_sub_type_from_name (const gchar *name)
{
  guint i;

  if (name == NULL)
    return GIBBER_STANZA_SUB_TYPE_NONE;

  /* We skip the first entry as it's NONE */
  for (i = 1; i < GIBBER_STANZA_SUB_TYPE_UNKNOWN; i++)
    {
      if (sub_type_names[i].name != NULL &&
          strcmp (name, sub_type_names[i].name) == 0)
        {
          return sub_type_names[i].sub_type;
        }
    }

  return GIBBER_STANZA_SUB_TYPE_UNKNOWN;
}

void
gibber_xmpp_stanza_get_type_info (GibberXmppStanza *stanza,
                                  GibberStanzaType *type,
                                  GibberStanzaSubType *sub_type)
{
  g_return_if_fail (stanza != NULL);
  g_assert (stanza->node != NULL);

  if (type != NULL)
    *type = get_type_from_name (stanza->node->name);

  if (sub_type != NULL)
    *sub_type = get_sub_type_from_name (gibber_xmpp_node_get_attribute (
          stanza->node, "type"));
}
