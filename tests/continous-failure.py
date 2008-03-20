#!/usr/bin/env python

# Both have a very small chance of occuring

from twisted.internet import reactor
from mesh import Mesh, MeshNode, packet_type, BYE, DATA
import sys

NUMNODES = 2
DELAY = 0.1

m = Mesh()

class TestMeshNode(MeshNode):
  def __init__(self, name, mesh):
    MeshNode.__init__(self, name, mesh)
    self.count = 0

  def node_connected (self):
    MeshNode.node_connected(self)
    reactor.callLater (0.5, self.push)

  def newNode (self, data):
    MeshNode.newNode (self, data)
    print data + " joined"

  def leftNode (self, data):
    MeshNode.leftNode (self, data)
    print data + " left"

  def push (self):
    reactor.callLater (2, self.push)
    self.pushInput (self.name + " " + str(self.count))
    self.count += 1

class FailMeshNode(TestMeshNode):
  def newNode (self, data):
    TestMeshNode.newNode (self, data)
    reactor.callLater (5, self.fail, "node")

n = TestMeshNode("node", m)
m.addMeshNode(n)

failnode = FailMeshNode("failnode", m)
m.addMeshNode(failnode)

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.00)

reactor.run()
