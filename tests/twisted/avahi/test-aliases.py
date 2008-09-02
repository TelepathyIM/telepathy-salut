from saluttest import exec_test
from avahitest import AvahiAnnouncer
from avahitest import get_host_name

import time

PUBLISHED_NAME="test-register"
FIRST_NAME="lastname"
LAST_NAME="lastname"

HT_CONTACT = 1
HT_CONTACT_LIST = 3

def wait_for_aliases_changed(q, handle):
    while True:
        e = q.expect('dbus-signal', signal='AliasesChanged')
        for x in e.args:
            (h, a) = x[0]
            if h == handle:
                return a

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = "aliastest@" + get_host_name()
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", 1234, basic_txt)

    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)

    handle = 0
    # Wait until the record shows up in publish
    while handle == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=publish)
        for h in e.args[1]:
            name = conn.InspectHandles(HT_CONTACT, [h])[0]
            if name == contact_name:
                handle = h
                break

    alias = wait_for_aliases_changed(q, handle)
    assert alias == contact_name, alias

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
