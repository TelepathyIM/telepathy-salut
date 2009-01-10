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
C_T_DTUBE = 'org.freedesktop.Telepathy.Channel.Type.DBusTube.DRAFT'
HT_CONTACT = 1

invalid_service_names = [ 'invalidServiceName'
                        , 'one ten hundred thousand million'
                        , 'me.is.it.you?.hello.you.sexy.sons.o.@#$%.heh'
                        , ':1.1'
                        , ''
                        ]

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    requestotron = dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection.Interface.Requests')

    for invalid_service_name in invalid_service_names:
        try:
            requestotron.CreateChannel(
                {'org.freedesktop.Telepathy.Channel.ChannelType':
                     C_T_DTUBE,
                 'org.freedesktop.Telepathy.Channel.TargetHandleType':
                     HT_CONTACT,
                 'org.freedesktop.Telepathy.Channel.TargetID':
                     'alice',
                 C_T_DTUBE + '.ServiceName':
                    invalid_service_name
                });
        except dbus.DBusException, e:
            assert e.get_dbus_name() == \
                'org.freedesktop.Telepathy.Errors.InvalidArgument', \
                (e.get_dbus_name(), invalid_service_name)
        else:
            assert False

if __name__ == '__main__':
    exec_test(test)
