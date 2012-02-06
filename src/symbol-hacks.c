/* This is pretty horrible. If we don't use a symbol in a wocky object
 * from its static library then libtool will not include said object
 * from the binary, so we can't use any symbols from that object in a
 * plugin.
 *
 * This is a hack that X does. They can generate their file though as
 * they have an _X_EXPORT macro. This'll all disappear when Wocky
 * becomes a shared library...
 *
 * http://cgit.freedesktop.org/xorg/xserver/tree/hw/xfree86/loader/sdksyms.sh
 */

#include "symbol-hacks.h"

/* First include all the public headers. */
#include <wocky/wocky.h>

/* Reference one symbol from each of the above headers to include each
 * object in the final binary. */
static void *hacks[] = {
  wocky_auth_handler_get_type,
  wocky_auth_registry_get_type,
  wocky_bare_contact_get_type,
  wocky_c2s_porter_get_type,
  wocky_caps_cache_get_type,
  wocky_connector_get_type,
  wocky_contact_factory_get_type,
  wocky_contact_get_type,
  wocky_data_form_get_type,
  wocky_init,
  wocky_jabber_auth_digest_get_type,
  wocky_jabber_auth_get_type,
  wocky_jabber_auth_password_get_type,
  wocky_ll_connection_factory_get_type,
  wocky_ll_connector_get_type,
  wocky_ll_contact_get_type,
  wocky_loopback_stream_get_type,
  wocky_meta_porter_get_type,
  wocky_muc_get_type,
  wocky_node_new,
  wocky_node_tree_get_type,
  wocky_pep_service_get_type,
  wocky_ping_get_type,
  wocky_porter_get_type,
  wocky_pubsub_make_stanza,
  wocky_pubsub_node_get_type,
  wocky_pubsub_service_get_type,
  wocky_resource_contact_get_type,
  wocky_roster_get_type,
  wocky_sasl_auth_get_type,
  wocky_sasl_digest_md5_get_type,
  wocky_sasl_plain_get_type,
  wocky_sasl_scram_get_type,
  wocky_session_get_type,
  wocky_stanza_get_type,
  wocky_tls_connector_get_type,
  wocky_tls_connection_get_type,
  wocky_tls_handler_get_type,
  wocky_strdiff,
  wocky_xmpp_connection_get_type,
  wocky_xmpp_error_quark,
  wocky_xmpp_reader_get_type,
  wocky_xmpp_writer_get_type,
  NULL,
};

gpointer
salut_symbol_hacks (void)
{
  return hacks;
}
