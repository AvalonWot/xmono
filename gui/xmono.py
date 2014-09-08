# -*- coding: utf-8 -*-
#file: ecmd.py
#author: skeu
#description: xmono客户端主程序

from PyQt4 import QtCore, QtGui
import xmonoUI
import cil, stack_trace, lua_hook, args_modify
import ecmd
import zlib
import re
from xres import *
from const_cmdid import *
import xmono_pb2

#log辅助类
class Log(object):
    """log辅助类"""
    def regHandle(self, end):
        """end为输出到后端的接口函数,其参数为:
            msg   : str log内容
            level : int log的等级"""
        self._handle = end

    def _log(self, msg, level):
        """level WARNING INFO DEBUG ERROR"""
        self._handle(msg, level)

    def w(self, msg):
        """log中的warring接口
            msg: str, 需要打印到log的字符串"""
        self._log(msg, 'WARNING')

    def i(self, msg):
        """log中的info接口
            msg: str, 需要打印到log的字符串"""
        self._log(msg, 'INFO')

    def d(self, msg):
        """log中的debug接口
            msg: str, 需要打印到log的字符串"""
        self._log(msg, 'DEBUG')

    def e(self, msg):
        """log中的error接口
            msg: str, 需要打印到log的字符串"""
        self._log(msg, 'ERROR')

class XMonoWindow(QtGui.QMainWindow):
    """xmono主类"""

    #声明信号槽
    cilDisasmed = QtCore.pyqtSignal(str)

    def __init__(self, parent=None):
        QtGui.QMainWindow.__init__(self, parent)
        self.ui = xmonoUI.Ui_MainWindow()
        self.ui.setupUi(self)
        self.cilWindow = cil.CilWindow(self)
        self.stackTraceWindow = stack_trace.StackTraceWindow(self)
        self.luaHookWindow = lua_hook.LuaHookWindow(self)
        self.argsModifyWindow = args_modify.ArgsModifyWindow(self)
        self.log = Log()
        self.log.regHandle(self._print2Log)
        self._ecmd = ecmd.Ecmd(self)
        self._createActions()
        self._createMenus()
        self._createToolBar()
        self._slotConnects()
        self._WinInit()
        self._ecmdInit()
        self._initVariate()

    def _initVariate(self):
        """初始化一些要用到功能性全局变量, 放到这里是为了让__init__看起来舒服点"""
        self._traceFuncCntDict = {}

    def _createActions(self):
        icon = QtGui.QIcon(":/icons/res/icons/connect.png")
        self.connectAct = QtGui.QAction(icon, u"连接", self)

        icon = QtGui.QIcon(":/icons/res/icons/play.png")
        self.startTraceAct = QtGui.QAction(icon, u"开始记录", self)

        icon = QtGui.QIcon(":/icons/res/icons/stop.png")
        self.stopTraceAct = QtGui.QAction(icon, u"停止记录", self)

        icon = QtGui.QIcon(":/icons/res/icons/clear.png")
        self.clearTraceAct = QtGui.QAction(icon, u"清除记录", self)

        icon = QtGui.QIcon(":/icons/res/icons/trace_win.png")
        self.showStackWinAct =  QtGui.QAction(icon, u"显示堆栈回溯窗口", self)

        self.stackTraceAct = QtGui.QAction(u"堆栈跟踪", self)
        self.luaHookAct = QtGui.QAction(u"函数Hook", self)
        self.argsModifyAct = QtGui.QAction(u"函数简单实参修改", self)

    def _createMenus(self):
        menuBar = self.menuBar()

        connectMenu = menuBar.addMenu(u"连接")
        connectMenu.addAction(self.connectAct)

        traceMenu = menuBar.addMenu(u"记录")
        traceMenu.addAction(self.startTraceAct)
        traceMenu.addAction(self.stopTraceAct)
        traceMenu.addAction(self.clearTraceAct)

        subwinMenu = menuBar.addMenu(u"窗口")
        subwinMenu.addAction(self.showStackWinAct)

        #这里创建func list窗口的右键菜单, 如果代码增长, 考虑移出到单独函数
        self.flRMenu = QtGui.QMenu(self.ui.funcCntTableWidget)
        self.flRMenu.addAction(self.stackTraceAct)
        self.flRMenu.addAction(self.luaHookAct)
        self.flRMenu.addAction(self.argsModifyAct)
        self.ui.funcCntTableWidget.setContextMenuPolicy(QtCore.Qt.CustomContextMenu)

    def _createToolBar(self):
        connectToolBar = self.addToolBar(u"Connect")
        connectToolBar.addAction(self.connectAct)

        traceToolBar = self.addToolBar(u"Trace")
        traceToolBar.addAction(self.startTraceAct)
        traceToolBar.addAction(self.stopTraceAct)
        traceToolBar.addAction(self.clearTraceAct)

        subwinToolBar = self.addToolBar(u"Windows")
        subwinToolBar.addAction(self.showStackWinAct)

    def _WinInit(self):
        """初始化界面, 当连接断开时会被调用"""
        self.connectAct.setEnabled(True)
        self.startTraceAct.setEnabled(False)
        self.stopTraceAct.setEnabled(False)
        self.clearTraceAct.setEnabled(False)
        self.cilWindow.WinInit()
        self.stackTraceWindow.WinInit()

    def _slotConnects(self):
        _actTriggered = lambda act,slot:self.connect(act, QtCore.SIGNAL("triggered()"), slot)
        _actTriggered(self.connectAct, self._connect)
        _actTriggered(self.startTraceAct, self._startTrace)
        _actTriggered(self.stopTraceAct, self._stopTrace)
        _actTriggered(self.clearTraceAct, self._clearTrace)
        _actTriggered(self.stackTraceAct, self._stackTraceMethod)
        _actTriggered(self.showStackWinAct, self.stackTraceWindow.show)
        _actTriggered(self.luaHookAct, self._showLuaHookWin)
        _actTriggered(self.argsModifyAct, self._showArgsModifyWin)

        self.ui.filterLineEdit.returnPressed.connect(self._filterOutRequest)
        self.ui.tabWidget.currentChanged.connect(self._traceCntShow)
        self.ui.funcCntTableWidget.itemDoubleClicked.connect(self._funcDoubleClicked)
        self.cilDisasmed.connect(self.cilWindow.showCil)
        self.ui.cmdLineEdit.returnPressed.connect(self._cmdHandle)
        self.ui.funcCntTableWidget.customContextMenuRequested.connect(self._showFuncCntRMenu)
        self.stackTraceWindow.deleteMethod.connect(self._disTraceMethod)
        self.stackTraceWindow.selectMethod.connect(self._disasmMethod)
        self.cilWindow.compiled.connect(self._replaceMethod)
        self.luaHookWindow.hookWithLua.connect(self._luaHook)
        self.argsModifyWindow.hookWithLua.connect(self._luaHook)
        self.ui.cntPushButton.clicked.connect(self._filterOutRequest)
        self.cilWindow.findMethod.connect(self._disasmMethod)

    def _showFuncCntRMenu(self, pos):
        p = self.ui.funcCntTableWidget.mapToGlobal(pos)
        self.flRMenu.exec_(p)

    def _ecmdInit(self):
        self._ecmd.registerConnectedCallback(self._ecmdConnected)
        self._ecmd.registerErrCallback(self._ecmdErr)
        self._ecmd.registerResp(XMONO_ID_TRACE_REPORT, self._recvFuncTrace)
        self._ecmd.registerResp(XMONO_ID_DISASM_METHOD_RSP, self._recvDisasmMethod)
        self._ecmd.registerResp(XMONO_ID_LIST_DOMAIN_RSP, self._recvListDomain)
        self._ecmd.registerResp(XMONO_ID_STACK_TRACE_RSP, self._recvStackTrace)
        self._ecmd.registerResp(XMONO_ID_REPLACE_METHOD_RSP, self._recvReplaceMethodRsp)
        self._ecmd.registerResp(XMONO_ID_LUA_EXEC_RSP, self._recvExecLua)
        self._ecmd.registerResp(XMONO_ID_LUA_HOOK_RSP, self._recvLuaHook)

    def _ecmdConnected(self):
        self.log.i(u"连接建立")
        self.startTraceAct.setEnabled(True)

    def _ecmdErr(self, err):
        self.log.e(u"连接错误 : {0}".format(err))
        self._WinInit()

    def _connect(self):
        self.log.i(u"开始连接")
        self._ecmd.connectToHost("127.0.0.1", 21187)
        self.connectAct.setEnabled(False)

    def _cmdHandle(self):
        t = self.ui.cmdLineEdit.text()
        self.ui.cmdText.append(t)
        self.ui.cmdText.append(">>{0}".format(eval(str(t))))
        self.ui.cmdLineEdit.clear()

    def _startTrace(self):
        self._clearFuncCntTableWidget()
        self.ui.funcGroupBox.setTitle (u"正在跟踪...")
        self.startTraceAct.setEnabled(False)
        self.stopTraceAct.setEnabled(True)
        self.clearTraceAct.setEnabled(False)
        p = ecmd.EcmdPacket(XMONO_ID_FUNC_TRACE_START, "")
        self._ecmd.sendPacket(p)
        self.log.d(u"send packet id = {0}".format(p.id));

    def _stopTrace(self):
        self.startTraceAct.setEnabled(True)
        self.stopTraceAct.setEnabled(False)
        self.clearTraceAct.setEnabled(True)
        p = ecmd.EcmdPacket(XMONO_ID_FUNC_TRACE_STOP, "")
        self._ecmd.sendPacket(p)
        self.log.d(u"send packet id = {0}".format(p.id));
        self.ui.funcGroupBox.setTitle (u"暂无跟踪结果...")

    def _clearTrace(self):
        self.clearTraceAct.setEnabled(False)
        self._traceFuncCntDict.clear()
        self._clearFuncCntTableWidget()
        self.ui.funcGroupBox.setTitle (u"函数 : 0")

    def _traceLogFilterOut(self, data):
        flr = str(self.ui.filterLineEdit.text())
        if flr == "":
            return data
        pattern = re.compile(flr, re.I)
        return [i for i in data if re.search(pattern, i) != None]

    def _filterOutRequest(self):
        self._funcTraceCntFilteShow()

    def _funcTraceCntFilteShow(self):
        d = {}
        f_k = self._traceLogFilterOut(self._traceFuncCntDict.keys())
        mint = self.ui.minSpinBox.value()
        maxt = self.ui.maxSpinBox.value()
        if maxt < mint:
            maxt = mint
        for k in f_k:
            #过滤次数
            t = self._traceFuncCntDict[k]
            if t[0] >= mint and t[0] <= maxt:
                d[k] = t
        self._traceCntShow(d)

    def _tabChange(self, index):
        if index == 0: #cnt
            self._funcTraceCntFilteShow()

    def _clearFuncCntTableWidget(self):
        w = self.ui.funcCntTableWidget
        w.clear()
        while w.rowCount() != 0:
            w.removeRow(0)
        w.horizontalHeader().hide()

    def _traceCntShow(self, d):
        self._clearFuncCntTableWidget()
        w = self.ui.funcCntTableWidget
        w.setColumnCount(3)
        labels = QtCore.QStringList()
        labels.append("sn")
        labels.append("function")
        labels.append("count")
        w.setHorizontalHeaderLabels(labels)
        w.horizontalHeader().setDefaultSectionSize(50)
        w.horizontalHeader().setResizeMode(0, QtGui.QHeaderView.Fixed)
        w.horizontalHeader().setResizeMode(1, QtGui.QHeaderView.Stretch)
        w.horizontalHeader().setResizeMode(2, QtGui.QHeaderView.ResizeToContents)
        w.verticalHeader().setDefaultSectionSize(23)
        w.verticalHeader().setResizeMode(QtGui.QHeaderView.Fixed)
        w.horizontalHeader().show()
        w.verticalHeader().hide()
        for k in d.keys():
            row = w.rowCount()
            w.insertRow(row)
            #设置序数
            v = QtGui.QTableWidgetItem()
            v.setData(0, d[k][1])
            w.setItem(row, 0, v)
            #设置函数名
            w.setItem(row, 1, QtGui.QTableWidgetItem(k))
            #设置调用次数
            v = QtGui.QTableWidgetItem()
            v.setData(0, d[k][0])  #这里用setData是为了方便排序
            w.setItem(row, 2, v)
        w.sortItems(0, QtCore.Qt.AscendingOrder)

    def _traceCntHandle(self, l):
        first = len(self._traceFuncCntDict) == 0
        d = {}
        for i in l:
            item = i.split('|')
            if first:
                d[item[0]] = (int(item[1]), int(item[2]))
                continue
            if item[0] in self._traceFuncCntDict.keys():
                d[item[0]] = (int(item[1]), int(item[2]))
        self._traceFuncCntDict.clear()
        self._traceFuncCntDict.update(d)
        self._funcTraceCntFilteShow()

    def _recvFuncTrace(self, packet):
        data = zlib.decompress(packet.data)
        self.log.d(u"recv func trace data : {0}".format(len(data)))
        l = data.split("\n")[:-1]
        self._traceCntHandle(l)
        self.ui.funcGroupBox.setTitle (u"函数 : {0}".format(len(self._traceFuncCntDict)))

    def _funcDoubleClicked(self, item):
        if item.column() != 0:
            item = self.ui.funcCntTableWidget.item(item.row(), 1)
        s = item.text()
        name, sig, token = self._matchMethodSig(str(s))
        self._disasmMethod(name, int(token, 16))

    def _disasmMethod(self, iname, token):
        req = xmono_pb2.DisasmMethodReq()
        req.method_token = token
        req.image_name = str(iname)
        pkg = ecmd.EcmdPacket(XMONO_ID_DISASM_METHOD_REP, req.SerializeToString())
        self._ecmd.sendPacket(pkg)

    def _replaceMethod(self, iname, token, t):
        code = t[0]
        ex = t[1]
        req = xmono_pb2.ReplaceMethodReq()
        req.domain_id = 0
        req.image_name = str(iname)
        req.method_token = token
        req.new_code = code
        if (len(ex.keys()) > 0):
            keys = ex.keys()
            keys.sort()
            for k in keys:
                e = req.ex.add()
                e.index = k
                e.try_offset = ex[k].try_offset
                e.try_len = ex[k].try_len
                e.handler_offset = ex[k].handler_offset
                e.handler_len = ex[k].handler_len
        pkg = ecmd.EcmdPacket(XMONO_ID_REPLACE_METHOD_REP, req.SerializeToString())
        self._ecmd.sendPacket(pkg)
        self.log.d(u"_replaceMethod over")

    def _listDomain(self):
        pkg = ecmd.EcmdPacket(XMONO_ID_LIST_DOMAIN_REP, "")
        self._ecmd.sendPacket(pkg)

    def _getFuncCntItemStr(self):
        item = self.ui.funcCntTableWidget.currentItem()
        if item == None:
            return
        if item.column() != 0:
            item = self.ui.funcCntTableWidget.item(item.row(), 1)
        return str(item.text())

    def _disTraceMethod(self, s):
        self._traceMethod(s, False)

    def _matchMethodSig(self, s):
        s = str(s)
        p = "\[(.+?)\](.*)\[([0-9A-Fa-f]{8})\]"
        r = re.search(p, s)
        if r == None:
            self.log.e(u"无法从{0}中提取需要的信息".format(s))
            return
        g = r.groups()
        return g[0], g[1], g[2]

    def _traceMethod(self, s, sw):
        name, sig, token = self._matchMethodSig(s)
        req = xmono_pb2.StackTraceReq()
        req.image_name = name
        req.method_token = int(token, 16)
        if sw:
            pck_id = XMONO_ID_STACK_TRACE_REP
        else:
            pck_id = XMONO_ID_UNSTACK_TRACE_REP
        pkg = ecmd.EcmdPacket(pck_id, req.SerializeToString())
        self._ecmd.sendPacket(pkg)

    def _stackTraceMethod(self):
        s = self._getFuncCntItemStr()
        self._traceMethod(s, True)
        self.stackTraceWindow.addMethod(s)

    def _execLuaCode(self, code):
        req = xmono_pb2.LuaExecReq()
        req.lua_code = code
        pkg = ecmd.EcmdPacket(XMONO_ID_LUA_EXEC_REP, req.SerializeToString())
        self._ecmd.sendPacket(pkg)

    def _luaHook(self, image_name, method_token, code):
        image_name = str(image_name)
        code = str(code)
        req = xmono_pb2.LuaHookReq()
        req.disable = False
        req.image_name = image_name
        req.method_token = method_token
        req.lua_code = code
        pkg = ecmd.EcmdPacket(XMONO_ID_LUA_HOOK_REP, req.SerializeToString())
        self._ecmd.sendPacket(pkg)

    def _showLuaHookWin(self):
        s = self._getFuncCntItemStr()
        if s == None:
            return
        name, sig, token = self._matchMethodSig(s)
        self.luaHookWindow.setMethodInfo(name, int(token, 16))
        self.luaHookWindow.show()

    def _showArgsModifyWin(self):
        pass

    def _recvDisasmMethod(self, packet):
        rsp = xmono_pb2.DisasmMethodRsp()
        rsp.ParseFromString(packet.data)
        if rsp.err == False:
            self.log.e(u"disasm method err : {0}".format(rsp.err_str))
            return
        self._code = rsp.asm_code
        print type(self._code)
        self.cilDisasmed.emit(rsp.disasm_code)

    def _recvReplaceMethodRsp(self, packet):
        rsp = xmono_pb2.ReplaceMethodRsp()
        rsp.ParseFromString(packet.data)
        if rsp.err:
            self.log.i(u"{0}".format(rsp.msg))
        else:
            self.log.e(u"{0}".format(rsp.msg))
        return

    def _recvListDomain(self, packet):
        rsp = xmono_pb2.ListDomainRsp()
        rsp.ParseFromString (packet.data)
        self.log.i(u"Domain List :")
        for i in rsp.id:
            self.log.i(u"\t{0}".format(i))

    def _recvStackTrace(self, packet):
        rsp = xmono_pb2.StackTraceRsp()
        rsp.ParseFromString(packet.data)
        if rsp.err == False:
            self.log.e(u"{0}".format(rsp.err_str))
            return
        l = rsp.stack.split("\n")[:-1]
        self.stackTraceWindow.addTraceResult(l)

    def _luaLog(self, lv, msg):
        if lv == xmono_pb2.err:
            self.log.e (msg)
        elif lv == xmono_pb2.info:
            self.log.i (msg)
        elif lv == xmono_pb2.warring:
            self.log.w (msg)
        elif lv == xmono_pb2.debug:
            self.log.d (msg)
        else:
            self.log.e (u"Lua执行回包中出现未知的level")

    def _recvExecLua(self, packet):
        rsp = xmono_pb2.LuaExecRsp()
        rsp.ParseFromString(packet.data)
        self._luaLog(rsp.level, rsp.message)

    def _recvLuaHook(self, packet):
        rsp = xmono_pb2.LuaHookRsp()
        rsp.ParseFromString(packet.data)
        self._luaLog(rsp.level, rsp.message)

    def _print2Log(self, msg, level):
        """level WARNING INFO DEBUG ERROR"""
        title = {'ERROR':'<font color="#FF2D2D">','DEBUG':'<font color="#2828FF">',
                 'WARNING':'<font color="#D200D2">', 'INFO':'<font>',}
        if level not in title.keys(): return
        self.ui.logTextBrowser.append("%s[+]%s</font>" % (title[level], msg.replace('\n', '<br/>')))