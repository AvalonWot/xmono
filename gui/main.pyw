# -*- coding: utf-8 -*-
from PyQt4 import QtCore, QtGui
from xmono import *
import sys

if __name__ == '__main__':
    app = QtGui.QApplication(sys.argv)
    #app.setWindowIcon(QtGui.QIcon("./res/q108.png")); 
    window = XMonoWindow()
    window.show()
    app.exec_()
