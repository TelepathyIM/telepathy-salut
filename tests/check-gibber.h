#ifndef __CHECK_GIBBER_H__
#define __CHECK_GIBBER_H__

TCase *make_gibber_xmpp_node_tcase (void);
TCase *make_gibber_xmpp_reader_tcase (void);
TCase *make_gibber_xmpp_connection_tcase (void);
TCase *make_gibber_sasl_auth_tcase (void);
TCase *make_gibber_r_multicast_packet_tcase (void);
TCase *make_gibber_r_multicast_transport_tcase (void);
TCase *make_gibber_r_multicast_sender_tcase (void);
TCase *make_gibber_xmpp_stanza_tcase (void);
TCase *make_gibber_iq_helper_tcase (void);

#endif /* #ifndef __CHECK_GIBBER_H__ */