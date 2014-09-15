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
        self.ui.yesButton.clicked.connect(self._emitHook)
        self.ui.noButton.clicked.connect(self.close)

    def setMethodInfo(self, iname, token, params):
        """iname : image_name(str), token : method_token(int)
            params : method signature(str)"""
        self.image_name = iname
        self.token = token
        #处理函数签名, 生成参数表
        self.params = self._makeParamsTable(params)

    def _makeParamsTable(self, params):
        """parse params sig string,
            return : list of (name, type, canEdit)"""
        if not params:
            return None
        res = []
        types = ['int', 'uint', 'double', 'single', 'bool', 'byte']
        l = params.split(',')
        for p in l:
            p = p.strip()
            if p in types:
                v = (p, True)
            else:
                v = (p, False)
            #参数名称
            res.append(v)
        return res

    def _clearArgsTableWidget(self):
        w = self.ui.argsTableWidget
        w.clear()
        while w.rowCount() != 0:
            w.removeRow(0)
        w.horizontalHeader().hide()

    def _setArgsTableWidget(self, param_table):
        #构造参数表窗体的格式
        w = self.ui.argsTableWidget
        w.setColumnCount(2)
        labels = QtCore.QStringList()
        labels.append("type")
        labels.append("value")
        w.setHorizontalHeaderLabels(labels)
        w.verticalHeader().setDefaultSectionSize(23)
        w.verticalHeader().setResizeMode(QtGui.QHeaderView.Fixed)
        w.horizontalHeader().show()
        w.verticalHeader().hide()
        #填充参数内容到参数窗体
        for p in param_table:
            row = w.rowCount()
            w.insertRow(row)
            falgs = QtCore.Qt.ItemIsSelectable | QtCore.Qt.ItemIsEditable | QtCore.Qt.ItemIsEnabled
            #设置参数类型
            v = QtGui.QTableWidgetItem(p[0])
            v.setFlags(falgs)
            w.setItem(row, 0, v)
            #参数是否可以编辑
            v = QtGui.QTableWidgetItem()
            v.setFlags(falgs)
            if not p[1]:
                v.setFlags(QtCore.Qt.NoItemFlags)
                #设置背景色为灰色
                brush = QtGui.QBrush(QtGui.QColor(QtCore.Qt.gray))
                v.setBackground(brush)
            w.setItem(row, 1, v)

    def show(self):
        #清空上一次的显示内容
        self._clearArgsTableWidget()
        #如果有参数表, 那么设置参数窗体
        if self.params:
            self._setArgsTableWidget(self.params)
        #调用父类显示窗体
        QtGui.QMainWindow.show(self)

    def _emitHook(self):
        lua_code = ""
        err = ""
        for i,p in enumerate(self.params):
            if not p[1]:
                continue
            #获取用户填值的item
            item = self.ui.argsTableWidget.item(i, 1)
            if item.data(QtCore.Qt.EditRole).isNull():
                continue
            if p[0] == 'single':
                t = item.data(QtCore.Qt.EditRole).toFloat()
                if t[1] == False:
                    err = u'错误 : arg {0} 必须是float!'.format(i + 1)
                    break
                v = t[0]
            elif p[0] == 'double':
                t = item.data(QtCore.Qt.EditRole).toDouble()
                if t[1] == False:
                    err = u'错误 : arg {0} 必须是Double!'.format(i + 1)
                    break
                v = t[0]
            elif p[0] == 'byte':
                t = item.data(QtCore.Qt.EditRole).toInt()
                if t[1] == False:
                    err = u'错误 : arg {0} 必须是Byte!'.format(i + 1)
                    break
                v = t[0] & 0xFF
            elif p[0] == 'bool':
                t = item.data(QtCore.Qt.EditRole).toBool()
                v = int(t)
            else:
                t = item.data(QtCore.Qt.EditRole).toInt()
                if t[1] == False:
                    err = u'错误 : arg {0} 必须是整数!'.format(i + 1)
                    break
                v = t[0]
            lua_code = lua_code + "xmono.set_args({0},{1})\n".format(i+1, v)
        #如果什么都不需要修改, 直接返回
        if not lua_code:
            self.close()
            return
        msg_box = QtGui.QMessageBox()
        if err:
            msg_box.setText(err)
            msg_box.exec_()
            return
        msg_box.setText(lua_code)
        msg_box.exec_()
        self.hookWithLuaSig.emit(self.image_name, self.token, lua_code)
        self.close()