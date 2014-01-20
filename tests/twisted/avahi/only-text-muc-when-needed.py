"""
Test support for creating MUC text channels when necessary, not all
the time.

This test is a copy of gabble's. It's not quite as good as gabble's as
that one really tests that the XMPP MUC is left but that's harder. Oh
well.
"""

import dbus

from servicetest import call_async, EventPattern, assertEquals, \
    sync_dbus, wrap_channel
from saluttest import exec_test
import constants as cs
import ns

def request_stream_tube(q, bus, conn, method, jid):
    call_async(q, conn.Requests, method,
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
              cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
              cs.TARGET_ID: jid,
              cs.STREAM_TUBE_SERVICE: 'the.service',
              })

def stream_tube(q, bus, conn, method, jid):
    request_stream_tube(q, bus, conn, method, jid)
    e, _ = q.expect_many(EventPattern('dbus-return', method=method),
                         EventPattern('dbus-signal', signal='NewChannel'))

    # sigh
    if method == 'EnsureChannel':
        path = e.value[1]
    else:
        path = e.value[0]

    tube_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamTube')

    return (tube_chan,) + e.value

def request_text_channel(q, bus, conn, method, jid):
    call_async(q, conn.Requests, method,
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
              cs.TARGET_ID: jid,
              })

def text_channel(q, bus, conn, method, jid, presence=True):
    request_text_channel(q, bus, conn, method, jid)
    e, _ = q.expect_many(EventPattern('dbus-return', method=method),
                         EventPattern('dbus-signal', signal='NewChannel'))

    # sigh
    if method == 'EnsureChannel':
        path = e.value[1]
    else:
        path = e.value[0]

    text_chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Text')

    return (text_chan,) + e.value

def expect_close(q, path):
    q.expect_many(EventPattern('dbus-signal', signal='ChannelClosed',
                               args=[path]),
                  EventPattern('dbus-signal', signal='Closed',
                               path=path))

def connect(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
             args=[cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED])

def stream_tube_predicate(e):
    _, props = e.args
    return props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE

def assert_on_bus(q, chan):
    call_async(q, chan.Properties, 'GetAll', cs.CHANNEL)
    e = q.expect('dbus-return', method='GetAll')
    props = e.value[0]
    assert 'ChannelType' in props

def assert_not_on_bus(q, chan):
    call_async(q, chan.Properties, 'GetAll', cs.CHANNEL)
    q.expect('dbus-error', method='GetAll',
             name='org.freedesktop.DBus.Error.UnknownMethod')

# tests start here

def tube_no_text(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    # create a stream tube.
    # this will need a MUC channel to be opened, but we want to make
    # sure it doesn't get signalled.
    request_stream_tube(q, bus, conn, 'CreateChannel', jid)

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel',
                     predicate=stream_tube_predicate))

    forbidden = [EventPattern('dbus-signal', signal='NewChannel')]
    q.forbid_events(forbidden)

    tube_path, tube_props = ret.value
    assertEquals(cs.CHANNEL_TYPE_STREAM_TUBE, tube_props[cs.CHANNEL_TYPE])

    path, props = new_sig.args

    assertEquals(tube_path, path)
    assertEquals(tube_props, props)

    sync_dbus(bus, q, conn)

    q.unforbid_events(forbidden)

def tube_then_text(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    # first let's get a stream tube
    tube_chan, _, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)

    # now let's try and ensure the text channel which should happen
    # immediately
    request_text_channel(q, bus, conn, 'EnsureChannel', jid)

    ret = q.expect('dbus-return', method='EnsureChannel')

    yours, text_path, text_props = ret.value
    assertEquals(True, yours)
    assertEquals(cs.CHANNEL_TYPE_TEXT, text_props[cs.CHANNEL_TYPE])

    new_sig = q.expect('dbus-signal', signal='NewChannel')
    path, props = new_sig.args

    assertEquals(text_path, path)
    assertEquals(text_props, props)

def tube_remains_text_closes(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    text_chan, text_path, _ = text_channel(q, bus, conn, 'CreateChannel', jid)
    tube_chan, tube_path, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)

    # now let's try and close the text channel
    # this should happen sucessfully but the tube channel
    # should stick around
    forbidden = [EventPattern('dbus-signal', signal='ChannelClosed',
                              args=[tube_path])]
    q.forbid_events(forbidden)

    assert_on_bus(q, tube_chan)
    assert_on_bus(q, text_chan)

    text_chan.Close()
    expect_close(q, text_path)

    sync_dbus(bus, q, conn)

    assert_on_bus(q, tube_chan)
    assert_not_on_bus(q, text_chan)

    q.unforbid_events(forbidden)

def normally_close_text(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    text_chan, text_path, _ = text_channel(q, bus, conn, 'CreateChannel', jid)

    text_chan.Close()
    expect_close(q, text_path)

    assert_not_on_bus(q, text_chan)

def text_can_automatically_close(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    tube_chan, tube_path, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)

    sync_dbus(bus, q, conn)

    tube_chan.Close()
    expect_close(q, tube_path)

    assert_not_on_bus(q, tube_chan)

def text_remains_after_tube(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    tube_chan, tube_path, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)
    text_chan, text_path, _ = text_channel(q, bus, conn, 'CreateChannel', jid)

    sync_dbus(bus, q, conn)

    tube_chan.Close()
    expect_close(q, tube_path)

    assert_not_on_bus(q, tube_chan)
    assert_on_bus(q, text_chan)

    call_async(q, text_chan.Properties, 'GetAll', cs.CHANNEL_TYPE_TEXT)
    q.expect('dbus-return', method='GetAll')

    text_chan.Close()
    expect_close(q, text_path)

    assert_not_on_bus(q, tube_chan)
    assert_not_on_bus(q, text_chan)

def recreate_text(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    tube_chan, _, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)
    text_chan, text_path, text_props = text_channel(q, bus, conn,
                                                    'CreateChannel', jid)

    text_chan.Close()
    expect_close(q, text_path)

    assert_on_bus(q, tube_chan)
    assert_not_on_bus(q, text_chan)

    # now let's try and create the same text channel and hope we get
    # back the same channel

    request_text_channel(q, bus, conn, 'CreateChannel', jid)

    ret = q.expect('dbus-return', method='CreateChannel')

    path, props = ret.value
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])

    new_sig = q.expect('dbus-signal', signal='NewChannel')

    assertEquals(path, new_sig.args[0])
    assertEquals(props, new_sig.args[1])

    # the channel should be identical given it's the same MucChannel
    assertEquals(text_path, path)
    assertEquals(text_props, props)

    assert_on_bus(q, tube_chan)
    assert_on_bus(q, text_chan)

def test_channels(q, bus, conn):
    jid = 'test-muc'

    connect(q, bus, conn)

    tube_chan, _, _ = stream_tube(q, bus, conn, 'CreateChannel', jid)
    text_chan, text_path, _ = text_channel(q, bus, conn,'CreateChannel', jid)

    text_chan.Close()
    expect_close(q, text_path)

    # the following are basically the same as assert_[not_]on_bus()
    # but they look pretty.

    # methods on the text channel should fail
    call_async(q, text_chan.Properties, 'GetAll', cs.CHANNEL_TYPE_TEXT)
    q.expect('dbus-error', method='GetAll')

    # but methods on the tube should pass
    call_async(q, tube_chan.Properties, 'GetAll', cs.CHANNEL_TYPE_STREAM_TUBE)
    q.expect('dbus-return', method='GetAll')

if __name__ == '__main__':
    # request tube, assert no text appears
    exec_test(tube_no_text)

    # request tube, request text (no presence), assert both appear
    exec_test(tube_then_text)

    # request tube & text, close text, assert tube doesn't close
    exec_test(tube_remains_text_closes)

    # request text, close text, assert unavailable presence
    exec_test(normally_close_text)

    # request tube, close tube, assert unavailable presence
    exec_test(text_can_automatically_close)

    # request tube & text, close tube, assert text doesn't close
    exec_test(text_remains_after_tube)

    # request tube & text, close text, request text (no presence),
    # assert appears as normal
    exec_test(recreate_text)

    # request tube & text, close text, assert GetAll on text fails but
    # works on tube
    exec_test(test_channels)

