/*
 * salut-avahi-entry-group.h - Header for SalutAvahiEntryGroup
 * Copyright (C) 2005 Collabora Ltd.
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

#ifndef __SALUT_AVAHI_ENTRY_GROUP_H__
#define __SALUT_AVAHI_ENTRY_GROUP_H__

#include <glib-object.h>
#include <avahi-client/publish.h>
#include <avahi-client/client.h>

#include "salut-avahi-client.h"

G_BEGIN_DECLS

typedef enum {
  SALUT_AVAHI_ENTRY_GROUP_STATE_UNCOMMITED = AVAHI_ENTRY_GROUP_UNCOMMITED,
  SALUT_AVAHI_ENTRY_GROUP_STATE_REGISTERING = AVAHI_ENTRY_GROUP_REGISTERING,
  SALUT_AVAHI_ENTRY_GROUP_STATE_ESTABLISHED =  AVAHI_ENTRY_GROUP_ESTABLISHED,
  SALUT_AVAHI_ENTRY_GROUP_STATE_COLLISTION =  AVAHI_ENTRY_GROUP_COLLISION,
  SALUT_AVAHI_ENTRY_GROUP_STATE_FAILURE =  AVAHI_ENTRY_GROUP_FAILURE
} SalutAvahiEntryGroupState;

typedef struct _SalutAvahiEntryGroupService SalutAvahiEntryGroupService;
typedef struct _SalutAvahiEntryGroup SalutAvahiEntryGroup;
typedef struct _SalutAvahiEntryGroupClass SalutAvahiEntryGroupClass;

struct _SalutAvahiEntryGroupService {
  AvahiIfIndex interface;
  AvahiProtocol protocol;
  AvahiPublishFlags flags;
  gchar *name;
  gchar *type;
  gchar *domain;
  gchar *host;
  guint16 port;
};

struct _SalutAvahiEntryGroupClass {
    GObjectClass parent_class;
};

struct _SalutAvahiEntryGroup {
    GObject parent;
};

GType salut_avahi_entry_group_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_ENTRY_GROUP \
  (salut_avahi_entry_group_get_type())
#define SALUT_AVAHI_ENTRY_GROUP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_ENTRY_GROUP, SalutAvahiEntryGroup))
#define SALUT_AVAHI_ENTRY_GROUP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_ENTRY_GROUP, SalutAvahiEntryGroupClass))
#define SALUT_IS_AVAHI_ENTRY_GROUP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_ENTRY_GROUP))
#define SALUT_IS_AVAHI_ENTRY_GROUP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_ENTRY_GROUP))
#define SALUT_AVAHI_ENTRY_GROUP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_ENTRY_GROUP, SalutAvahiEntryGroupClass))

SalutAvahiEntryGroup * salut_avahi_entry_group_new(void);

gboolean
salut_avahi_entry_group_attach(SalutAvahiEntryGroup *group,
                               SalutAvahiClient *client, GError **error);

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_strlist(SalutAvahiEntryGroup *group,
                                            const gchar *name,
                                            const gchar *type,
                                            guint16 port,
                                            GError **error,
                                            AvahiStringList *txt);

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_full_strlist(SalutAvahiEntryGroup *group,
                                                 AvahiIfIndex interface,
                                                 AvahiProtocol protocol,
                                                 AvahiPublishFlags flags,
                                                 const gchar *name,
                                                 const gchar *type,
                                                 const gchar *domain,
                                                 const gchar *host,
                                                 guint16 port,
                                                 GError **error,
                                                 AvahiStringList *txt);
SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service(SalutAvahiEntryGroup *group,
                                    const gchar *name,
                                    const gchar *type,
                                    guint16 port,
                                    GError **error,
                                    ...);

SalutAvahiEntryGroupService *
salut_avahi_entry_group_add_service_full(SalutAvahiEntryGroup *group,
                                         AvahiIfIndex interface,
                                         AvahiProtocol protocol,
                                         AvahiPublishFlags flags,
                                         const gchar *name,
                                         const gchar *type,
                                         const gchar *domain,
                                         const gchar *host,
                                         guint16 port,
                                         GError **error,
                                         ...);

/* Add raw record */
gboolean
salut_avahi_entry_group_add_record(SalutAvahiEntryGroup *group,
                                   AvahiPublishFlags flags,
                                   const gchar *name,
                                   guint16 type,
                                   guint32 ttl,
                                   const void *rdata,
                                   gsize size,
                                   GError **error);
gboolean
salut_avahi_entry_group_add_record_full(SalutAvahiEntryGroup *group,
                                        AvahiIfIndex interface,
                                        AvahiProtocol protocol,
                                        AvahiPublishFlags flags,
                                        const gchar *name,
                                        guint16 clazz,
                                        guint16 type,
                                        guint32 ttl,
                                        const void *rdata,
                                        gsize size,
                                        GError **error);



void
salut_avahi_entry_group_service_freeze(SalutAvahiEntryGroupService *service);

/* Set a key in the service record. If the service isn't frozen it's committed
 * immediately */
gboolean
salut_avahi_entry_group_service_set(SalutAvahiEntryGroupService *service,
                                        const gchar *key, const gchar *value,
                                        GError **error);

gboolean
salut_avahi_entry_group_service_set_arbitrary(
    SalutAvahiEntryGroupService *service,
    const gchar *key, const guint8 *value, gsize size,
    GError **error);

/* Remove one key from the service record */
gboolean
salut_avahi_entry_group_service_remove_key(SalutAvahiEntryGroupService *service,
                                           const gchar *key, GError **error);

/* Update the txt record of the frozen service */
gboolean
salut_avahi_entry_group_service_thaw(SalutAvahiEntryGroupService *service,
                                     GError **error);

/* Commit all newly added services */
gboolean
salut_avahi_entry_group_commit(SalutAvahiEntryGroup *group, GError **error);

/* Invalidated all SalutAvahiEntryGroupServices */
gboolean
salut_avahi_entry_group_reset(SalutAvahiEntryGroup *group, GError **error);

G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_ENTRY_GROUP_H__*/
