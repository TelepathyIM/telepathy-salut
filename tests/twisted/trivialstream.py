import dbus.glib
import gobject
import sys
import time
import os
import socket
import tempfile
import random
import string

class TrivialStream:
    def __init__(self, socket_address=None):
        self.socket_address = socket_address

    def read_socket(self, s):
        try:
            data = s.recv(1024)
            if len(data) > 0:
                print "received:", data
        except socket.error, e:
            pass
        return True

    def write_socket(self, s, msg):
        print "send:", msg
        try:
            s = s.send(msg)
        except socket.error, e:
            pass
        return True

class TrivialStreamServer(TrivialStream):
    def __init__(self):
        TrivialStream.__init__(self)

    def run(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setblocking(1)
        s.settimeout(0.1)
        s.bind(("127.0.0.1", 0))

        self.socket_address = s.getsockname()
        print "Trivial Server lauched on socket", self.socket_address
        s.listen(1)

        gobject.timeout_add(1000, self.accept_client, s)

    def accept_client(self, s):
        try:
            s2, addr = s.accept()
            s2.setblocking(1)
            s2.setblocking(0.1)
            self.handle_client(s2)
            return True
        except socket.timeout:
            return True

    def handle_client(self, s):
        gobject.timeout_add(5000, self.write_socket, s, "hi !")

class TrivialStreamClient(TrivialStream):
    def __init__(self, socket_address):
        TrivialStream.__init__(self, socket_address)

    def connect(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(self.socket_address)
        print "Trivial client connected to", self.socket_address
        gobject.timeout_add(1000, self.read_socket, s)

