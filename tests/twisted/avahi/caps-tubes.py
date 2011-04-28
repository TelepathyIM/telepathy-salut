
"""
Test tubes capabilities with Connection.Interface.ContactCapabilities.DRAFT

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, to test whether the caps cache
  works with tubes

2. Test UpdateCapabilities and test that the avahi txt record is updated test
that the D-Bus signal ContactCapabilitiesChanged is fired for the self handle,
ask Salut for its caps with an iq request, check the reply is correct, and ask
Salut for its caps using D-Bus method GetContactCapabilities. Also check that
GetContactAttributes gives the same results.

- no tube cap at all
- 1 stream tube cap
- 1 D-Bus tube cap
- 1 stream tube + 1 D-Bus tube caps
- 2 stream tube + 2 D-Bus tube caps
- 1 stream tube + 1 D-Bus tube caps, again, just for the fun

"""

import dbus
import sys

from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
from avahitest import txt_get_key
import avahi

from twisted.words.xish import domish, xpath

from servicetest import EventPattern
from saluttest import exec_test, make_result_iq, sync_stream, fixed_features
from xmppstream import setup_stream_listener, connect_to_stream
import ns
from constants import *

from caps_helper import compute_caps_hash, check_caps
from config import PACKAGE_STRING

print "FIXME: disabled because 1-1 tubes are disabled for now"
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

text_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.Text'
    })
text_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

ft_fixed_properties = dbus.Dictionary({
    CHANNEL_TYPE: CHANNEL_TYPE_FILE_TRANSFER,
    TARGET_HANDLE_TYPE: HT_CONTACT
    })
ft_allowed_properties = dbus.Array([
    FT_CONTENT_HASH_TYPE, TARGET_HANDLE, TARGET_ID, FT_CONTENT_TYPE, FT_FILENAME,
    FT_SIZE, FT_CONTENT_HASH, FT_DESCRIPTION,
    FT_DATE, FT_INITIAL_OFFSET, FT_URI
    ])

stream_tube_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE
    })
stream_tube_allowed_properties = dbus.Array([TARGET_HANDLE,
    TARGET_ID, STREAM_TUBE_SERVICE])

dbus_tube_fixed_properties = dbus.Dictionary({
    TARGET_HANDLE_TYPE: HT_CONTACT,
    CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE
    })
dbus_tube_allowed_properties = dbus.Array([TARGET_HANDLE,
    TARGET_ID, DBUS_TUBE_SERVICE_NAME])

daap_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.Service':
        'daap'
    })
daap_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

http_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.StreamTube',
    'org.freedesktop.Telepathy.Channel.Type.StreamTube.Service':
        'http'
    })
http_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

xiangqi_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.ServiceName':
        'com.example.Xiangqi'
    })
xiangqi_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

go_fixed_properties = dbus.Dictionary({
    'org.freedesktop.Telepathy.Channel.TargetHandleType': 1L,
    'org.freedesktop.Telepathy.Channel.ChannelType':
        'org.freedesktop.Telepathy.Channel.Type.DBusTube',
    'org.freedesktop.Telepathy.Channel.Type.DBusTube.ServiceName':
        'com.example.Go'
    })
go_allowed_properties = dbus.Array([
    'org.freedesktop.Telepathy.Channel.TargetHandle',
    ])

def make_presence(from_jid, type, status):
    presence = domish.Element((None, 'presence'))

    if from_jid is not None:
        presence['from'] = from_jid

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    return presence

def presence_add_caps(presence, ver, client, hash=None):
    c = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
    c['node'] = client
    c['ver'] = ver
    if hash is not None:
        c['hash'] = hash
    return presence

# last value of the "ver" key we resolved. We use it to be sure that the
# modified caps has already be announced.
old_ver = ''

def receive_presence_and_ask_caps(q, stream, service):
    global old_ver

    event_avahi, event_dbus = q.expect_many(
            EventPattern('service-resolved', service=service),
            EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )
    signaled_caps = event_dbus.args[0][1]

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
    request = """
<iq from='fake_contact@jabber.org/resource' 
    id='disco1'
    to='salut@jabber.org/resource' 
    type='get'>
  <query xmlns='http://jabber.org/protocol/disco#info'
         node='""" + node + '#' + ver + """'/>
</iq>
"""
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq',
        query_ns='http://jabber.org/protocol/disco#info')
    caps_str = str(xpath.queryForNodes('/iq/query/feature', event.stanza))

    features = []
    for feature in xpath.queryForNodes('/iq/query/feature', event.stanza):
        features.append(feature['var'])

    # Check if the hash matches the announced capabilities
    assert ver == compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, {})

    return (event, caps_str, signaled_caps)

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

def test_tube_caps_from_contact(q, bus, conn, service,
        client):

    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, CONN_IFACE_CONTACTS)

    # send presence with no tube cap
    ver = compute_caps_hash([], [], {})
    txt_record = { "txtvers": "1", "status": "avail",
        "node": client, "ver": ver, "hash": "sha-1"}
    contact_name = "test-caps-tube@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # this is the first presence, Salut connects to the contact
    e = q.expect('incoming-connection', listener = listener)
    incoming = e.connection

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming, iq_type='set',
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
    feature['var'] = 'http://jabber.org/protocol/jingle'
    feature = query.addElement('feature')
    feature['var'] = 'http://jabber.org/protocol/jingle/description/audio'
    feature = query.addElement('feature')
    feature['var'] = 'http://www.google.com/transport/p2p'
    incoming.send(result)

    # no change in ContactCapabilities, so no signal ContactCapabilitiesChanged
    sync_stream(q, incoming)

    # no special capabilities
    basic_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
            caps_via_contacts_iface

    # send presence with generic tube capability
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES], {})
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming,
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES
    incoming.send(result)

    # generic tubes capabilities
    generic_tubes_caps = dbus.Dictionary({contact_handle:
            [(text_fixed_properties, text_allowed_properties),
             (stream_tube_fixed_properties, stream_tube_allowed_properties)]})
    # FIXME: add D-Bus tube once implemented
    #         (dbus_tube_fixed_properties, dbus_tube_allowed_properties)]})
    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    assert len(event.args) == 1
    assert event.args[0] == generic_tubes_caps, generic_tubes_caps

    # send presence with 1 stream tube cap
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES + '/stream#daap'], {})
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming, iq_type='set',
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    signaled_caps = event.args[0][contact_handle]
    assert len(signaled_caps) == 3, signaled_caps # basic caps + daap
    assert (daap_fixed_properties, daap_allowed_properties) in signaled_caps

    # daap capabilities
    daap_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with 1 D-Bus tube cap
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi'], {})
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming, iq_type='set',
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    signaled_caps = event.args[0][contact_handle]
    assert len(signaled_caps) == 3, signaled_caps # basic caps + Xiangqi
    assert (xiangqi_fixed_properties, xiangqi_allowed_properties) in signaled_caps

    # xiangqi capabilities
    xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
        ns.TUBES + '/stream#daap'], {})
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming, iq_type='set',
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    signaled_caps = event.args[0][contact_handle]
    assert len(signaled_caps) == 4, signaled_caps # basic caps + daap+xiangqi
    assert (daap_fixed_properties, daap_allowed_properties) in signaled_caps
    assert (xiangqi_fixed_properties, xiangqi_allowed_properties) in signaled_caps

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with 4 tube caps
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
        ns.TUBES + '/dbus#com.example.Go', ns.TUBES + '/stream#daap', ns.TUBES + '/stream#http'], {})
    announcer.update(txt_record)

    # Salut looks up our capabilities
    event = q.expect('stream-iq', connection = incoming, iq_type='set',
        query_ns='http://jabber.org/protocol/disco#info')
    query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
    assert query_node.attributes['node'] == \
        client + '#' + txt_record['ver']

    # send good reply
    result = make_result_iq(event.stanza)
    query = result.firstChildElement()
    query['node'] = client + '#' + txt_record['ver']
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Xiangqi'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/dbus#com.example.Go'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#daap'
    feature = query.addElement('feature')
    feature['var'] = ns.TUBES + '/stream#http'
    incoming.send(result)

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    signaled_caps = event.args[0][contact_handle]
    assert len(signaled_caps) == 6, signaled_caps # basic caps + 4 tubes
    assert (daap_fixed_properties, daap_allowed_properties) in signaled_caps
    assert (http_fixed_properties, http_allowed_properties) in signaled_caps
    assert (xiangqi_fixed_properties, xiangqi_allowed_properties) in signaled_caps
    assert (go_fixed_properties, go_allowed_properties) in signaled_caps

    # http + daap + xiangqi + go capabilities
    all_tubes_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties,
                xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

    # send presence with both D-Bus and stream tube caps
    txt_record['ver'] = compute_caps_hash([], [ns.TUBES + '/dbus#com.example.Xiangqi',
         ns.TUBES + '/stream#daap'], {})
    announcer.update(txt_record)

    # Salut does not look up our capabilities because of the cache

    event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
    signaled_caps = event.args[0][contact_handle]
    assert len(signaled_caps) == 4, signaled_caps # basic caps + daap+xiangqi
    assert (daap_fixed_properties, daap_allowed_properties) in signaled_caps
    assert (xiangqi_fixed_properties, xiangqi_allowed_properties) in signaled_caps

    # daap + xiangqi capabilities
    daap_xiangqi_caps = dbus.Dictionary({contact_handle:
        [(text_fixed_properties, text_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # test again, to check GetContactCapabilities does not have side effect
    caps = conn_caps_iface.GetContactCapabilities([contact_handle])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [contact_handle], [CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[contact_handle], \
        caps_via_contacts_iface

def test_tube_caps_to_contact(q, bus, conn, service):
    basic_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
         (ft_fixed_properties, ft_allowed_properties)]})
    daap_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties)]})
    xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    daap_xiangqi_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties)]})
    all_tubes_caps = dbus.Dictionary({1:
        [(text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties),
        (stream_tube_fixed_properties, stream_tube_allowed_properties),
        (daap_fixed_properties, daap_allowed_properties),
        (http_fixed_properties, http_allowed_properties),
        (xiangqi_fixed_properties, xiangqi_allowed_properties),
        (go_fixed_properties, go_allowed_properties)]})

    # send presence with no cap info
    txt_record = { "txtvers": "1", "status": "avail"}
    contact_name = "test-caps-tube2@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)
    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port,
            txt_record)

    # Before opening a connection to Salut, wait Salut receives our presence
    # via Avahi. Otherwise, Salut will not allow our connection. We may
    # consider it is a bug in Salut, and we may want Salut to wait a few
    # seconds in case Avahi was slow.
    # See incoming_pending_connection_got_from(): if the SalutContact is not
    # found in the table, we close the connection.
    q.expect('dbus-signal', signal='PresencesChanged')

    # initialise a connection (Salut does not do it because there is no caps
    # here)
    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(HT_CONTACT, [self_handle])[0]
    service.resolve()
    e = q.expect('service-resolved', service = service)
    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)
    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    conn_caps_iface = dbus.Interface(conn, CONN_IFACE_CONTACT_CAPS)
    conn_contacts_iface = dbus.Interface(conn, CONN_IFACE_CONTACTS)

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise nothing
    conn_caps_iface.UpdateCapabilities([])

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == basic_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    sync_stream(q, outbound)

    # Advertise daap
    ret_caps = conn_caps_iface.UpdateCapabilities(
        [('bigclient', [daap_fixed_properties], [])])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == False, caps_str
    assert len(signaled_caps) == 4, signaled_caps # basic caps + daap
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        STREAM_TUBE_SERVICE: 'daap'}, [TARGET_HANDLE]) in signaled_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [('bigclient', [xiangqi_fixed_properties], [])])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 4, signaled_caps # basic caps + daap
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'}, [TARGET_HANDLE]) in signaled_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [('bigclient', [daap_fixed_properties + xiangqi_fixed_properties], [])])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 5, signaled_caps # basic caps + daap+xiangqi
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        STREAM_TUBE_SERVICE: 'daap'}, [TARGET_HANDLE]) in signaled_caps
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'}, [TARGET_HANDLE]) in signaled_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise 4 tubes
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [('bigclient', [daap_fixed_properties, http_fixed_properties,
         go_fixed_properties, xiangqi_fixed_properties], [])])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
            service)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == True, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 7, signaled_caps # basic caps + 4 tubes
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        STREAM_TUBE_SERVICE: 'daap'}, [TARGET_HANDLE]) in signaled_caps
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'}, [TARGET_HANDLE]) in signaled_caps
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        STREAM_TUBE_SERVICE: 'http'}, [TARGET_HANDLE]) in signaled_caps
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        DBUS_TUBE_SERVICE_NAME: 'com.example.Go'}, [TARGET_HANDLE]) in signaled_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == all_tubes_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface

    # Advertise daap + xiangqi
    ret_caps = conn_caps_iface.SetSelfCapabilities(
        [('bigclient', [daap_fixed_properties + xiangqi_fixed_properties], [])])

    # Expect Salut to reply with the correct caps
    event, caps_str, signaled_caps = receive_presence_and_ask_caps(q, outbound,
service)
    assert caps_contain(event, ns.TUBES) == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#daap') == True, caps_str
    assert caps_contain(event, ns.TUBES + '/stream#http') == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Go') \
            == False, caps_str
    assert caps_contain(event, ns.TUBES + '/dbus#com.example.Xiangqi') \
            == True, caps_str
    assert len(signaled_caps) == 5, signaled_caps # basic caps + daap+xiangqi
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        STREAM_TUBE_SERVICE: 'daap'}, [TARGET_HANDLE]) in signaled_caps
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_DBUS_TUBE, TARGET_HANDLE_TYPE: HT_CONTACT,
        DBUS_TUBE_SERVICE_NAME: 'com.example.Xiangqi'}, [TARGET_HANDLE]) in signaled_caps

    # Check our own caps
    caps = conn_caps_iface.GetContactCapabilities([1])
    assert caps == daap_xiangqi_caps, caps
    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn_contacts_iface.GetContactAttributes(
            [1], [CONN_IFACE_CONTACT_CAPS], False) \
            [1][CONN_IFACE_CONTACT_CAPS + '/caps']
    assert caps_via_contacts_iface == caps[1], caps_via_contacts_iface


def test(q, bus, conn):
    # last value of the "ver" key we resolved. We use it to be sure that the
    # modified caps has already be announced.
    old_ver = None

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 0])

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(1, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)
    ver = txt_get_key(e.txt, "ver")
    while ver == old_ver:
        # be sure that the announced caps actually changes
        e = q.expect('service-resolved', service=service)
        ver = txt_get_key(e.txt, "ver")
    old_ver = ver

    caps = compute_caps_hash(['client/pc//%s' % PACKAGE_STRING],
        fixed_features, {})
    check_caps(e.txt, caps)

    client = 'http://telepathy.freedesktop.org/fake-client'

    test_tube_caps_from_contact(q, bus, conn, service,
            client)

    test_tube_caps_to_contact(q, bus, conn, service)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


if __name__ == '__main__':
    exec_test(test)

