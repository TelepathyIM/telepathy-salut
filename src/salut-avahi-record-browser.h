/*
 * salut-avahi-record-browser.h - Header for SalutAvahiRecordBrowser
 * Copyright (C) 2007 Collabora Ltd.
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

#ifndef __SALUT_AVAHI_RECORD_BROWSER_H__
#define __SALUT_AVAHI_RECORD_BROWSER_H__

#include <glib-object.h>
#include <avahi-client/lookup.h>
#include <avahi-common/defs.h>
#include "salut-avahi-client.h"
#include "salut-avahi-enums.h"

G_BEGIN_DECLS

typedef struct _SalutAvahiRecordBrowser SalutAvahiRecordBrowser;
typedef struct _SalutAvahiRecordBrowserClass SalutAvahiRecordBrowserClass;

struct _SalutAvahiRecordBrowserClass {
    GObjectClass parent_class;
};

struct _SalutAvahiRecordBrowser {
    GObject parent;
};

GType salut_avahi_record_browser_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_RECORD_BROWSER \
  (salut_avahi_record_browser_get_type())
#define SALUT_AVAHI_RECORD_BROWSER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_RECORD_BROWSER, SalutAvahiRecordBrowser))
#define SALUT_AVAHI_RECORD_BROWSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_RECORD_BROWSER, SalutAvahiRecordBrowserClass))
#define SALUT_IS_AVAHI_RECORD_BROWSER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_RECORD_BROWSER))
#define SALUT_IS_AVAHI_RECORD_BROWSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_RECORD_BROWSER))
#define SALUT_AVAHI_RECORD_BROWSER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_RECORD_BROWSER, SalutAvahiRecordBrowserClass))

SalutAvahiRecordBrowser *
salut_avahi_record_browser_new(const gchar *name, guint16 type);

SalutAvahiRecordBrowser *
salut_avahi_record_browser_new_full(AvahiIfIndex interface,
                                    AvahiProtocol protocol,
                                    const gchar *name,
                                    guint16 clazz,
                                    guint16 type,
                                    SalutAvahiLookupFlags flags);

gboolean
salut_avahi_record_browser_attach(SalutAvahiRecordBrowser *browser,
                                  SalutAvahiClient *client, GError **error);


G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_RECORD_BROWSER_H__*/
