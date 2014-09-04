# -*- coding: utf-8 -*-
#file: args_modify.py
#author: skeu
#description: 修改简单参数的傻瓜界面
from PyQt4 import QtCore, QtGui
import args_modifyUI

class ArgsModifyWindow(QtGui.QMainWindow):
    hookWithLuaSig = QtCore.pyqtSignal(str, int, str, name='hookWithLua')
    def __init__(self, parent=None):
        QtGui.QMainWindow.__init__(self, parent)
        self.ui = args_modifyUI.Ui_ArgsModifyWindow()
        self.ui.setupUi(self)