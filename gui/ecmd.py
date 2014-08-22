# -*- coding: utf-8 -*-
#file: ecmd.py
#author: skeu
#description: 基于pyqt的 ecmd客户端接口

from PyQt4 import QtCore, QtNetwork
import struct

class EcmdPacket(object):
    def __init__(self, _id, _data):
        self.id = _id
        self.data = _data

class Ecmd(QtNetwork.QTcpSocket):
    def __init__(self, parent=None):
        QtNetwork.QTcpSocket.__init__(self, parent)
        self._dispatchTable = {}
        self._blockSize = 0
        self._errCallback = self._defaultErrHandle
        self._connectedCallback = self._defaultConnectedHandle
        self._slotConnect()

    def _slotConnect(self):
        self.connect(self, QtCore.SIGNAL("readyRead()"), self._recvLength)
        self.connect(self, QtCore.SIGNAL("error(QAbstractSocket::SocketError)"), self._socketErr)
        self.connect(self, QtCore.SIGNAL("connected()"), self._connected)

    def _recvLength(self):
        if self._blockSize == 0:
            if self.bytesAvailable() < 4:
                return
            self._blockSize = struct.unpack("<i", self.read(4))[0] - 4 #减去头部len的长度
        if self.bytesAvailable() < self._blockSize:
            return
        packet_size = self._blockSize
        self._blockSize = 0
        _packet = struct.unpack("<i{0}s".format(packet_size - 4), self.read(packet_size))
        packet = EcmdPacket(_packet[0], _packet[1])
        self._dispatch(packet)

    def _dispatch(self, packet):
        print "recv id {0}".format(packet.id)
        if packet.id in self._dispatchTable.keys():
            self._dispatchTable[packet.id](packet)

    def _defaultConnectedHandle(self):
        print "connected!"

    def _connected(self):
        self._connectedCallback()

    def _defaultErrHandle(self, err):
        print "socket err!"

    def _socketErr(self, err):
        self.abort()
        self._errCallback(err)

    def registerErrCallback(self, _func):
        self._errCallback = _func

    def registerConnectedCallback(self, _func):
        self._connectedCallback = _func

    def registerResp(self, _id, _func):
        self._dispatchTable[_id] = _func

    def sendPacket(self, packet):
        data = struct.pack("<ii{0}s".format(len(packet.data)), len(packet.data)+8, packet.id, packet.data)
        self.write(data)
