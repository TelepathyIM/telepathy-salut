/*
 * gibber-iq-helper.h - Header for GibberIqHelper
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __GIBBER_IQ_HELPER_H__
#define __GIBBER_IQ_HELPER_H__

#include <glib-object.h>

#include "gibber-xmpp-stanza.h"
#include "gibber-xmpp-connection.h"
#include "gibber-xmpp-error.h"

G_BEGIN_DECLS

typedef struct _GibberIqHelper GibberIqHelper;
typedef struct _GibberIqHelperClass GibberIqHelperClass;

struct _GibberIqHelperClass
{
    GObjectClass parent_class;
};

struct _GibberIqHelper
{
    GObject parent;

    gpointer priv;
};

GType gibber_iq_helper_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_IQ_HELPER \
  (gibber_iq_helper_get_type())
#define GIBBER_IQ_HELPER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_IQ_HELPER, GibberIqHelper))
#define GIBBER_IQ_HELPER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_IQ_HELPER, \
                           GibberIqHelperClass))
#define GIBBER_IS_IQ_HELPER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_IQ_HELPER))
#define GIBBER_IS_IQ_HELPER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_IQ_HELPER))
#define GIBBER_IQ_HELPER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_IQ_HELPER, \
                              GibberIqHelperClass))

typedef void (*GibberIqHelperStanzaReplyFunc) (GibberIqHelper *helper,
                                               GibberXmppStanza *sent_stanza,
                                               GibberXmppStanza *reply_stanza,
                                               GObject *object,
                                               gpointer user_data);

GibberIqHelper *
gibber_iq_helper_new (GibberXmppConnection *xmpp_connection);

gboolean
gibber_iq_helper_send_with_reply (GibberIqHelper *helper,
    GibberXmppStanza *iq, GibberIqHelperStanzaReplyFunc reply_func,
    GObject *object, gpointer user_data, GError **error);

GibberXmppStanza *
gibber_iq_helper_new_result_reply (GibberXmppStanza *iq);

GibberXmppStanza *
gibber_iq_helper_new_error_reply (GibberXmppStanza *iq, GibberXmppError error,
    const gchar *errmsg);

G_END_DECLS

#endif /* #ifndef __GIBBER_IQ_HELPER_H__*/
