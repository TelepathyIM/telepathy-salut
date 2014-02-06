from saluttest import make_connection, wait_for_contact_list
from avahitest import get_host_name
from servicetest import make_channel_proxy

import constants as cs

def connect_two_accounts(q, bus, conn):
    # first connection: connect
    contact1_name = "testsuite" + "@" + get_host_name()
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with other channels announces.
    wait_for_contact_list(q, conn)

    # second connection: connect
    conn2_params = {
        'published-name': 'testsuite2',
        'first-name': 'test2',
        'last-name': 'suite2',
        }
    contact2_name = "testsuite2" + "@" + get_host_name()
    conn2 = make_connection(bus, lambda x: None, conn2_params)
    conn2.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED])

    wait_for_contact_list(q, conn2)

    # first connection: get the contact list
    publish_handle = conn.RequestHandles(cs.HT_LIST, ["publish"])[0]
    conn1_publish = conn.RequestChannel(cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.HT_LIST, publish_handle, False)
    conn1_publish_proxy = bus.get_object(conn.bus_name, conn1_publish)

    # second connection: get the contact list
    publish_handle = conn2.RequestHandles(cs.HT_LIST, ["publish"])[0]
    conn2_publish = conn2.RequestChannel(cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.HT_LIST, publish_handle, False)
    conn2_publish_proxy = bus.get_object(conn2.bus_name, conn2_publish)

    # first connection: wait to see contact2
    # The signal MembersChanged may be already emitted... check the Members
    # property first
    contact2_handle_on_conn1 = 0
    conn1_members = conn1_publish_proxy.Get(cs.CHANNEL_IFACE_GROUP, 'Members',
            dbus_interface=cs.PROPERTIES_IFACE)
    for h in conn1_members:
        name = conn.InspectHandles(cs.HT_CONTACT, [h])[0]
        if name == contact2_name:
            contact2_handle_on_conn1 = h
    while contact2_handle_on_conn1 == 0:
        e = q.expect('dbus-signal', signal='MembersChanged',
            path=conn1_publish)
        for h in e.args[1]:
            name = conn.InspectHandles(cs.HT_CONTACT, [h])[0]
            if name == contact2_name:
                contact2_handle_on_conn1 = h

    # second connection: wait to see contact1
    # The signal MembersChanged may be already emitted... check the Members
    # property first
    contact1_handle_on_conn2 = 0
    conn2_members = conn2_publish_proxy.Get(
            'org.freedesktop.Telepathy.Channel.Interface.Group', 'Members',
            dbus_interface='org.freedesktop.DBus.Properties')
    for h in conn2_members:
        name = conn2.InspectHandles(cs.HT_CONTACT, [h])[0]
        if name == contact1_name:
            contact1_handle_on_conn2 = h
    while contact1_handle_on_conn2 == 0:
        e = q.expect('dbus-signal', signal='MembersChanged',
            path=conn2_publish)
        for h in e.args[1]:
            name = conn2.InspectHandles(cs.HT_CONTACT, [h])[0]
            if name == contact1_name:
                contact1_handle_on_conn2 = h

    return contact1_name, conn2, contact2_name, contact2_handle_on_conn1, contact1_handle_on_conn2

def join_muc(q, conn, muc_name):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    muc_handle = conn.RequestHandles(cs.HT_ROOM, [muc_name])[0]
    path = conn.RequestChannel(cs.CHANNEL_TYPE_TEXT, cs.HT_ROOM, muc_handle, True)
    # added as remote pending
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [], [], [], [self_handle], self_handle, 0])
    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [self_handle], [], [], [], self_handle, 0])
    group = make_channel_proxy(conn, path, "Channel.Interface.Group")

    return muc_handle, group

def invite_to_muc(q, group1, conn2, invited_handle, inviter_handle):
    # first connection: invite contact
    group1.AddMembers([invited_handle], "Let's tube!")

    # channel is created on conn2
    e = q.expect('dbus-signal', signal='NewChannel', path=conn2.object_path)
    path = e.args[0]
    group2 = make_channel_proxy(conn2, path, "Channel.Interface.Group")

    # we are invited to the muc
    # added as local pending
    conn2_self_handle = conn2.Properties.Get(cs.CONN, "SelfHandle")
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=["Let's tube!", [], [], [conn2_self_handle], [],
            inviter_handle, 4])

    # second connection: accept the invite
    group2.AddMembers([conn2_self_handle], "")

    # added as remote pending
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [], [], [], [conn2_self_handle], conn2_self_handle, 0])

    # added as member
    q.expect('dbus-signal', signal='MembersChanged', path=path,
        args=['', [conn2_self_handle], [], [], [], conn2_self_handle, 0])

    return group2
