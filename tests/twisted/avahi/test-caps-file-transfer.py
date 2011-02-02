
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities.DRAFT

1. Check if Salut advertise the OOB caps

2. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- capa announced with FT
- capa announced without FT
- no capabilites announced (assume FT is supported)
"""

import dbus

from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
from avahitest import txt_get_key
import avahi

from twisted.words.xish import xpath

from servicetest import EventPattern
from saluttest import exec_test, make_result_iq
from xmppstream import setup_stream_listener
import ns
from constants import *

from caps_helper import compute_caps_hash
from config import PACKAGE_STRING

# last value of the "ver" key we resolved. We use it to be sure that the
# modified caps has already be announced.
old_ver = ''

def receive_presence_and_ask_caps(q, stream, service, contact_name):
    global old_ver

    event_avahi = q.expect('service-resolved', service=service)

    ver = txt_get_key(event_avahi.txt, "ver")
    while ver == old_ver:
        # be sure that the announced caps actually changes
        event_avahi = q.expect('service-resolved', service=service)
        ver = txt_get_key(event_avahi.txt, "ver")
    old_ver = ver

    hash = txt_get_key(event_avahi.txt, "hash")
    node = txt_get_key(event_avahi.txt, "node")
    assert hash == 'sha-1'

    # ask caps
    # FIXME: this only works because Salut ignores @to
    request = """
<iq from='""" + contact_name + """'
    id='receive-presence-and-ask-caps'
    to='salut@jabber.org/resource'
    type='get'>
  <query xmlns='http://jabber.org/protocol/disco#info'
         node='""" + node + '#' + ver + """'/>
</iq>
"""
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq', iq_type='result',
            to=contact_name, iq_id='receive-presence-and-ask-caps',
            query_ns='http://jabber.org/protocol/disco#info')
    caps_str = str(xpath.queryForNodes('/iq/query/feature', event.stanza))

    features = []
    for feature in xpath.queryForNodes('/iq/query/feature', event.stanza):
        features.append(feature['var'])

    # Check if the hash matches the announced capabilities
    assert ver == compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, [])
    assert ns.X_OOB in features
    assert ns.IQ_OOB in features

def caps_contain(event, cap):
    node = xpath.queryForNodes('/iq/query/feature[@var="%s"]'
            % cap,
            event.stanza)
    if node is None:
        return False
    if len(node) != 1:
        return False
    var = node[0].attributes['var']
    if var is None:
        return False
    return var == cap

def test_ft_caps_from_contact(q, bus, conn, client):

    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, CONN_IFACE_CONTACTS)

    # send presence with FT capa
    ver = compute_caps_hash([], [ns.IQ_OOB], [])
    txt_record = { "txtvers": "1", "status": "avail",
        "node": client, "ver": ver, "hash": "sha-1"}
    contact_name = "test-caps-ft@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # this is the first presence, Salut connects to the contact
    e = q.expect('incoming-connection', listener = listener)
    incoming = e.connection

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + ver, (query_node.attributes['node'], client, ver)

    contact_handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + ver

    feature = query.addElement('feature')
    feature['var'] = ns.IQ_OOB
    incoming.send(result)

    # FT capa is announced
    e = q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
            predicate=lambda e: contact_handle in e.args[0])
    caps = e.args[0][contact_handle]
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_FILE_TRANSFER,
             TARGET_HANDLE_TYPE: HT_CONTACT},
            [FT_CONTENT_HASH_TYPE, TARGET_HANDLE, TARGET_ID, FT_CONTENT_TYPE, FT_FILENAME,
                FT_SIZE, FT_CONTENT_HASH, FT_DESCRIPTION,
                FT_DATE, FT_INITIAL_OFFSET, FT_URI]) in caps

    caps_get = conn_caps_iface.GetContactCapabilities([contact_handle])[contact_handle]
    assert caps == caps_get

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps, caps_via_contacts_iface

    # check if Salut announces the OOB capa
    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
            protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    receive_presence_and_ask_caps(q, incoming, service, contact_name)

    # capa announced without FT
    ver = compute_caps_hash([], ["http://telepathy.freedesktop.org/xmpp/pony"], [])
    txt_record = { "txtvers": "1", "status": "avail",
        "node": client, "ver": ver, "hash": "sha-1"}
    contact_name = "test-caps-ft2@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # this is the first presence, Salut connects to the contact
    e = q.expect('incoming-connection', listener = listener)
    incoming = e.connection

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + ver, (query_node.attributes['node'], client, ver)

    contact_handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + ver

    feature = query.addElement('feature')
    feature['var'] = "http://telepathy.freedesktop.org/xmpp/pony"
    incoming.send(result)

    # the FT capability is not announced
    e = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    caps = e.args[0][contact_handle]
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_FILE_TRANSFER,
             TARGET_HANDLE_TYPE: HT_CONTACT},
            [FT_CONTENT_HASH_TYPE, TARGET_HANDLE, TARGET_ID, FT_CONTENT_TYPE, FT_FILENAME, FT_SIZE,
                FT_CONTENT_HASH, FT_DESCRIPTION,
                FT_DATE, FT_INITIAL_OFFSET, FT_URI]) not in caps

    caps_get = conn_caps_iface.GetContactCapabilities([contact_handle])[contact_handle]
    assert caps == caps_get


    # no capabilites announced (assume FT is supported to insure interop)
    txt_record = { "txtvers": "1", "status": "avail"}
    contact_name = "test-caps-ft-no-capa2@" + get_host_name()
    contact_handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # FT capa is announced
    e = q.expect('dbus-signal', signal='ContactCapabilitiesChanged',
            predicate=lambda e: contact_handle in e.args[0].keys())

    caps = e.args[0][contact_handle]
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_FILE_TRANSFER,
             TARGET_HANDLE_TYPE: HT_CONTACT},
            [FT_CONTENT_HASH_TYPE, TARGET_HANDLE, TARGET_ID, FT_CONTENT_TYPE, FT_FILENAME,
                FT_SIZE, FT_CONTENT_HASH, FT_DESCRIPTION,
                FT_DATE, FT_INITIAL_OFFSET, FT_URI]) in caps

    caps_get = conn_caps_iface.GetContactCapabilities([contact_handle])[contact_handle]
    assert caps == caps_get

def test(q, bus, conn):
    # last value of the "ver" key we resolved. We use it to be sure that the
    # modified caps has already be announced.
    old_ver = None

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 0])

    # check our own capabilities
    self_handle = conn.GetSelfHandle()
    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPS)
    caps = conn_caps_iface.GetContactCapabilities([self_handle])[self_handle]
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_FILE_TRANSFER,
             TARGET_HANDLE_TYPE: HT_CONTACT},
            [FT_CONTENT_HASH_TYPE, TARGET_HANDLE, TARGET_ID, FT_CONTENT_TYPE, FT_FILENAME,
                FT_SIZE, FT_CONTENT_HASH, FT_DESCRIPTION,
                FT_DATE, FT_INITIAL_OFFSET, FT_URI]) in caps

    client = 'http://telepathy.freedesktop.org/fake-client'
    test_ft_caps_from_contact(q, bus, conn, client)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)
