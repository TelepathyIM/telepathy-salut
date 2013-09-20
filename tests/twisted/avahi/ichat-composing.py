from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish


import time
import dbus

import constants as cs

OUTGOING_MESSAGE = "This is a message"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    contact_name = "test-text-channel@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()


    e = q.expect('service-resolved', service = service)

    xmpp_connection = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason

    e = q.expect('stream-opened', connection = xmpp_connection)

    # connected to salut, now send a messages as composing part 
    # here be sillyness
    parts = OUTGOING_MESSAGE.split(" ")

    for x in xrange(1,len(parts)):
        message = domish.Element(('', 'message'))
        message.addElement('body', content=' '.join(parts[:x]))
        event = message.addElement('x', 'jabber:x:event')
        event.addElement('composing')
        event.addElement('id')
        xmpp_connection.send(message)

    message = domish.Element(('', 'message'))
    message.addElement('body', content=OUTGOING_MESSAGE)
    event = message.addElement('x', 'jabber:x:event')
    event.addElement('composing')
    xmpp_connection.send(message)

    e = q.expect('dbus-signal', signal='Received')
    assert e.args[2] == handle
    assert e.args[5] == OUTGOING_MESSAGE

if __name__ == '__main__':
    exec_test(test)
