"""
Test that aliases are built as expected from contacts' TXT records, and that
the details show up correctly in ContactInfo.
"""

from servicetest import assertContains, assertEquals, assertLength
from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer
from avahitest import get_host_name
import constants as cs

import time

def wait_for_aliases_changed(q, handle):
    e = q.expect('dbus-signal', signal='AliasesChanged',
            predicate=lambda e: e.args[0][0][0] == handle)
    _, alias = e.args[0][0]
    return alias

def wait_for_contact_info_changed(q, handle):
    e = q.expect('dbus-signal', signal='ContactInfoChanged',
            predicate=lambda e: e.args[0] == handle)
    _, info = e.args
    return info

def assertOmitsField(field_name, fields):
    def matches(field):
        return field[0] == field_name

    assertLength(0, filter(matches, fields))

def check_contact_info(info, txt):
    if '1st' in txt or 'last' in txt:
        values = [txt.get('last', ''), txt.get('1st', ''), '', '', '']
        assertContains(('n', [], values), info)
    else:
        assertOmitsField('n', info)

    if 'email' in txt:
        assertContains(('email', ['type=internet'], [txt['email']]), info)
    else:
        assertOmitsField('email', info)

    if 'jid' in txt:
        assertContains(('x-jabber', [], [txt['jid']]), info)
    else:
        assertOmitsField('x-jabber', info)

def check_all_contact_info_methods(conn, handle, keys):
    attrs = conn.Contacts.GetContactAttributes([handle],
        [cs.CONN_IFACE_CONTACT_INFO], True)[handle]
    info = attrs[cs.CONN_IFACE_CONTACT_INFO + "/info"]
    check_contact_info(info, keys)

    info = conn.ContactInfo.GetContactInfo([handle])[handle]
    check_contact_info(info, keys)

    info = conn.ContactInfo.RequestContactInfo(handle)
    check_contact_info(info, keys)

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

    for (alias, dict, expect_contact_info_changed) in [
      ("last", { "last": "last" }, True),
      ("1st", { "1st": "1st"}, True),
      ("1st last", { "1st": "1st", "last": "last" }, True),
      ("nickname", { "1st": "1st", "last": "last", "nick": "nickname" },
       # We don't report 'nick' in ContactInfo, and nothing else has changed.
       False),
      (contact_name, {}, True) ]:
        txt = basic_txt.copy()
        txt.update(dict)

        announcer.set(txt)

        a = wait_for_aliases_changed (q, handle)
        assert a == alias, (a, alias, txt)

        if expect_contact_info_changed:
            info = wait_for_contact_info_changed(q, handle)
            check_contact_info(info, dict)

        attrs = conn.Contacts.GetContactAttributes([handle],
            [cs.CONN_IFACE_ALIASING], True)[handle]
        assertEquals(alias, attrs[cs.CONN_IFACE_ALIASING + "/alias"])

        check_all_contact_info_methods(conn, handle, dict)

    for keys in [ { "email": "foo@bar.com" },
                  { "jid": "nyan@gmail.com", "email": "foo@bar.com" },
                  { "jid": "orly@example.com" },
                ]:
        txt = basic_txt.copy()
        txt.update(keys)

        announcer.set(txt)
        info = wait_for_contact_info_changed(q, handle)
        check_contact_info(info, keys)

        check_all_contact_info_methods(conn, handle, keys)

if __name__ == '__main__':
    exec_test(test)
