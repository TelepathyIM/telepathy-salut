#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib-object.h>

#include <check.h>

#include "check-salut.h"
#include "check-helpers.h"

#include "config.h"

static Suite *
make_salut_suite (void)
{
    Suite *s = suite_create ("Salut");

    suite_add_tcase (s, make_salut_wocky_node_properties_tcase ());

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

    s = make_salut_suite ();
    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed += srunner_ntests_failed (sr);
    srunner_free (sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
