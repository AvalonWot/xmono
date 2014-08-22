# -*- coding: utf-8 -*-
#file: stack_trace.py
#author: skeu
#description: 堆栈回溯模块

from PyQt4 import QtCore, QtGui
import stack_traceUI

class StackTraceWindow(QtGui.QMainWindow):
    """stack_trace窗口主类"""
    deleteMethodSig = QtCore.pyqtSignal(str, bool, name = "deleteMethod")
    selectMethodSig = QtCore.pyqtSignal(str, name = "selectMethod")
    def __init__(self, parent=None):
        QtGui.QMainWindow.__init__(self, parent)
        self.ui = stack_traceUI.Ui_stackTraceForm()
        self.ui.setupUi(self)
        self._createRMenu()
        self._connectSolt()

        self.funcList = []
        self.stackList = []

    def _createRMenu(self):
        self.deleteAct = QtGui.QAction(u"取消堆栈回溯", self)
        self.deleteAllAct = QtGui.QAction(u"取消所有堆栈回溯", self)
        self.ui.stackTraceListWidget.addAction(self.deleteAct)
        self.ui.stackTraceListWidget.addAction(self.deleteAllAct)
        self.ui.stackTraceListWidget.setContextMenuPolicy(QtCore.Qt.ActionsContextMenu);
        self.deleteAct.triggered.connect(self._deleteMethod)
        self.deleteAllAct.triggered.connect(self._deleteAllMethod)

        self.clearResultAct = QtGui.QAction(u"清除结果", self)
        self.ui.stackTraceTreeWidget.addAction(self.clearResultAct)
        self.ui.stackTraceTreeWidget.setContextMenuPolicy(QtCore.Qt.ActionsContextMenu);
        self.clearResultAct.triggered.connect(self._clearTraceResult)

    def _connectSolt(self):
        self.ui.stackTraceListWidget.itemDoubleClicked.connect(self._doubleClicked)
        self.ui.stackTraceTreeWidget.itemDoubleClicked.connect(self._doubleClicked)

    def _doubleClicked(self, item):
        if type(item) is QtGui.QTreeWidgetItem:
            self.selectMethodSig.emit(str(item.text(0)))
        else:
            self.selectMethodSig.emit(str(item.text()))

    def _updateFuncListShow(self):
        self.ui.stackTraceListWidget.clear()
        self.ui.stackTraceListWidget.addItems(self.funcList)

    def _deleteMethod(self):
        item = self.ui.stackTraceListWidget.currentItem()
        if item == None:
            return
        s = str(item.text())
        self.deleteMethodSig.emit(s, False)
        self.funcList.remove(s)
        self._updateFuncListShow()

    def _deleteAllMethod(self):
        while len(self.funcList) > 0:
            s = self.funcList.pop()
            self.deleteMethodSig.emit(s, False)
        self.ui.stackTraceListWidget.clear()

    def _clearTraceResult(self):
        self.ui.stackTraceTreeWidget.clear()
        self.stackList = []

    def addTraceResult(self, l):
        """增加堆栈回溯的结果, 将被显示到tree widget中"""
        self.ui.stackTraceTreeWidget.setColumnCount(1)
        self.ui.stackTraceTreeWidget.setHeaderLabel("stack trace")
        self.stackList.append(l)
        root = QtGui.QTreeWidgetItem(None, QtCore.QStringList(l[0]))
        for i in l:
            print i
            root.addChild(QtGui.QTreeWidgetItem(None, QtCore.QStringList(i)))
        self.ui.stackTraceTreeWidget.addTopLevelItem(root)

    def addMethod(self, func):
        if func in self.funcList:
            return
        self.funcList.append(func)
        self._updateFuncListShow()
        self.show()

    def WinInit(self):
        self._deleteAllMethod()