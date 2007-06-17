#!/usr/bin/env python

from twisted.internet import reactor, protocol
from base64 import b64encode, b64decode
import random

class BaseMeshNode(protocol.ProcessProtocol):
  delimiter = '\n'
  __buffer = ''
  peers = []

  def __init__(self, name):
    self.name = name
    self.process = reactor.spawnProcess(self,
                    "./test-r-multicast-transport-io",
                    ("test-r-multicast-transport-io", name), 
                    None)

  def sendPacket(self, data):
    "Should be overridden"
    print "Should send: " + data

  def __sendPacket(self, data):
    return self.sendPacket(b64decode(data))


  def gotOutput(self, sender, data):
    "Should be overridden"
    print "Output: " + data

  def __gotOutput(self, data):
    (sender, rawdata) = data.split(":", 1)
    self.gotOutput(sender, b64decode(rawdata))

  def newNode(self, data):
    print "New node: " + data
    if not data in self.peers:
      self.peers.append(data)

  def recvPacket(self, data):
    self.process.write("RECV:" + b64encode(data) + "\n")

  def pushInput(self, data):
    self.process.write("INPUT:" + b64encode(data) + "\n")

  def lineReceived(self, line):
    commands = { "SEND"   :  self.__sendPacket,
                 "OUTPUT" :  self.__gotOutput,
                 "NEWNODE":  self.newNode }

    parts = line.rstrip().split(":", 1)
    if len(parts) == 2:
      command = parts[0]
      rawdata = parts[1]
      if command in commands:
        commands[command](rawdata)
        return

    print "Unknown output: " + line.rstrip()

  def outReceived(self, data):
    lines = (self.__buffer + data).split(self.delimiter)
    self.__buffer = lines.pop(-1)
    for line in lines:
      self.lineReceived(line)

  def errReceived(self, data):
    print "Error: " + data

  def processEnded(self, reason):
    print "process ended: " + str(reason)

class MeshNode(BaseMeshNode):
  def __init__(self, name, mesh):
    BaseMeshNode.__init__(self, name)
    self.mesh = mesh

  def sendPacket(self, data):
    self.mesh.sendPacket(self, data)

  def gotOutput(self, sender, data):
    self.mesh.gotOutput(self, sender, data)

class Link:
  def __init__(self, target, bandwidth, latency, dropchance):
    self.target = target
    self.bandwidth = bandwidth
    self.latency = latency
    self.dropchance = dropchance

  def send(self, data):
    if random.random() > self.dropchance:
      self.target.recvPacket(data)
    #else:
      #print "packet dropped"

class Mesh:
  nodes = []
  connections = {};

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

