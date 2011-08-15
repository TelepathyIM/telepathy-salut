"""
Test that aliases are built as expected from contacts' TXT records, and that
the details show up correctly in ContactInfo.
"""

from servicetest import assertContains, assertEquals
from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer
from avahitest import get_host_name
import constants as cs

import time

def wait_for_aliases_changed(q, handle):
    while True:
        e = q.expect('dbus-signal', signal='AliasesChanged')
        for x in e.args:
            (h, a) = x[0]
            if h == handle:
                return a

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED])

    assertContains(cs.CONN_IFACE_CONTACT_INFO,
        conn.Properties.Get(cs.CONN, "Interfaces"))
    ci_props = conn.Properties.GetAll(cs.CONN_IFACE_CONTACT_INFO)
    assertEquals(cs.CONTACT_INFO_FLAG_PUSH, ci_props['ContactInfoFlags'])
    assertEquals(
        [ ('n', [], cs.CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1),
          ('email', ['type=internet'],
           cs.CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1),
          ('x-jabber', [], cs.CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1),
        ],
        ci_props['SupportedFields'])

    # Just to verify that RCI does approximately nothing and doesn't crash.
    conn.ContactInfo.RefreshContactInfo([21,42,88])

    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = "aliastest@" + get_host_name()
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", 1234, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)
    alias = wait_for_aliases_changed(q, handle)
    assertEquals(contact_name, alias)

    for (alias, dict) in [
      ("last", { "last": "last" }),
      ("1st", { "1st": "1st"}),
      ("1st last", { "1st": "1st", "last": "last" }),
      ("nickname", { "1st": "1st", "last": "last", "nick": "nickname" }),
      (contact_name, { }) ]:
        txt = basic_txt.copy()
        txt.update(dict)

        announcer.set(txt)

        a = wait_for_aliases_changed (q, handle)
        assert a == alias, (a, alias, txt)

if __name__ == '__main__':
    exec_test(test)
