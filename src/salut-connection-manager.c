/*
 * salut-connection-manager.c - Source for SalutConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "salut-connection-manager.h"
#include "salut-connection-manager-signals-marshal.h"

#include "salut-connection-manager-glue.h"

#include "salut-connection.h"
#include "salut-contact-manager.h"

#include "telepathy-helpers.h"
#include "telepathy-constants.h"
#include "telepathy-errors.h"

#define TP_TYPE_PARAM (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_VALUE, \
      G_TYPE_INVALID))

G_DEFINE_TYPE(SalutConnectionManager, salut_connection_manager, G_TYPE_OBJECT)

/* signal enum */
enum
{
    NEW_CONNECTION,
    NO_MORE_CONNECTIONS,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutConnectionManagerPrivate SalutConnectionManagerPrivate;

struct _SalutConnectionManagerPrivate
{
  gboolean dispose_has_run;
  GHashTable *connections;
};

#define SALUT_CONNECTION_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_CONNECTION_MANAGER, SalutConnectionManagerPrivate))

static void
salut_connection_manager_init (SalutConnectionManager *obj)
{
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->connections = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                              NULL, g_object_unref);
}

static void salut_connection_manager_dispose (GObject *object);
static void salut_connection_manager_finalize (GObject *object);

static void
salut_connection_manager_class_init (SalutConnectionManagerClass *salut_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_connection_manager_class);

  g_type_class_add_private (salut_connection_manager_class, sizeof (SalutConnectionManagerPrivate));

  object_class->dispose = salut_connection_manager_dispose;
  object_class->finalize = salut_connection_manager_finalize;

  signals[NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_OBJECT_CLASS_TYPE (salut_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_connection_manager_marshal_VOID__STRING_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_STRING, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);
  signals[NO_MORE_CONNECTIONS] =
    g_signal_new ("no-more-connections",
                  G_OBJECT_CLASS_TYPE (salut_connection_manager_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_connection_manager_class), &dbus_glib_salut_connection_manager_object_info);
}

void
salut_connection_manager_dispose (GObject *object)
{
  SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE (self);
  DBusGProxy *bus_proxy;  
  bus_proxy = tp_get_bus_proxy();;

  if (priv->dispose_has_run)
    return;
  priv->dispose_has_run = TRUE;
  
  if (priv->connections != NULL) {
    g_hash_table_destroy(priv->connections);
    priv->connections = NULL;
  }


  /* release any references held by the object here */
  org_freedesktop_DBus_request_name(bus_proxy,
                                    SALUT_CONN_MGR_BUS_NAME,
                                    DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                    NULL, NULL);

  if (G_OBJECT_CLASS (salut_connection_manager_parent_class)->dispose)
    G_OBJECT_CLASS (salut_connection_manager_parent_class)->dispose (object);
}

void
salut_connection_manager_finalize (GObject *object)
{
  //SalutConnectionManager *self = SALUT_CONNECTION_MANAGER (object);
  //SalutConnectionManagerPrivate *priv = SALUT_CONNECTION_MANAGER_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_connection_manager_parent_class)->finalize (object);
}

/* private data */
typedef struct { 
  guint set_mask;

  gchar *first_name;
  gchar *last_name;
  gchar *email;
  gchar *jid;
  gchar *account;
} SalutParams;

enum {
  LOCAL_JABBER_PARAM_FIRST_NAME = 0,
  LOCAL_JABBER_PARAM_LAST_NAME,
  LOCAL_JABBER_PARAM_JID,
  LOCAL_JABBER_PARAM_EMAIL,
  LOCAL_JABBER_NR_PARAMS
};

static const SalutParamSpec local_jabber_params[] = {
  { "first-name", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 
     G_STRUCT_OFFSET(SalutParams, first_name)},
  { "last-name", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 
     G_STRUCT_OFFSET(SalutParams, last_name)},
  { "jid", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 
     G_STRUCT_OFFSET(SalutParams, jid)},
  { "email", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 
     G_STRUCT_OFFSET(SalutParams, email)},
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, 
     G_STRUCT_OFFSET(SalutParams, account)},
  {NULL, NULL, 0, 0, NULL, 0}
};

static const SalutProtocolSpec _salut_protocols[] = {
  {"salut", local_jabber_params},
  { NULL, NULL}
};

const SalutProtocolSpec *salut_protocols = _salut_protocols;

/* public functions */
void
_salut_connection_manager_register(SalutConnectionManager *self) {
  DBusGConnection *bus;
  DBusGProxy *bus_proxy;
  GError *error = NULL;
  guint request_name_result;

  g_assert(SALUT_IS_CONNECTION_MANAGER (self));

  bus = tp_get_bus();
  bus_proxy = tp_get_bus_proxy();

  if (!org_freedesktop_DBus_request_name(bus_proxy,
                                         SALUT_CONN_MGR_BUS_NAME,
                                         DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                         &request_name_result,
                                         &error)) {
    g_error("Failed to request bus name: %s", error->message);
  }

  if (request_name_result == DBUS_REQUEST_NAME_REPLY_EXISTS) 
    g_error("Failed to acquire bus name, connection maanger already running?");

  dbus_g_connection_register_g_object(bus, SALUT_CONN_MGR_OBJECT_PATH, 
                                      G_OBJECT(self));
}

/* private */
static void
connection_disconnected_cb(SalutConnection *conn, gpointer data) {
  SalutConnectionManager *mgr = SALUT_CONNECTION_MANAGER(data);
  SalutConnectionManagerPrivate *priv = 
    SALUT_CONNECTION_MANAGER_GET_PRIVATE(mgr);
  g_assert(g_hash_table_remove(priv->connections, conn));

  if (g_hash_table_size(priv->connections) == 0) {
    g_signal_emit(mgr, signals[NO_MORE_CONNECTIONS], 0);
  }
}

static gboolean
get_parameters (const char *proto, const SalutParamSpec **params, GError **error)
{
  if (!strcmp (proto, "salut"))
    {
      *params = local_jabber_params;
    }
  else
    {
      g_debug ("%s: unknown protocol %s", G_STRFUNC, proto);

      *error = g_error_new (TELEPATHY_ERRORS, NotImplemented,
                            "unknown protocol %s", proto);

      return FALSE;
    }

  return TRUE;
}

static gboolean
set_param_from_value (const SalutParamSpec *paramspec,
                      GValue *value,
                      SalutParams *params,
                      GError **error)
{
  if (G_VALUE_TYPE (value) != paramspec->gtype)
    {
      g_debug ("%s: expected type %s for parameter %s, got %s",
               G_STRFUNC,
               g_type_name (paramspec->gtype), paramspec->name,
               G_VALUE_TYPE_NAME (value));
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "expected type %s for account parameter %s, got %s",
                            g_type_name (paramspec->gtype), paramspec->name,
                            G_VALUE_TYPE_NAME (value));
      return FALSE;
    }

  switch (paramspec->dtype[0])
    {
      case DBUS_TYPE_STRING:
        {
          const char *str = g_value_get_string (value);
          if (!str || *str == '\0') {
            *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "empty string for  for account parameter %s",
                            paramspec->name);
            return FALSE;
          } else {
            *((char **) ((void *)params + paramspec->offset)) = g_value_dup_string (value);
          }
        }
        break;
      case DBUS_TYPE_INT16:
        *((gint *) ((void *)params + paramspec->offset)) = g_value_get_int (value);
        break;
      case DBUS_TYPE_UINT16:
        *((guint *) ((void *)params + paramspec->offset)) = g_value_get_uint (value);
        break;
      case DBUS_TYPE_BOOLEAN:
        *((gboolean *) ((void *)params + paramspec->offset)) = g_value_get_boolean (value);
        break;
      default:
        g_error ("set_param_from_value: encountered unknown type %s on argument %s",
                 paramspec->dtype, paramspec->name);
        return FALSE;
    }

  return TRUE;
}


static gboolean
parse_parameters (const SalutParamSpec *paramspec,
                  GHashTable            *provided,
                  SalutParams          *params,
                  GError               **error)
{
  int unhandled;
  int i;
  guint mandatory_flag = TP_CONN_MGR_PARAM_FLAG_REQUIRED;
  GValue *value;

  unhandled = g_hash_table_size (provided);

  for (i = 0; paramspec[i].name; i++) {
      value = g_hash_table_lookup (provided, paramspec[i].name);

      if (value == NULL)
        {
          if (paramspec[i].flags & mandatory_flag)
            {
              g_debug ("%s: missing mandatory param %s",
                       G_STRFUNC, paramspec[i].name);
              *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                                    "missing mandatory account parameter %s",
                                    paramspec[i].name);
              return FALSE;
            }
          else
            {
              g_debug ("%s: using default value for param %s",
                       G_STRFUNC, paramspec[i].name);
            }
        }
      else
        {
          if (!set_param_from_value (&paramspec[i], value, params, error))
            return FALSE;

          params->set_mask |= 1 << i;
          unhandled--;
          if (paramspec[i].gtype == G_TYPE_STRING) {
            g_debug ("%s: accepted value %s for param %s",
                      G_STRFUNC,
                      *((char **) ((void *)params + paramspec[i].offset)),
                      paramspec[i].name);
            }
          else
            {
              g_debug ("%s: accepted value %u for param %s", G_STRFUNC,
                       *((guint *) ((void *)params + paramspec[i].offset)), paramspec[i].name);
            }
        }
    }

  if (unhandled)
    {
      g_debug ("%s: unknown argument name provided", G_STRFUNC);
      *error = g_error_new (TELEPATHY_ERRORS, InvalidArgument,
                            "unknown argument name provided");
      return FALSE;
    }

  return TRUE;
}

static void
free_params(SalutParams *params) {
  g_free(params->first_name);
  g_free(params->last_name);
  g_free(params->jid);
  g_free(params->email);
  params->set_mask = 0;
}



/* dbus-exported methods */

/**
 * salut_connection_manager_get_parameters
 *
 * Implements DBus method GetParameters
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_manager_get_parameters (SalutConnectionManager *obj, 
                                          const gchar * proto, 
                                          GPtrArray ** ret, 
                                          GError **error)
{
  const SalutParamSpec *params = NULL;
  int i;

  if (!get_parameters(proto, &params, error)) {
    return FALSE;
  }

  *ret = g_ptr_array_new();
  for (i = 0; params[i].name != NULL ; i++) {
    GValue *def_value;
    GValue param = { 0, };
    g_value_init(&param, TP_TYPE_PARAM);
    g_value_set_static_boxed(&param, 
                             dbus_g_type_specialized_construct (TP_TYPE_PARAM));
    /* There are no default values for salut */
    def_value = g_new0(GValue, 1);
    g_value_init(def_value, params[i].gtype);
    dbus_g_type_struct_set(&param, 
                            0, params[i].name,
                            1, params[i].flags,
                            2, params[i].dtype,
                            3, def_value,
                            G_MAXUINT);
    g_value_unset(def_value);
    g_free(def_value);
    g_ptr_array_add(*ret, g_value_get_boxed(&param));
  }
  return TRUE;
}


/**
 * salut_connection_manager_list_protocols
 *
 * Implements DBus method ListProtocols
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_manager_list_protocols (SalutConnectionManager *obj, 
                                          gchar *** ret, 
                                          GError **error)
{
  const char *protocols[] = { "salut", NULL };
  *ret = g_strdupv((gchar **) protocols);
  return TRUE;
}

#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if ((params.set_mask & (1 << param)) != 0) \
    { \
      g_object_set (conn, prop, member, NULL); \
    }

/**
 * salut_connection_manager_request_connection
 *
 * Implements DBus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean 
salut_connection_manager_request_connection (SalutConnectionManager *self, 
                                              const gchar * proto, 
                                              GHashTable * parameters, 
                                              gchar ** bus_name, 
                                              gchar ** object_path, 
                                              GError **error)
{
  SalutConnection *conn = NULL;
  SalutConnectionManagerPrivate *priv = 
    SALUT_CONNECTION_MANAGER_GET_PRIVATE(self);
  const SalutParamSpec *paramspec;
  SalutParams params = { 0, };


  if (!get_parameters(proto, &paramspec, error)) {
    return FALSE;
  }
  if (!parse_parameters (paramspec, parameters, &params, error)) {
    free_params(&params);
    return FALSE;
  }
  conn = g_object_new(SALUT_TYPE_CONNECTION, NULL);
  SET_PROPERTY_IF_PARAM_SET("first-name", LOCAL_JABBER_PARAM_FIRST_NAME, 
                              params.first_name);
  SET_PROPERTY_IF_PARAM_SET("last-name", LOCAL_JABBER_PARAM_LAST_NAME, 
                              params.last_name);
  SET_PROPERTY_IF_PARAM_SET("jid", LOCAL_JABBER_PARAM_EMAIL, params.jid);
  SET_PROPERTY_IF_PARAM_SET("email", LOCAL_JABBER_PARAM_JID, params.email);
  free_params(&params);

  if (!_salut_connection_register(conn, bus_name, object_path, error)) {
    goto ERROR;
  }
  g_hash_table_insert(priv->connections, conn, conn);
  g_signal_connect(conn, "disconnected", 
                    G_CALLBACK(connection_disconnected_cb), self);

  g_signal_emit(self, signals[NEW_CONNECTION], 0, 
                 *bus_name, *object_path, proto);

  return TRUE;
ERROR:
  if (conn) 
    g_object_unref(G_OBJECT(conn));

  return FALSE;
}

