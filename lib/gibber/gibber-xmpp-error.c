/*
 * gibber-xmpp-error.c - Source for Gibber's XMPP error handling API
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "gibber-xmpp-error.h"

#include <stdlib.h>
#include <stdio.h>

#include "gibber-namespaces.h"

#define MAX_LEGACY_ERRORS 3

typedef struct {
    const gchar *name;
    const gchar *description;
    const gchar *type;
    guint specialises;
    const gchar *namespace;
    const guint16 legacy_errors[MAX_LEGACY_ERRORS];
} XmppErrorSpec;

static const XmppErrorSpec xmpp_errors[NUM_XMPP_ERRORS] =
{
    {
      "redirect",
      "the recipient or server is redirecting requests for this information "
      "to another entity",
      "modify",
      0,
      NULL,
      { 302, 0, },
    },

    {
      "gone",
      "the recipient or server can no longer be contacted at this address",
      "modify",
      0,
      NULL,
      { 302, 0, },
    },

    {
      "bad-request",
      "the sender has sent XML that is malformed or that cannot be processed",
      "modify",
      0,
      NULL,
      { 400, 0, },
    },
    {
      "unexpected-request",
      "the recipient or server understood the request but was not expecting "
      "it at this time",
      "wait",
      0,
      NULL,
      { 400, 0, },
    },
    {
      "jid-malformed",
      "the sending entity has provided or communicated an XMPP address or "
      "aspect thereof (e.g., a resource identifier) that does not adhere "
      "to the syntax defined in Addressing Scheme (Section 3)",
      "modify",
      0,
      NULL,
      { 400, 0, },
    },

    {
      "not-authorized",
      "the sender must provide proper credentials before being allowed to "
      "perform the action, or has provided improper credentials",
      "auth",
      0,
      NULL,
      { 401, 0, },
    },

    {
      "payment-required",
      "the requesting entity is not authorized to access the requested "
      "service because payment is required",
      "auth",
      0,
      NULL,
      { 402, 0, },
    },

    {
      "forbidden",
      "the requesting entity does not possess the required permissions to "
      "perform the action",
      "auth",
      0,
      NULL,
      { 403, 0, },
    },

    {
      "item-not-found",
      "the addressed JID or item requested cannot be found",
      "cancel",
      0,
      NULL,
      { 404, 0, },
    },
    {
      "recipient-unavailable",
      "the intended recipient is temporarily unavailable",
      "wait",
      0,
      NULL,
      { 404, 0, },
    },
    {
      "remote-server-not-found",
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      "cancel",
      0,
      NULL,
      { 404, 0, },
    },

    {
      "not-allowed",
      "the recipient or server does not allow any entity to perform the action",
      "cancel",
      0,
      NULL,
      { 405, 0, },
    },

    {
      "not-acceptable",
      "the recipient or server understands the request but is refusing to "
      "process it because it does not meet criteria defined by the recipient "
      "or server (e.g., a local policy regarding acceptable words in messages)",
      "modify",
      0,
      NULL,
      { 406, 0, },
    },

    {
      "registration-required",
      "the requesting entity is not authorized to access the requested service "
      "because registration is required",
      "auth",
      0,
      NULL,
      { 407, 0, },
    },
    {
      "subscription-required",
      "the requesting entity is not authorized to access the requested service "
      "because a subscription is required",
      "auth",
      0,
      NULL,
      { 407, 0, },
    },

    {
      "remote-server-timeout",
      "a remote server or service specified as part or all of the JID of the "
      "intended recipient (or required to fulfill a request) could not be "
      "contacted within a reasonable amount of time",
      "wait",
      0,
      NULL,
      { 408, 504, 0, },
    },

    {
      "conflict",
      "access cannot be granted because an existing resource or session exists "
      "with the same name or address",
      "cancel",
      0,
      NULL,
      { 409, 0, },
    },

    {
      "internal-server-error",
      "the server could not process the stanza because of a misconfiguration "
      "or an otherwise-undefined internal server error",
      "wait",
      0,
      NULL,
      { 500, 0, },
    },
    {
      "undefined-condition",
      "application-specific condition",
      NULL,
      0,
      NULL,
      { 500, 0, },
    },
    {
      "resource-constraint",
      "the server or recipient lacks the system resources necessary to service "
      "the request",
      "wait",
      0,
      NULL,
      { 500, 0, },
    },

    {
      "feature-not-implemented",
      "the feature requested is not implemented by the recipient or server and "
      "therefore cannot be processed",
      "cancel",
      0,
      NULL,
      { 501, 0, },
    },

    {
      "service-unavailable",
      "the server or recipient does not currently provide the requested "
      "service",
      "cancel",
      0,
      NULL,
      { 502, 503, 510, },
    },

    {
      "out-of-order",
      "the request cannot occur at this point in the state machine",
      "cancel",
      XMPP_ERROR_UNEXPECTED_REQUEST,
      GIBBER_XMPP_NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unknown-session",
      "the 'sid' attribute specifies a session that is unknown to the "
      "recipient",
      "cancel",
      XMPP_ERROR_BAD_REQUEST,
      GIBBER_XMPP_NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unsupported-transports",
      "the recipient does not support any of the desired content transport "
      "methods",
      "cancel",
      XMPP_ERROR_FEATURE_NOT_IMPLEMENTED,
      GIBBER_XMPP_NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "unsupported-content",
      "the recipient does not support any of the desired content description"
      "formats",
      "cancel",
      XMPP_ERROR_FEATURE_NOT_IMPLEMENTED,
      GIBBER_XMPP_NS_JINGLE_ERRORS,
      { 0, },
    },

    {
      "no-valid-streams",
      "None of the available streams are acceptable.",
      "cancel",
      XMPP_ERROR_BAD_REQUEST,
      GIBBER_XMPP_NS_SI,
      { 400, 0 },
    },

    {
      "bad-profile",
      "The profile is not understood or invalid.",
      "modify",
      XMPP_ERROR_BAD_REQUEST,
      GIBBER_XMPP_NS_SI,
      { 400, 0 },
    },
};

GQuark
gibber_xmpp_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gibber-xmpp-error");
  return quark;
}

GibberXmppError
gibber_xmpp_error_from_node (GibberXmppNode *error_node)
{
  gint i, j;
  const gchar *error_code_str;

  g_return_val_if_fail (error_node != NULL, INVALID_XMPP_ERROR);

  /* First, try to look it up the modern way */
  if (error_node->children)
    {
      for (i = NUM_XMPP_ERRORS; i > 0; )
        {
          i--;
          if (gibber_xmpp_node_get_child (error_node, xmpp_errors[i].name))
            {
              return i;
            }
        }
    }

  /* Ok, do it the legacy way */
  error_code_str = gibber_xmpp_node_get_attribute (error_node, "code");
  if (error_code_str)
    {
      gint error_code;

      error_code = atoi (error_code_str);

      for (i = 0; i < NUM_XMPP_ERRORS; i++)
        {
          const XmppErrorSpec *spec = &xmpp_errors[i];

          for (j = 0; j < MAX_LEGACY_ERRORS; j++)
            {
              gint cur_code = spec->legacy_errors[j];
              if (cur_code == 0)
                break;

              if (cur_code == error_code)
                return i;
            }
        }
    }

  return INVALID_XMPP_ERROR;
}

static GError *
gibber_xmpp_error_to_g_error (GibberXmppError error)
{
  if (error >= NUM_XMPP_ERRORS)
      return g_error_new (GIBBER_XMPP_ERROR, XMPP_ERROR_UNKNOWN,
          "Unknown or invalid XMPP error");

  return g_error_new (GIBBER_XMPP_ERROR,
                      error,
                      xmpp_errors[error].description);
}

/*
 * See RFC 3920: 4.7 Stream Errors, 9.3 Stanza Errors.
 */
GibberXmppNode *
gibber_xmpp_error_to_node (GibberXmppError error,
                           GibberXmppNode *parent_node,
                           const gchar *errmsg)
{
  const XmppErrorSpec *spec, *extra;
  GibberXmppNode *error_node, *node;
  gchar str[6];

  if (error >= NUM_XMPP_ERRORS)
    return NULL;

  if (xmpp_errors[error].specialises)
    {
      extra = &xmpp_errors[error];
      spec = &xmpp_errors[extra->specialises];
    }
  else
    {
      extra = NULL;
      spec = &xmpp_errors[error];
    }

  error_node = gibber_xmpp_node_add_child (parent_node, "error");

  sprintf (str, "%d", spec->legacy_errors[0]);
  gibber_xmpp_node_set_attribute (error_node, "code", str);

  if (spec->type)
    {
      gibber_xmpp_node_set_attribute (error_node, "type", spec->type);
    }

  node = gibber_xmpp_node_add_child (error_node, spec->name);
  gibber_xmpp_node_set_ns (node, GIBBER_XMPP_NS_STANZAS);

  if (extra != NULL)
    {
      node = gibber_xmpp_node_add_child (error_node, extra->name);
      gibber_xmpp_node_set_ns (node, extra->namespace);
    }

  if (NULL != errmsg)
    {
      GibberXmppNode * node;

      node = gibber_xmpp_node_add_child (error_node, "text");
      gibber_xmpp_node_set_content (node, errmsg);
    }

  return error_node;
}

const gchar *
gibber_xmpp_error_string (GibberXmppError error)
{
  if (error < NUM_XMPP_ERRORS)
    return xmpp_errors[error].name;
  else
    return NULL;
}

const gchar *
gibber_xmpp_error_description (GibberXmppError error)
{
  if (error < NUM_XMPP_ERRORS)
    return xmpp_errors[error].description;
  else
    return NULL;
}

GError *
gibber_message_get_xmpp_error (GibberXmppStanza *msg)
{
  GibberStanzaSubType sub_type;
  g_return_val_if_fail (msg != NULL, NULL);

  gibber_xmpp_stanza_get_type_info (msg, NULL, &sub_type);

  if (sub_type == GIBBER_STANZA_SUB_TYPE_ERROR)
    {
      GibberXmppNode *error_node = gibber_xmpp_node_get_child (msg->node,
          "error");

      if (error_node != NULL)
        {
          return gibber_xmpp_error_to_g_error
              (gibber_xmpp_error_from_node (error_node));
        }
      else
        {
          return g_error_new (GIBBER_XMPP_ERROR, XMPP_ERROR_UNKNOWN,
              "Unknown or invalid XMPP error");
        }
    }

  /* no error */
  return NULL;
}
