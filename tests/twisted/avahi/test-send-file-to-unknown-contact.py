import httplib
import urlparse
import dbus
import socket
import md5

from saluttest import exec_test
from avahitest import AvahiAnnouncer
from avahitest import get_host_name

from xmppstream import setup_stream_listener
from servicetest import make_channel_proxy, EventPattern, call_async

from twisted.words.xish import domish, xpath

from dbus import PROPERTIES_IFACE

CONNECTION_INTERFACE_REQUESTS = 'org.freedesktop.Telepathy.Connection.Interface.Requests'
CHANNEL_INTERFACE ='org.freedesktop.Telepathy.Channel'
CHANNEL_TYPE_FILE_TRANSFER = 'org.freedesktop.Telepathy.Channel.Type.FileTransfer.DRAFT'

HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

FT_STATE_NONE = 0
FT_STATE_PENDING = 1
FT_STATE_ACCEPTED = 2
FT_STATE_OPEN = 3
FT_STATE_COMPLETED = 4
FT_STATE_CANCELLED = 5

FT_STATE_CHANGE_REASON_NONE = 0
FT_STATE_CHANGE_REASON_REQUESTED = 1

FILE_HASH_TYPE_MD5 = 1

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_IPV4 = 2

SOCKET_ACCESS_CONTROL_LOCALHOST = 0

# File to Offer
FILE_DATA = "That works!"
FILE_SIZE = len(FILE_DATA)
FILE_NAME = 'test.txt'
FILE_CONTENT_TYPE = 'text/plain'
FILE_DESCRIPTION = 'A nice file to test'
FILE_HASH_TYPE = FILE_HASH_TYPE_MD5
m = md5.new()
m.update(FILE_DATA)
FILE_HASH = m.hexdigest()

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    contact_name = "test-file-receiver@" + get_host_name()
    handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]

    requests_iface = dbus.Interface(conn, CONNECTION_INTERFACE_REQUESTS)

    # check if we can request FileTransfer channels
    properties = conn.GetAll(
        'org.freedesktop.Telepathy.Connection.Interface.Requests',
        dbus_interface='org.freedesktop.DBus.Properties')

    assert ({CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
             CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT},
            [CHANNEL_INTERFACE + '.TargetHandle',
             CHANNEL_INTERFACE + '.TargetID',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentType',
             CHANNEL_TYPE_FILE_TRANSFER + '.Filename',
             CHANNEL_TYPE_FILE_TRANSFER + '.Size',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType',
             CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash',
             CHANNEL_TYPE_FILE_TRANSFER + '.Description',
             CHANNEL_TYPE_FILE_TRANSFER + '.Date',
             CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset'],
         ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    call_async(q, requests_iface, 'CreateChannel',
            {CHANNEL_INTERFACE + '.ChannelType': CHANNEL_TYPE_FILE_TRANSFER,
            CHANNEL_INTERFACE + '.TargetHandleType': HT_CONTACT,
            CHANNEL_INTERFACE + '.TargetHandle': handle,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentType': FILE_CONTENT_TYPE,
            CHANNEL_TYPE_FILE_TRANSFER + '.Filename': FILE_NAME,
            CHANNEL_TYPE_FILE_TRANSFER + '.Size': FILE_SIZE,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType': FILE_HASH_TYPE,
            CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash': FILE_HASH,
            CHANNEL_TYPE_FILE_TRANSFER + '.Description': FILE_DESCRIPTION,
            CHANNEL_TYPE_FILE_TRANSFER + '.Date':  1225278834,
            CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset': 0,
            })

    e = q.expect('dbus-error')
    assert e.error.get_dbus_name() == 'org.freedesktop.Telepathy.Errors.NotAvailable'

if __name__ == '__main__':
    exec_test(test)
