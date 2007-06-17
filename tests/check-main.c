#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <check.h>

#include "check-gibber.h"
#include "check-salut.h"
#include "check-helpers.h"

Suite *
make_gibber_suite (void)
{
    Suite *s = suite_create ("Gibber");

    suite_add_tcase (s, make_gibber_xmpp_node_tcase());
    suite_add_tcase (s, make_gibber_xmpp_reader_tcase());

    suite_add_tcase (s, make_gibber_r_multicast_transport_tcase());

    return s;
}

Suite *
make_salut_suite (void)
{
    Suite *s = suite_create ("Salut");

    suite_add_tcase (s, make_salut_gibber_xmpp_node_properties_tcase ());

    return s;
}

int
main (void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    check_helpers_init ();

    s = make_gibber_suite ();
    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);

    s = make_salut_suite ();
    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);


    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
