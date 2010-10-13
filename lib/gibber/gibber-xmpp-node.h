/*
 * gibber-xmpp-node.h - Header for Gibber xmpp nodes
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

#ifndef __GIBBER_XMPP_NODE_H__
#define __GIBBER_XMPP_NODE_H__

#include <glib.h>
#include <wocky/wocky-node.h>

G_BEGIN_DECLS

typedef WockyNode GibberXmppNode;

#define gibber_xmpp_node_each_child wocky_node_each_child
#define gibber_xmpp_node_get_attribute wocky_node_get_attribute
#define gibber_xmpp_node_get_attribute_ns wocky_node_get_attribute_ns
#define gibber_xmpp_node_set_attribute wocky_node_set_attribute
#define gibber_xmpp_node_set_attribute_ns wocky_node_set_attribute_ns
#define gibber_xmpp_node_set_attribute_n wocky_node_set_attribute_n
#define gibber_xmpp_node_set_attribute_n_ns wocky_node_set_attribute_n_ns
#define gibber_xmpp_node_get_child wocky_node_get_child
#define gibber_xmpp_node_get_child_ns wocky_node_get_child_ns
#define gibber_xmpp_node_add_child wocky_node_add_child
#define gibber_xmpp_node_add_child_ns wocky_node_add_child_ns
#define gibber_xmpp_node_add_child_with_content wocky_node_add_child_with_content
#define gibber_xmpp_node_add_child_with_content_ns wocky_node_add_child_with_content_ns

#define gibber_xmpp_node_get_ns wocky_node_get_ns
#define gibber_xmpp_node_get_language wocky_node_get_language
#define gibber_xmpp_node_set_language wocky_node_set_language
#define gibber_xmpp_node_set_language_n wocky_node_set_language_n
#define gibber_xmpp_node_set_content wocky_node_set_content
#define gibber_xmpp_node_append_content wocky_node_append_content
#define gibber_xmpp_node_append_content_n wocky_node_append_content_n
#define gibber_xmpp_node_to_string wocky_node_to_string

#define gibber_xmpp_node_new_ns wocky_node_new
#define gibber_xmpp_node_free wocky_node_free

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_NODE_H__*/
