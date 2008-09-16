from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiRecordAnnouncer, AvahiListener
from avahitest import get_host_name, get_domain_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, format_event

from twisted.words.xish import xpath, domish


import time
import dbus
import socket

CHANNEL_TYPE_TEXT = "org.freedesktop.Telepathy.Channel.Type.Text"
HT_CONTACT = 1
HT_ROOM = 2
HT_CONTACT_LIST = 3

PUBLISHED_NAME = "acttest"
TESTSUITE_PUBLISHED_NAME = "salutacttest"
ACTIVITY_ID = str(time.time())

joined_activity = False


def compare_handle (name, conn, handle):
  handle_name = conn.InspectHandles(HT_CONTACT, [handle])[0]
  return name == handle_name

def wait_for_handle(name, q, conn):
    publish_handle = conn.RequestHandles(HT_CONTACT_LIST, ["publish"])[0]
    publish = conn.RequestChannel(
        "org.freedesktop.Telepathy.Channel.Type.ContactList",
        HT_CONTACT_LIST, publish_handle, False)

    proxy = make_channel_proxy(conn, publish, "Channel.Interface.Group")

    for h in proxy.GetMembers():
        if compare_handle(name, conn, h):
            return h

    # Wait until the record shows up in publish
    while True:
        e = q.expect('dbus-signal', signal='MembersChanged', path=publish)
        for h in e.args[1]:
            if compare_handle(name, conn, h):
                return h

def activity_listener_hook(q, event):
    # Assert that the testsuite doesn't announce the activity
    global joined_activity
    if joined_activity:
        return

    assert event.name != \
      ACTIVITY_ID + ":" + TESTSUITE_PUBLISHED_NAME + "@" + get_host_name(), \
        "salut announced the activity while it shouldn't"

def announce_address(hostname, address):
    "Announce IN A record, address is assume to be ipv4"

    data = reduce (lambda x, y: (x << 8) + int(y), address.split("."), 0)
    ndata = socket.htonl(data)
    rdata = [ (ndata >> (24 - x)) & 0xff for x in xrange(0, 32, 8)]

    AvahiRecordAnnouncer(hostname, 0x1, 0x01, rdata)

def test(q, bus, conn):
    global joined_activity

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

    act_hostname = ACTIVITY_ID + ":" + PUBLISHED_NAME + \
        "._clique._udp." + get_domain_name()
    act_address = "239.253.70.70"

    announce_address(act_hostname, act_address)

    # FIXME, if we use the same name as the running salut will MembersChanged
    # isn't signalled later on, needs to be fixed.
    AvahiAnnouncer(ACTIVITY_ID + ":" + PUBLISHED_NAME,
        "_clique._udp", 12345, {}, hostname = act_hostname)
    AvahiAnnouncer(activity_name, "_olpc-activity1._udp",
      0, activity_txt)

    # Publish a contact, now get it's handle
    handle = wait_for_handle (contact_name, q, conn)

    # Assert that the remote handles signals it joined the activity
    while True:
      e = q.expect('dbus-signal', signal = 'ActivitiesChanged')
      if e.args[0] == handle and e.args[1] != []:
          assert len(e.args[1]) == 1
          assert e.args[1][0][0] == ACTIVITY_ID
          activity_handle = e.args[1][0][1]
          break

    act_properties = conn.ActivityProperties.GetProperties(activity_handle)
    assert act_properties['private'] == False
    assert act_properties['color'] == activity_txt['color']
    assert act_properties['name'] == activity_txt['name']
    assert act_properties['type'] == activity_txt['type']

    room_channel = conn.RequestChannel(CHANNEL_TYPE_TEXT,
        HT_ROOM, activity_handle, True)

    q.expect('dbus-signal', signal='MembersChanged', path=room_channel,
        args = [u'', [1L], [], [], [], 1L, 0L])

    # Make it public that we joined the activity
    joined_activity = True
    conn.BuddyInfo.SetActivities([(ACTIVITY_ID, activity_handle)])

    q.expect('service-added',
        name = ACTIVITY_ID + ":" + TESTSUITE_PUBLISHED_NAME +
            "@" + get_host_name())

    conn.BuddyInfo.SetActivities([])

    q.expect('service-removed',
        name = ACTIVITY_ID + ":" + TESTSUITE_PUBLISHED_NAME +
            "@" + get_host_name())

if __name__ == '__main__':
    exec_test(test, { "published-name": TESTSUITE_PUBLISHED_NAME}, timeout=15)
