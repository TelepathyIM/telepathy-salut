#!/usr/bin/env python

from twisted.internet import reactor, protocol
from base64 import b64encode, b64decode

class BaseMeshNode(protocol.ProcessProtocol):
  delimiter = '\n'
  __buffer = ''

  def __init__(self, name):
    self.name = name
    self.process = reactor.spawnProcess(self,
                    "test-r-multicast-transport-io",
                    ("test-r-multicast-transport-io", name))

  def sendPacket(self, data):
    "Should be overridden"
    print "Should send: " + data

  def gotOutput(self, data):
    "Should be overridden"
    print "Output: " + data

  def recvPacket(self, data):
    self.process.write("RECV:" + b64encode(data) + "\n")

  def pushInput(self, data):
    self.process.write("INPUT:" + b64encode(data) + "\n")

  def lineReceived(self, line):
    commands = { "SEND"  :  self.sendPacket,
                 "OUTPUT":  self.gotOutput }

    parts = line.rstrip().split(":", 1)
    command = parts[0]
    rawdata = parts[1]
    commands[command](b64decode(rawdata))

  def outReceived(self, data):
    lines = (self.__buffer + data).split(self.delimiter)
    self.__buffer = lines.pop(-1)
    for line in lines:
      self.lineReceived(line)

  def errReceived(self, data):
    print "Error: " + data

  def processEnded(self, reason):
    print "process ended"

class MeshNode(BaseMeshNode):
  def __init__(self, name, mesh):
    BaseMeshNode.__init__(self, name)
    self.mesh = mesh

  def sendPacket(self, data):
    self.mesh.sendPacket(self, data)

  def gotOutput(self, data):
    self.mesh.gotOutput(self, data)

class Mesh:
  nodes = []

  def gotOutput(self, node, data):
    print "Got " + data + " from " + node.name

  def sendPacket(self, node, data):
    for x in self.nodes:
      if x != node:
        x.recvPacket(data)

  def addNode(self, name):
    node = MeshNode(name, self)
    self.nodes.append(node)
    return node

