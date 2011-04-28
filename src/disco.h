/*
 * disco.h - Headers for Salut service discovery
 *
 * Copyright (C) 2006-2008 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
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
 *
 * -- LET'S DISCO!!!  \o/ \o_ _o/ /\o/\ _/o/- -\o\_ --
 */

#ifndef __SALUT_DISCO_H__
#define __SALUT_DISCO_H__

#include <glib-object.h>
#include <wocky/wocky-stanza.h>

#include "contact.h"
#include "connection.h"

G_BEGIN_DECLS

typedef enum
{
  SALUT_DISCO_TYPE_INFO,
  SALUT_DISCO_TYPE_ITEMS
} SalutDiscoType;

typedef struct _SalutDiscoClass SalutDiscoClass;
typedef struct _SalutDiscoPrivate SalutDiscoPrivate;
typedef struct _SalutDiscoRequest SalutDiscoRequest;

/**
 * SalutDiscoError:
 * @SALUT_DISCO_ERROR_CANCELLED: The DISCO request was cancelled
 * @SALUT_DISCO_ERROR_TIMEOUT: The DISCO request timed out
 * @SALUT_DISCO_ERROR_UNKNOWN: An unknown error occured
 */
typedef enum
{
  SALUT_DISCO_ERROR_CANCELLED,
  SALUT_DISCO_ERROR_TIMEOUT,
  SALUT_DISCO_ERROR_UNKNOWN
} SalutDiscoError;

GQuark salut_disco_error_quark (void);
#define SALUT_DISCO_ERROR salut_disco_error_quark ()

GType salut_disco_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_DISCO \
  (salut_disco_get_type ())
#define SALUT_DISCO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_DISCO, SalutDisco))
#define SALUT_DISCO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_DISCO, SalutDiscoClass))
#define SALUT_IS_DISCO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_DISCO))
#define SALUT_IS_DISCO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_DISCO))
#define SALUT_DISCO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_DISCO, SalutDiscoClass))

struct _SalutDiscoClass {
    GObjectClass parent_class;
};

struct _SalutDisco {
    GObject parent;
    SalutDiscoPrivate *priv;
};

typedef void (*SalutDiscoCb)(SalutDisco *self, SalutDiscoRequest *request,
    SalutContact *contact, const gchar *node, WockyNode *query_result,
    GError* error, gpointer user_data);

SalutDisco *salut_disco_new (SalutConnection *connection);

SalutDiscoRequest *salut_disco_request (SalutDisco *self,
    SalutDiscoType type, SalutContact *contact, const char *node,
    SalutDiscoCb callback, gpointer user_data, GObject *object,
    GError **error);

void salut_disco_cancel_request (SalutDisco *disco,
    SalutDiscoRequest *request);


G_END_DECLS

#endif
