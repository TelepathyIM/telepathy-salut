from saluttest import exec_test
import dbus
from dbus.service import method, signal, Object

from servicetest import (wrap_channel, call_async, EventPattern, Event,
        assertSameSets)

import constants as cs
import tubetestutil as t

sample_parameters = dbus.Dictionary({
    's': 'hello',
    'ay': dbus.ByteArray('hello'),
    'u': dbus.UInt32(123),
    'i': dbus.Int32(-123),
    }, signature='sv')

muc_name = "test-two-muc-stream-tubes"

def check_dbus_names(tube, members):
    names = tube.Properties.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames')
    assert set(names.keys()) == set(members), names.keys()

SERVICE = "im.telepathy.v1.Tube.Test"
IFACE = SERVICE
PATH = "/im/telepathy/v1/Tube/Test"

print "FIXME: MUC tubes tests are currently broken: fdo#69223"
# exiting 77 causes automake to consider the test to have been skipped
raise SystemExit(77)

class Test(Object):
    def __init__(self, tube, q):
        super(Test, self).__init__(tube, PATH)
        self.tube = tube
        self.q = q

    @signal(dbus_interface=IFACE, signature='s')
    def MySig(self, arg):
        pass

    @method(dbus_interface=IFACE, in_signature='u', out_signature='u')
    def MyMethod(self, arg):
        self.q.append(Event('tube-dbus-call', method='MyMethod', args=[arg]))
        return arg * 10

def test(q, bus, conn):

    contact1_name, conn2, contact2_name, contact2_handle_on_conn1,\
        contact1_handle_on_conn2 = t.connect_two_accounts(q, bus, conn)

    conn1_self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    conn2_self_handle = conn2.Properties.Get(cs.CONN, "SelfHandle")

    # first connection: join muc
    muc_handle1, group1 = t.join_muc(q, conn, muc_name)

    # Can we request muc D-Bus tubes?
    properties = conn.GetAll(cs.CONN,
        dbus_interface=cs.PROPERTIES_IFACE)

    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM},
         [cs.TARGET_HANDLE, cs.TARGET_ID, cs.DBUS_TUBE_SERVICE_NAME]
        ) in properties.get('RequestableChannelClasses'),\
                 properties['RequestableChannelClasses']

    # request a stream tube channel (new API)
    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    requestotron.CreateChannel({
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE,
        cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
        cs.TARGET_ID: muc_name,
        cs.DBUS_TUBE_SERVICE_NAME: 'com.example.TestCase'})

    e = q.expect('dbus-signal', signal='NewChannel')

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn.Get(cs.CONN_IFACE_REQUESTS, 'Channels', dbus_interface=cs.PROPERTIES_IFACE,
        byte_arrays=True)

    path, props = e.args

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE
    assert props[cs.REQUESTED] == True
    assertSameSets([cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_TUBE],
            props[cs.INTERFACES])
    assert props[cs.DBUS_TUBE_SERVICE_NAME] == 'com.example.TestCase'
    assert props[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS] == [
        cs.SOCKET_ACCESS_CONTROL_CREDENTIALS, cs.SOCKET_ACCESS_CONTROL_LOCALHOST]

    contact1_tube = wrap_channel(bus.get_object(conn.bus_name, path), 'DBusTube')
    tube1_path = path

    assert (path, props) in all_channels, (path, props)

    state = contact1_tube.Properties.Get(cs.CHANNEL_IFACE_TUBE, 'State')
    assert state == cs.TUBE_CHANNEL_STATE_NOT_OFFERED

    call_async(q, contact1_tube.DBusTube, 'Offer', sample_parameters,
        cs.SOCKET_ACCESS_CONTROL_CREDENTIALS)

    _, e = q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged',
            args=[cs.TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-return', method='Offer'))

    tube_addr1 = e.value[0]

    state = contact1_tube.Properties.Get(cs.CHANNEL_IFACE_TUBE, 'State')
    assert state == cs.TUBE_CHANNEL_STATE_OPEN

    check_dbus_names(contact1_tube, [conn1_self_handle])

    t.invite_to_muc(q, group1, conn2, contact2_handle_on_conn1, contact1_handle_on_conn2)

    # tubes channel is created
    e, dbus_names_e = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='DBusNamesChanged', interface=cs.CHANNEL_TYPE_DBUS_TUBE))

    channels = e.args[0]
    assert len(channels) == 1

    # get the list of all channels to check that newly announced ones are in it
    all_channels = conn2.Get(cs.CONN_IFACE_REQUESTS, 'Channels', dbus_interface=cs.PROPERTIES_IFACE,
        byte_arrays=True)

    path, props = channels[0]

    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_DBUS_TUBE
    assert props[cs.REQUESTED] == False
    assertSameSets([cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_TUBE],
            props[cs.INTERFACES])
    assert props[cs.TUBE_PARAMETERS] == sample_parameters
    assert props[cs.DBUS_TUBE_SERVICE_NAME] == 'com.example.TestCase'
    assert props[cs.DBUS_TUBE_SUPPORTED_ACCESS_CONTROLS] == [
        cs.SOCKET_ACCESS_CONTROL_CREDENTIALS, cs.SOCKET_ACCESS_CONTROL_LOCALHOST]

    contact2_tube = wrap_channel(bus.get_object(conn.bus_name, path), 'DBusTube')
    tube2_path = path

    assert (path, props) in all_channels, (path, props)

    # second connection: check DBusNamesChanged signal
    assert dbus_names_e.path == tube2_path
    added, removed = dbus_names_e.args
    assert added.keys() == [contact1_handle_on_conn2]
    assert removed == []

    state = contact2_tube.Properties.Get(cs.CHANNEL_IFACE_TUBE, 'State')
    assert state == cs.TUBE_CHANNEL_STATE_LOCAL_PENDING

    # first connection: contact2 is not in the tube yet
    check_dbus_names(contact1_tube, [conn1_self_handle])

    # second connection: accept the tube (new API)
    tube_addr2 = unix_socket_adr = contact2_tube.DBusTube.Accept(cs.SOCKET_ACCESS_CONTROL_CREDENTIALS)

    state = contact2_tube.Properties.Get(cs.CHANNEL_IFACE_TUBE, 'State')
    assert state == cs.TUBE_CHANNEL_STATE_OPEN

    e, dbus_names_e = q.expect_many(
        EventPattern('dbus-signal', signal='TubeChannelStateChanged',
            path=tube2_path, args=[cs.TUBE_CHANNEL_STATE_OPEN]),
        EventPattern('dbus-signal', signal='DBusNamesChanged',
            interface=cs.CHANNEL_TYPE_DBUS_TUBE, path=tube1_path))

    added, removed = dbus_names_e.args
    assert added.keys() == [contact2_handle_on_conn1]
    assert removed == []

    check_dbus_names(contact1_tube, [conn1_self_handle, contact2_handle_on_conn1])
    check_dbus_names(contact2_tube, [conn2_self_handle, contact1_handle_on_conn2])

    tube2_names = contact2_tube.Get(cs.CHANNEL_TYPE_DBUS_TUBE, 'DBusNames',
        dbus_interface=cs.PROPERTIES_IFACE)

    tube_conn1 = dbus.connection.Connection(tube_addr1)
    tube_conn2 = dbus.connection.Connection(tube_addr2)

    obj1 = Test(tube_conn1, q)

    # fire 'MySig' signal on the tube
    def my_sig_cb (arg, sender=None):
        assert tube2_names[contact1_handle_on_conn2] == sender

        q.append(Event('tube-dbus-signal', signal='MySig', args=[arg]))

    tube_conn2.add_signal_receiver(my_sig_cb, 'MySig', IFACE, path=PATH,
        sender_keyword='sender')

    obj1.MySig('hello')
    q.expect('tube-dbus-signal', signal='MySig', args=['hello'])

    # call remote method
    def my_method_cb(result):
        q.append(Event('tube-dbus-return', method='MyMethod', value=[result]))

    def my_method_error(e):
        assert False, e

    tube_conn2.get_object(tube2_names[contact1_handle_on_conn2], PATH).MyMethod(
        42, dbus_interface=IFACE,
        reply_handler=my_method_cb, error_handler=my_method_error)

    q.expect('tube-dbus-call', method='MyMethod', args=[42])
    q.expect('tube-dbus-return', method='MyMethod', value=[420])

    call_async(q, contact1_tube, 'Close')
    _, _, _, dbus_names_e = q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'),
        EventPattern('dbus-signal', signal='DBusNamesChanged',
            interface=cs.CHANNEL_TYPE_DBUS_TUBE, path=tube2_path))

    # Contact1 is removed from the tube
    added, removed = dbus_names_e.args
    assert added == {}
    assert removed == [contact1_handle_on_conn2]

    check_dbus_names(contact2_tube, [conn2_self_handle])

    call_async(q, contact2_tube, 'Close')
    q.expect_many(
        EventPattern('dbus-return', method='Close'),
        EventPattern('dbus-signal', signal='Closed'),
        EventPattern('dbus-signal', signal='ChannelClosed'))

    conn.Disconnect()
    conn2.Disconnect()

if __name__ == '__main__':
    # increase timer because Clique takes some time to join an existing muc
    exec_test(test, timeout=60)
