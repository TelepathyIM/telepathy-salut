#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <check.h>

Suite *
make_gibber_suite (void)
{
    Suite *s = suite_create ("Gibber");

    TCase *tc_empty = tcase_create ("Empty");
    suite_add_tcase (s, tc_empty);

    return s;
}

int
main (void)
{
    int number_failed;

    Suite *s = make_gibber_suite ();
    SRunner *sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
