from saluttest import exec_test
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import dbus
import os
import errno
import string

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy, Event, EventPattern, call_async, \
         tp_name_prefix, sync_dbus

from twisted.words.xish import xpath, domish
from twisted.internet.protocol import Factory, Protocol, ClientCreator
from twisted.internet import reactor

PUBLISHED_NAME="test-tube"

CHANNEL_TYPE_TUBES = "org.freedesktop.Telepathy.Channel.Type.Tubes"
HT_CONTACT = 1
HT_ROOM = 2
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_UNIX = dbus.UInt32(0)
SOCKET_ADDRESS_TYPE_IPV4 = dbus.UInt32(2)
SOCKET_ADDRESS_TYPE_IPV6 = dbus.UInt32(3)
SOCKET_ACCESS_CONTROL_LOCALHOST = dbus.UInt32(0)

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

test_string = "This string travels on a tube !"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    try:
        requestotron.CreateChannel(
            {'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT',
             'org.freedesktop.Telepathy.Channel.TargetHandleType':
                 HT_CONTACT,
             'org.freedesktop.Telepathy.Channel.TargetID':
                 'alice',
             'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT.ServiceName':
                'invalidServiceName'
            });
    except dbus.DBusException, e:
        assert e.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.InvalidArgument'
    else:
        assert False

if __name__ == '__main__':
    exec_test(test)
