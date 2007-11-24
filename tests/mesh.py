#!/usr/bin/env python

from twisted.internet import reactor, protocol
from base64 import b64encode, b64decode
from struct import unpack
import random

packettypes = {    0: "Whois request",
                   1: "Whois reply",
                   2: "Repair request",
                   3: "Session",
                 0xf: "Data",
                0x10: "No data",
                0x11: "Failure",
                0x12: "Attempt join",
                0x13: "Join",
                0x14: "Bye"
}

WHOIS_REQUEST   = 0x0
WHOIS_REPLY     = 0x1
REPAIR_REQUEST  = 0x2
SESSION         = 0x3
DATA            = 0xf
NO_DATA         = 0x10
FAILURE         = 0x11
ATTEMPT_JOIN    = 0x12
JOIN            = 0x13
BYE             = 0x14

def packet_sender(data):
  return unpack ("!I", data[8:12])[0]

def packet_type(data):
  return unpack("B", data[7])[0]

def dump_depends(data):
  sender = packet_sender(data)
  num_senders = unpack("B", data[16])[0]
  print "sender: %x (%d)" % (sender, num_senders)
  for x in xrange (0, num_senders):
    (sender, depend) = unpack("!II", data[17 + x:17 + x + 8])
    print "%x:\t%x" % (sender, depend)


class BaseMeshNode(protocol.ProcessProtocol):
  delimiter = '\n'
  __buffer = ''

  def __init__(self, name):
    self.name = name
    self.process = reactor.spawnProcess(self,
                    "./test-r-multicast-transport-io",
                    ("test-r-multicast-transport-io", name), 
                    None)
    self.peers = []
    self.packets = {}

  def sendPacket(self, data):
    "Should be overridden"
    print "Should send: " + data

  def __sendPacket(self, data):
    binary = b64decode(data)
    type = packet_type(binary)
    self.packets[type] = self.packets.get(type, 0) + 1
    return self.sendPacket(binary)

  def stats(self):
    print "-------" + self.name + "-------"
    for (a,b) in self.packets.iteritems():
      print packettypes[a] + ":\t" + str(b)

  def gotOutput(self, sender, data):
    "Should be overridden"
    print "Output: " + data

  def node_connected(self):
    "Should be overridden"
    print "Connected!!"

  def node_disconnected(self):
    "Should be overridden"
    print "Disconnected!!"

  def __connected(self, data):
    self.node_connected()

  def __disconnected(self, data):
    self.node_disconnected()

  def __gotOutput(self, data):
    (sender, rawdata) = data.split(":", 1)
    self.gotOutput(sender, b64decode(rawdata))


  def newNode(self, data):
    if not data in self.peers:
      self.peers.append(data)

  def __newNodes(self, data):
    for x in data.split():
      self.newNode(x)

  def leftNode(self, node):
    self.peers.remove(node)

  def __leftNodes(self, data):
    for x in data.split():
      self.leftNode (x)

  def recvPacket(self, data):
    self.process.write("RECV:" + b64encode(data) + "\n")

  def pushInput(self, data):
    self.process.write("INPUT:" + b64encode(data) + "\n")

  def fail(self, name):
    self.process.write("FAIL:" + name + "\n")

  def disconnect(self):
    self.process.write("DISCONNECT\n")

  def lineReceived(self, line):
    commands = { "SEND"         :  self.__sendPacket,
                 "OUTPUT"       :  self.__gotOutput,
                 "CONNECTED"    :  self.__connected,
                 "DISCONNECTED" :  self.__disconnected,
                 "NEWNODES"      :  self.__newNodes,
                 "LOSTNODES"      :  self.__leftNodes
                 }

    parts = line.rstrip().split(":", 1)
    if len(parts) == 2:
      command = parts[0]
      rawdata = parts[1]
      if command in commands:
        commands[command](rawdata)
        return
    self.unknownOutput(line)

  def unknownOutput(self, line):
    print self.name + " (U) " + line.rstrip()

  def outReceived(self, data):
    lines = (self.__buffer + data).split(self.delimiter)
    self.__buffer = lines.pop(-1)
    for line in lines:
      self.lineReceived(line)

  def errReceived(self, data):
    print self.name + " Error: " + data.rstrip()

  def processEnded(self, reason):
    if self.process.status != 0:
      print "process ended: " + str(reason)

class MeshNode(BaseMeshNode):
  def __init__(self, name, mesh):
    BaseMeshNode.__init__(self, name)
    self.mesh = mesh

  def sendPacket(self, data):
    self.mesh.sendPacket(self, data)

  def gotOutput(self, sender, data):
    self.mesh.gotOutput(self, sender, data)

  def node_connected(self):
    self.mesh.connected(self)

class Link:
  def __init__(self, target, bandwidth, latency, dropchance):
    self.target = target
    self.bandwidth = bandwidth
    self.latency = latency
    self.dropchance = dropchance

  def send(self, data):
    if random.random() > self.dropchance:
      self.target.recvPacket(data)

class Mesh:
  def __init__ (self):
    self.nodes = []
    self.connections = {}

  def connect(self, node):
    print node.name + " got connected"

  def gotOutput(self, node, sender, data):
    print "Got " + data + " from " + node.name + " send by " + sender

  def connect(self, node0, node1, bandwidth, latency, dropchance):
    self.connections.setdefault(node0, []).append(
       Link(node1, bandwidth, latency, dropchance))

  def connect_duplex(self, node0, node1, bandwidth, latency, dropchance):
    self.connect(node0, node1, bandwidth, latency, dropchance)
    self.connect(node1, node0, bandwidth, latency, dropchance)

  def connect_full(self, bandwidth, latency, dropchance):
    self.connections = {}
    for x in self.nodes:
      for y in self.nodes:
        if x != y:
          self.connect(x, y, bandwidth, latency, dropchance)

  def sendPacket(self, node, data):
    conn = self.connections.get(node, [])
    for link in conn:
      link.send(data)

  def addNode(self, name):
    node = MeshNode(name, self)
    self.nodes.append(node)
    return node

  def addMeshNode(self, node):
    self.nodes.append(node)
    return node

  def connected (self, node):
    "To be overwritten"
    pass

