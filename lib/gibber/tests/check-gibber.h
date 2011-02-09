#ifndef __CHECK_GIBBER_H__
#define __CHECK_GIBBER_H__

#include <check.h>

TCase *make_gibber_xmpp_reader_tcase (void);
TCase *make_gibber_xmpp_connection_tcase (void);
TCase *make_gibber_r_multicast_packet_tcase (void);
TCase *make_gibber_r_multicast_causal_transport_tcase (void);
TCase *make_gibber_r_multicast_sender_tcase (void);
TCase *make_gibber_iq_helper_tcase (void);
TCase *make_gibber_listener_tcase (void);
TCase *make_gibber_xmpp_connection_listener_tcase (void);
TCase *make_gibber_xmpp_error_tcase (void);
TCase *make_gibber_unix_transport_tcase (void);

#endif /* #ifndef __CHECK_GIBBER_H__ */
