"""
Test that aliases are built as expected from contacts' TXT records, and that
the details show up correctly in ContactInfo.
"""

from servicetest import assertContains, assertEquals, assertLength, call_async
from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer
from avahitest import get_host_name
import constants as cs

import time

def wait_for_aliases_changed(q, handle):
    e = q.expect('dbus-signal', signal='AliasesChanged',
            predicate=lambda e: handle in e.args[0])
    alias = e.args[0][handle]
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
    first = txt.get('1st', '')
    last = txt.get('last', '')

    if first != '' or last != '':
        values = [last, first, '', '', '']
        assertContains(('n', [], values), info)

        fn = ' '.join([ x for x in [first, last] if x != ''])
        assertContains(('fn', [], [fn]), info)
    else:
        assertOmitsField('n', info)
        assertOmitsField('fn', info)

    email = txt.get('email', '')
    if email != '':
        assertContains(('email', ['type=internet'], [email]), info)
    else:
        assertOmitsField('email', info)

    jid = txt.get('jid', '')
    if jid != '':
        assertContains(('x-jabber', [], [jid]), info)
    else:
        assertOmitsField('x-jabber', info)

def check_all_contact_info_methods(conn, handle, keys):
    attrs = conn.Contacts.GetContactAttributes([handle],
        [cs.CONN_IFACE_CONTACT_INFO])[handle]
    info = attrs[cs.CONN_IFACE_CONTACT_INFO + "/info"]
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
          ('fn', [], cs.CONTACT_INFO_FIELD_FLAG_PARAMETERS_EXACT, 1),
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
      # Contact publishes just one of 1st and last
      ("last", { "last": "last" }, True),
      ("1st", { "1st": "1st"}, True),
      # Contact publishes a meaningful value for one of 1st and last, and an
      # empty value for the other one and for "nick". Empty values should be
      # treated as if missing.
      ("last", { "last": "last", "1st": "", "nick": "" }, True),
      ("1st", { "1st": "1st", "last": "", "nick": "" }, True),
      # When a contact publishes both 1st and last, we have to join them
      # together in a stupid anglo-centric way, like iChat does.
      ("1st last", { "1st": "1st", "last": "last" }, True),
      # Nickname should be preferred as the alias to 1st or last. Since we
      # don't report nicknames in ContactInfo, and nothing else has changed
      # from the last update, no ContactInfo changes should be announced.
      ("nickname", { "1st": "1st", "last": "last", "nick": "nickname" }, False),
      # If the contact stops publishing any of this stuff, we should fall back
      # to their JID as their alias.
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
            [cs.CONN_IFACE_ALIASING])[handle]
        assertEquals(alias, attrs[cs.CONN_IFACE_ALIASING + "/alias"])

        check_all_contact_info_methods(conn, handle, dict)

    for keys in [ # Check a few neat transitions, with no empty fields
                  { "email": "foo@bar.com" },
                  { "jid": "nyan@gmail.com", "email": "foo@bar.com" },
                  { "jid": "orly@example.com" },
                  # Check that empty fields are treated as if omitted
                  { "email": "foo@bar.com", "jid": "" },
                  { "jid": "orly@example.com", "email": "" },
                ]:
        txt = basic_txt.copy()
        txt.update(keys)

        announcer.set(txt)
        info = wait_for_contact_info_changed(q, handle)
        check_contact_info(info, keys)

        check_all_contact_info_methods(conn, handle, keys)

    # Try an invalid handle. Request should return InvalidHandle.
    # (Technically so should RefreshContactInfo but I am lazy.)
    call_async(q, conn.ContactInfo, 'RequestContactInfo', 42)
    q.expect('dbus-error', method='RequestContactInfo', name=cs.INVALID_HANDLE)

    # Try a valid handle for whom we have no data from the network.
    # Request should fail.
    h = conn.Contacts.GetContactByID('rthrtha@octopus', [])[0]
    call_async(q, conn.ContactInfo, 'RequestContactInfo', h)
    q.expect('dbus-error', method='RequestContactInfo', name=cs.NOT_AVAILABLE)

if __name__ == '__main__':
    exec_test(test)
