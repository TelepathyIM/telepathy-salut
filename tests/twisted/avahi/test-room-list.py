from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish


import time
import dbus

CHANNEL_TYPE_ROOMLIST = 'org.freedesktop.Telepathy.Channel.Type.RoomList'
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    t = conn.RequestChannel(CHANNEL_TYPE_ROOMLIST, 0, 0, True)
    channel = make_channel_proxy(conn, t, "Channel.Type.RoomList")

    channel.ListRooms()

    q.expect('dbus-signal', signal='ListingRooms', args=[True])

    e = q.expect('dbus-signal', signal='GotRooms')
    rooms = e.args[0]
    assert rooms == []

    q.expect('dbus-signal', signal='ListingRooms', args=[False])

    # TODO: announce some Clique rooms and check is they are properly listed

if __name__ == '__main__':
    exec_test(test)
