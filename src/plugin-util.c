/*
 * util.c - Code for Salut utility functions
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006-2007 Nokia Corporation
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

#include <salut/util.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <telepathy-glib/util.h>

#include <salut/capability-set.h>
#include <salut/plugin-contact.h>

#ifdef HAVE_UUID
# include <uuid.h>
#endif

static void
send_stanza_to_contact (WockyPorter *porter,
    WockyContact *contact,
    WockyStanza *stanza)
{
  WockyStanza *to_send = wocky_stanza_copy (stanza);

  wocky_stanza_set_to_contact (to_send, contact);
  wocky_porter_send (porter, to_send);
  g_object_unref (to_send);
}

void
salut_send_ll_pep_event (WockySession *session,
    WockyStanza *stanza)
{
  WockyContactFactory *contact_factory;
  WockyPorter *porter;
  WockyLLContact *self_contact;
  GList *contacts, *l;
  WockyNode *message, *event, *items;
  const gchar *pep_node;
  gchar *node;

  g_return_if_fail (WOCKY_IS_SESSION (session));
  g_return_if_fail (WOCKY_IS_STANZA (stanza));

  message = wocky_stanza_get_top_node (stanza);
  event = wocky_node_get_first_child (message);
  items = wocky_node_get_first_child (event);

  pep_node = wocky_node_get_attribute (items, "node");

  if (pep_node == NULL)
    return;

  node = g_strdup_printf ("%s+notify", pep_node);

  contact_factory = wocky_session_get_contact_factory (session);
  porter = wocky_session_get_porter (session);

  contacts = wocky_contact_factory_get_ll_contacts (contact_factory);

  for (l = contacts; l != NULL; l = l->next)
    {
      SalutPluginContact *contact;

      if (!SALUT_IS_PLUGIN_CONTACT (l->data))
        continue;

      contact = l->data;

      if (gabble_capability_set_has (salut_plugin_contact_get_caps (contact), node))
        send_stanza_to_contact (porter, WOCKY_CONTACT (contact), stanza);
    }

  /* now send to self */
  self_contact = wocky_contact_factory_ensure_ll_contact (contact_factory,
      wocky_porter_get_full_jid (porter));

  send_stanza_to_contact (porter, WOCKY_CONTACT (self_contact), stanza);

  g_object_unref (self_contact);
  g_list_free (contacts);
  g_free (node);
}
