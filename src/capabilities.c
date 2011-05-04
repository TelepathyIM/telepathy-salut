/*
 * capabilities.c - Connection.Interface.Capabilities constants and utilities
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#include "config.h"
#include "capabilities.h"

#include <wocky/wocky-namespaces.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-manager.h>

typedef struct _Feature Feature;

struct _Feature
{
  enum {
    FEATURE_FIXED,
    FEATURE_OPTIONAL,
  } feature_type;
  gchar *ns;
};

static const Feature self_advertised_features[] =
{
  { FEATURE_FIXED, WOCKY_XMPP_NS_SI},
  { FEATURE_FIXED, WOCKY_XMPP_NS_IBB},
  { FEATURE_FIXED, WOCKY_TELEPATHY_NS_TUBES},
  { FEATURE_FIXED, WOCKY_XMPP_NS_IQ_OOB},
  { FEATURE_FIXED, WOCKY_XMPP_NS_X_OOB},

  { 0, NULL}
};

GabbleCapabilitySet *
salut_dup_self_advertised_caps (void)
{
  GabbleCapabilitySet *ret = gabble_capability_set_new ();
  const Feature *i;

  for (i = self_advertised_features; NULL != i->ns; i++)
    gabble_capability_set_add (ret, i->ns);

  return ret;
}
