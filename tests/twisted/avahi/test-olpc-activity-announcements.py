from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, format_event

from twisted.words.xish import xpath, domish


import time
import dbus

CHANNEL_TYPE_TEXT = "org.freedesktop.Telepathy.Channel.Type.Text"
HT_CONTACT = 1
HT_ROOM = 2
HT_CONTACT_LIST = 3

PUBLISHED_NAME = "acttest"
TESTSUITE_PUBLISHED_NAME = "salutacttest"
ACTIVITY_ID = str(time.time())

def get_handle(name, q, conn):
    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)

    handle = 0
    # Wait until the record shows up in publish
    while handle == 0:
        e = q.expect('dbus-signal', signal='MembersChanged', path=publish)
        for h in e.args[1]:
            handle_name = conn.InspectHandles(HT_CONTACT, [h])[0]

            if name == handle_name:
                handle = h

    return handle

def activity_listener_hook(q, event):
    # Assert that the testsuite doesn't announce the activity
    assert event.name != \
      ACTIVITY_ID + ":" + TESTSUITE_PUBLISHED_NAME + "@" + get_host_name(), \
        "salut announced the activity while it shouldn't"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])


    activity_txt = { "type": "org.laptop.HelloMesh",
            "name": "HelloMesh",
            "color": "#7b83c1,#260993",
            "txtvers": "0",
            "activity-id": ACTIVITY_ID,
            "room": ACTIVITY_ID
          }


    # Listen for announcements
    l = AvahiListener(q).listen_for_service("_olpc-activity1._udp")
    q.hook(activity_listener_hook, 'service-added')

    contact_name = PUBLISHED_NAME + "@" + get_host_name()

    activity_name = ACTIVITY_ID + ":" + PUBLISHED_NAME + "@" + get_host_name()

    AvahiAnnouncer(contact_name, "_presence._tcp", 1234, {})
    AvahiAnnouncer(ACTIVITY_ID, "_clique._udp", 12345, {})
    AvahiAnnouncer(activity_name, "_olpc-activity1._udp",
      0, activity_txt)

    # Publish a contact, now get it's handle
    handle = get_handle (contact_name, q, conn)

    while True:
      e = q.expect('dbus-signal', signal = 'ActivitiesChanged')

if __name__ == '__main__':
    exec_test(test, { "published-name": TESTSUITE_PUBLISHED_NAME} )
