# -*- coding: utf-8 -*-
#file: lua_hook.py
#author: skeu
#description: lua_hook子界面模块
from PyQt4 import QtCore, QtGui
import lua_hookUI

class LuaHookWindow(QtGui.QMainWindow):
    hookWithLuaSig = QtCore.pyqtSignal(str, int, str, name='hookWithLua')
    def __init__(self, parent=None):
        QtGui.QMainWindow.__init__(self, parent)
        self.ui = lua_hookUI.Ui_LuaHookWindow()
        self.ui.setupUi(self)
        self.ui.yesButton.clicked.connect(self._completeEdit)
        self.ui.noButton.clicked.connect(self._cancelEdit)

    def setMethodInfo (self, image_name, method_token):
        self.ui.inameLineEdit.setText(image_name)
        self.ui.tokenLineEdit.setText("{0:08X}".format(method_token))

    def _completeEdit(self):
        iname = self.ui.inameLineEdit.text()
        token = int(str(self.ui.tokenLineEdit.text()), 16)
        code = self.ui.luaCodeTextEdit.toPlainText()
        self.hookWithLuaSig.emit(iname, token, code)

    def _cancelEdit(self):
        self.ui.inameLineEdit.setText("")
        self.ui.tokenLineEdit.setText("")
        self.ui.luaCodeTextEdit.setText("")
        self.close()