#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib-object.h>

#include <check.h>

#include "check-gibber.h"
#include "check-helpers.h"

#include "config.h"

static Suite *
make_gibber_suite (void)
{
    Suite *s = suite_create ("Gibber");

    suite_add_tcase (s, make_gibber_xmpp_node_tcase ());
    suite_add_tcase (s, make_gibber_xmpp_reader_tcase ());
    suite_add_tcase (s, make_gibber_xmpp_connection_tcase ());
    suite_add_tcase (s, make_gibber_r_multicast_packet_tcase ());
    suite_add_tcase (s, make_gibber_r_multicast_sender_tcase ());
    suite_add_tcase (s, make_gibber_r_multicast_causal_transport_tcase ());
    suite_add_tcase (s, make_gibber_xmpp_stanza_tcase ());
    suite_add_tcase (s, make_gibber_iq_helper_tcase ());
    suite_add_tcase (s, make_gibber_listener_tcase ());
    suite_add_tcase (s, make_gibber_xmpp_connection_listener_tcase ());
    suite_add_tcase (s, make_gibber_xmpp_error_tcase ());
    suite_add_tcase (s, make_gibber_unix_transport_tcase ());

    return s;
}

int
main (void)
{
    int number_failed = 0;
    Suite *s;
    SRunner *sr;

    check_helpers_init ();
    g_type_init ();

    s = make_gibber_suite ();
    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed += srunner_ntests_failed (sr);
    srunner_free (sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
