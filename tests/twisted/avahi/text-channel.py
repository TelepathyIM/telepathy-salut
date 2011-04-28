from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish


import time
import dbus

CHANNEL_TYPE_TEXT = "org.freedesktop.Telepathy.Channel.Type.Text"
HT_CONTACT = 1
HT_CONTACT_LIST = 3
TEXT_MESSAGE_TYPE_NORMAL = dbus.UInt32(0)

INCOMING_MESSAGE = "Test 123"
OUTGOING_MESSAGE = "Test 321"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = "test-text-channel@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    t = conn.RequestChannel(CHANNEL_TYPE_TEXT, HT_CONTACT, handle,
        True)
    text_channel = make_channel_proxy(conn, t, "Channel.Type.Text")
    text_channel.Send(TEXT_MESSAGE_TYPE_NORMAL, INCOMING_MESSAGE)

    e = q.expect('incoming-connection', listener = listener)
    incoming = e.connection

    e = q.expect('stream-message', connection = incoming)
    assert e.message_type == "chat"
    body = xpath.queryForNodes("/message/body",  e.stanza )
    assert map(str, body) == [ INCOMING_MESSAGE ]

    # drop the connection
    incoming.transport.loseConnection()

    # Now send a message to salut
    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]


    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()


    e = q.expect('service-resolved', service = service)

    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason

    e = q.expect('stream-opened', connection = outbound)

    # connected to salut, now send a message
    message = domish.Element(('', 'message'))
    message['type'] = "chat"
    message.addElement('body', content=OUTGOING_MESSAGE)

    e.connection.send(message)

    e = q.expect('dbus-signal', signal='Received')
    assert e.args[2] == handle
    assert e.args[3] == TEXT_MESSAGE_TYPE_NORMAL
    assert e.args[5] == OUTGOING_MESSAGE


if __name__ == '__main__':
    exec_test(test)
