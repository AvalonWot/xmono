# -*- coding: utf-8 -*-
# -*- coding: utf-8 -*-
#file: cil.py
#author: skeu
#description: cil显示,编辑,编译模块

from PyQt4 import QtCore, QtGui
import cilUI
import ilopcode
import re, struct

class CilWindow(QtGui.QMainWindow):
    """cil主类"""
    compiledSig = QtCore.pyqtSignal(str, int, tuple, name = 'compiled')
    def __init__(self, parent=None):
        QtGui.QMainWindow.__init__(self, parent)
        self.ui = cilUI.Ui_CilWindow()
        self.ui.setupUi(self)
        self._createActions()
        self._createMenus()
        self._slotConnects()

    def _createActions(self):
        self.compileAct = QtGui.QAction(u"编译", self)
        self.sendAct = QtGui.QAction(u"发送到客户端", self)

    def _createMenus(self):
        menuBar = self.menuBar()

        funcMenu = menuBar.addMenu(u"函数")
        funcMenu.addAction(self.compileAct)
        funcMenu.addAction(self.sendAct)

    def _slotConnects(self):
        self.compileAct.triggered.connect(self._compile)

    def showCil(self, code):
        self.show()
        self.ui.ilTextBrowser.clear()
        self.ui.ilTextBrowser.append(code)

    def WinInit(self):
        return

    def _compile(self):
        s = str(self.ui.ilTextBrowser.toPlainText())
        err, result = cil_compile(s)
        if err:
            print "编译成功!"
            msg_box = QtGui.QMessageBox()
            msg_box.setText(u"编译成功!")
            msg_box.exec_()
            self.compiledSig.emit(result[0], result[1], (result[2], result[3]))
        else:
            print "编译失败: {0}".format(result)
            msg_box = QtGui.QMessageBox()
            msg_box.setText(u"{0}".format(result.decode('utf-8')))
            msg_box.exec_()


class LineInfo(object):
    def __init__(self):
        self.labels = []
        self.opcode = None
        self.jmps = []

class ExceptionClause(object):
    def __init__(self):
        self.try_offset = 0
        self.try_len = 0
        self.handler_offset = 0
        self.handler_len = 0

def _strip_comment(line):
    line = line.strip()
    i = line.find('//')
    if i == -1:
        return line
    else:
        return line[:i]

#假设传入的是非空字符串
def _read_label(line):
    line = line.strip()
    i = line.find(':')
    if i != -1:
        return line[:i], line[i+1:].strip()
    return None, line

#假设传入的是非空字符串
def _read_opcode(line):
    line = line.strip()
    #print "_read_opcode : " + line
    ma = re.match('([a-z0-9A-Z_\.\}]+)',line)
    if not ma:
        raise Exception("无效的指令")
    op_str = ma.groups()[0]
    op = ilopcode.search_opcode(op_str)
    if not op:
        raise Exception("无效的指令")
    return op, line[ma.end():].strip()

def _read_op_argment(op, line):
    line = line.strip()
    if op.arg_type == 'InlineBrTarget':
        ma = re.match('(\w+)', line)
        if not ma:
            raise Exception("无效的跳转标号")
        op.add_size(4)
        return [ma.groups()[0]], line[ma.end():].strip()
    elif op.arg_type == 'ShortInlineBrTarget':
        ma = re.match('(\w+)', line)
        if not ma:
            raise Exception("无效的跳转标号")
        op.add_size(2)
        return [ma.groups()[0]], line[ma.end():].strip()
    elif op.arg_type == 'InlineField' or op.arg_type == 'InlineMethod' or \
         op.arg_type == 'InlineSig' or op.arg_type == 'InlineString' or \
         op.arg_type == 'InlineTok' or op.arg_type == 'InlineType':
        ma = re.match('\[([0-9A-Fa-f]{8})\]', line)
        if not ma:
            raise Exception("无效的token")
        v = struct.pack("=i", int(ma.groups()[0],16))
        op.args.append(v)
        op.add_size(4)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'InlineVar':
        ma = re.match('(0x[0-9]+|[+-]?[0-9]+)', line)
        if not ma:
            raise Exception("无效的InlineVar")
        n = int(ma.groups()[0],0)
        if n < 0 and n > 65536:
            raise Exception("无效的InlineVar")
        v = struct.pack("=H", n)
        op.args.append(v)
        op.add_size(2)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'InlineI':
        ma = re.match('(0x[0-9]+|[+-]?[0-9]+)', line)
        if not ma:
            raise Exception("无效的整数")
        v = struct.pack("=i", int(ma.groups()[0], 0))
        op.args.append(v)
        op.add_size(4)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'InlineI8':
        ma = re.match('(0x[0-9]+|[+-]?[0-9]+)', line)
        if not ma:
            raise Exception("无效的整数")
        v = struct.pack("=q", int(ma.groups()[0], 0))
        op.args.append(v)
        op.add_size(8)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'InlineR':
        ma = re.match('[+-]?([0-9]+(\.[0-9]+)?)', line)
        if not ma:
            raise Exception("无效的浮点数")
        v = struct.pack("=d", float(ma.groups()[0]))
        op.args.append(v)
        op.add_size(8)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'ShortInlineR':
        ma = re.match('[+-]?([0-9]+(\.[0-9]+)?)', line)
        if not ma:
            raise Exception("无效的浮点数")
        v = struct.pack("=f", float(ma.groups()[0]))
        op.args.append(v)
        op.add_size(4)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'ShortInlineI' or op.arg_type == 'ShortInlineVar':
        #Fixme : ShortInlineI是有符号, ShortInlineVar是无符号 要分开
        ma = re.match('(0x[0-9A-Za-z]+|[+-]?[0-9]+)', line)
        if not ma:
            raise Exception("无效的短整数")
        n = int(ma.groups()[0], 0)
        if n > 256:
            raise Exception("无效的短整数")
        v = struct.pack("=B", n)
        op.args.append(v)
        op.add_size(1)
        return [], line[ma.end():].strip()
    elif op.arg_type == 'InlineSwitch':
        op.add_size(4)
        ma = re.match('\((([ \t]*\w*[ \t]*[,]?[ \t]*)*)\)', line)
        if not ma:
            raise Exception("无效的switch表")
        t = ma.groups()[0].strip()
        jmps = []
        if t != '':
            ts = t.split(",")
            if ts[len(ts)-1] == '':
                ts = ts[:-1]
            for l in ts:
                l = l.strip()
                if not re.match('(\w+)', l):
                    raise Exception("无效的switch表项")
                jmps.append(l)
                op.add_size(4)
        v = struct.pack("=i", len(jmps))
        op.args.append(v)
        return jmps, line[ma.end():].strip()
    elif op.arg_type == 'InlineNone':
        return [], line
    elif op.arg_type == 'InlineDot':
        if line[0] != '{':
            raise Exception("缺少花括号")
        line = line[1:].strip()
        ma = re.match('([0-9]+)', line)
        if not ma:
            raise Exception("缺少异常序数")
        n = int(ma.groups()[0], 0)
        op.args.append(n)
        return [], line[ma.end():].strip()


#假设传入的是非空字符串
def _parse_first(line):
    line = line.strip()
    #print "_parse_first : " + line
    line_info = LineInfo()
    label, line = _read_label(line)
    line_info.labels.append(label)
    if line == '':
        return line_info
    op, line = _read_opcode(line)
    jmps, line = _read_op_argment(op, line)  #感觉这里返回jmps不清晰, 但传入line_info也好不到哪去
    if line != '':
        print line
        raise Exception("多余的字节出现")
    line_info.opcode = op
    line_info.jmps = jmps
    return line_info

def _encode_line(line):
    opcode = line.opcode
    if opcode and opcode.size > 0:
        return "".join(opcode.op) + "".join(opcode.args)
    return ""


def cil_compile(s):
    """simple cil编译, s为cil文本,
        返回值: True, (cil字节码, 异常结构)
                False, 错误信息文本"""
    line_infos = []
    cil_lines = s.split("\n")
    #读第一行, 分离出method_token和image
    ma = re.search("\[(.*)\].*\[(.*)\]", cil_lines[0])
    if not ma:
        return False, "Err : 第一行缺少函数信息"
    image_name = ma.groups()[0]
    token = int(ma.groups()[1], 16)
    #第一遍parse
    for i, line in enumerate(cil_lines):
        org_line = line
        line = line.strip()
        #print line
        if line == '': continue
        line = _strip_comment(line)
        if line == '': continue
        try:
            line_infos.append(_parse_first(line))
        except Exception, err_str:
            return False, "Err : line {0} {1}: {2}!".format(i, org_line, err_str)
    #第二遍parse, 取除空余行
    i = 0
    while i < len(line_infos):
        if line_infos[i].opcode == None and line_infos[i].labels == []:
            line_infos.pop(i)
            continue
        if line_infos[i].opcode == None and line_infos[i].labels != []:
            line_infos[i+1].labels.extend(line_infos[i].labels)
            line_infos.pop(i)
            continue
        i = i + 1
    #第三遍parse, 处理分支的偏移信息
    cur_offset = 0
    for i in line_infos:
        cur_offset = cur_offset + i.opcode.size
        if len(i.jmps) < 0:
            continue
        for j in i.jmps:
            dest_offset = 0
            for _i in line_infos:
                if j in _i.labels:
                    offset = dest_offset - cur_offset
                    if i.opcode.arg_type == 'ShortInlineBrTarget':
                        if offset < -128 or offset > 127:
                            raise False, "Err : line {0} {1} : {2}!".format(i.opcode.name, j, "短偏移过大")
                        i.opcode.args.append(struct.pack("=b", offset))
                    else:
                        i.opcode.args.append(struct.pack("=i", offset))
                dest_offset = dest_offset + _i.opcode.size
    #第四遍parse, 提取实际编码
    encodes = ""
    for i in line_infos:
        encodes = encodes + _encode_line(i)
    #第五遍parse, 提取异常偏移
    cur_offset = 0
    ex = {}
    ex_stack = []
    for i in line_infos:
        i = i.opcode
        if i.arg_type == 'InlineDot':
            ex_stack.append((i, cur_offset))
        if i.name == '}':
            if len(ex_stack) == 0:
                return False, "Err : 不匹配的try catch括号"
            t = ex_stack.pop()
            #print t
            if t[0].name == '.try':
                e = ExceptionClause()
                e.try_offset = t[1]
                e.try_len = cur_offset - t[1]
                ex[t[0].args[0]] = e
            else:
                if t[0].args[0] not in ex.keys():
                    return False, "Err : catch等出现在try之前"
                ex[t[0].args[0]].handler_offset = t[1]
                ex[t[0].args[0]].handler_len = cur_offset - t[1]
        cur_offset = cur_offset + i.size
    if len(ex_stack):
        return False, "Err : 不匹配的花括号"

    return True, (image_name, token, encodes, ex)
if __name__ == '__main__':
    s = """//NetworkManager:HandleTdrPackage (byte[],int)
IL_0000:     newobj [060023B1] //cspkg.CSPKG:.ctor ()
IL_0005:     stloc.0 
IL_0006:     ldloc.0 
IL_0007:     callvirt [060023B3] //cspkg.CSPKG:construct ()
IL_000c:     pop 
IL_000d:     ldc.i4.0 
IL_000e:     stloc.1 
IL_000f:     ldloc.0 
IL_0010:     ldarga.s 1
IL_0012:     ldarg.2 
IL_0013:     ldloca.s 1
IL_0015:     ldc.i4.0 
IL_0016:     callvirt [060023B6] //cspkg.CSPKG:unpack (byte[]&,int,int&,uint)
IL_001b:     stloc.2 
IL_001c:     ldloc.2 
IL_001d:     brfalse IL_0038

IL_0022:     ldstr [7001AE57] //"NetworkManager.HandleTdrPackage unpack CSPKG error {0}"
IL_0027:     ldloc.2 
IL_0028:     box [02000393] //type
IL_002d:     call [0A000018] //string:Format (string,object)
IL_0032:     call [06002461] //WICore:InternalLog (object)
IL_0037:     ret 
IL_0038:     ldarg.0 
IL_0039:     ldfld [04001B09]    //NetworkManager:isHeartbeatTimeout
IL_003e:     brfalse IL_0049

IL_0043:     ldarg.0 
IL_0044:     call [06002268] //NetworkManager:ResetDisConnectState ()
IL_0049:     ldarg.0 
IL_004a:     call [0A000206] //UnityEngine.Time:get_realtimeSinceStartup ()
IL_004f:     stfld [04001B05]    //NetworkManager:lastRecvPackageTime
IL_0054:     ldstr [7001AEC5] //"In NetworkManager.HandleTdrPackage(), received {0} byte(s) of TDR package. cmd {1}."
IL_0059:     ldarg.2 
IL_005a:     box [0100000C] //type
IL_005f:     ldloc.0 
IL_0060:     ldfld [04001B54]    //cspkg.CSPKG:stHead
IL_0065:     ldfld [04001B4A]    //cspkg.CSPKGHEAD:dwCmd
IL_006a:     box [0100008C] //type
IL_006f:     call [0A000356] //string:Format (string,object,object)
IL_0074:     call [06002461] //WICore:InternalLog (object)
IL_0079:     ldarg.0 
IL_007a:     ldfld [04001B12]    //NetworkManager:EventNetworkResponseHandlers
IL_007f:     ldloc.0 
IL_0080:     ldfld [04001B54]    //cspkg.CSPKG:stHead
IL_0085:     ldfld [04001B4A]    //cspkg.CSPKGHEAD:dwCmd
IL_008a:     callvirt [0A000D8F] //System.Collections.Generic.Dictionary`2<uint, NetworkManager/NetworkResponseHandler>:ContainsKey (uint)
IL_008f:     brfalse IL_00dc

IL_0094:     ldsfld [0400136E]    //OnlineClient:Instance
IL_0099:     ldloc.0 
IL_009a:     ldfld [04001B54]    //cspkg.CSPKG:stHead
IL_009f:     ldfld [04001B4D]    //cspkg.CSPKGHEAD:dwServerTime
IL_00a4:     callvirt [060015D2] //OnlineClient:UpdateServerClientDeltaTime (uint)
IL_00a9:     ldarg.0 
IL_00aa:     ldfld [04001B12]    //NetworkManager:EventNetworkResponseHandlers
IL_00af:     ldloc.0 
IL_00b0:     ldfld [04001B54]    //cspkg.CSPKG:stHead
IL_00b5:     ldfld [04001B4A]    //cspkg.CSPKGHEAD:dwCmd
IL_00ba:     callvirt [0A000D94] //System.Collections.Generic.Dictionary`2<uint, NetworkManager/NetworkResponseHandler>:get_Item (uint)
IL_00bf:     stloc.3 
IL_00c0:     ldloc.3 
IL_00c1:     ldloc.0 
IL_00c2:     ldfld [04001B55]    //cspkg.CSPKG:stBody
IL_00c7:     ldfld [04001B51]    //cspkg.CSPKGBODY:szData
IL_00cc:     ldloc.0 
IL_00cd:     ldfld [04001B55]    //cspkg.CSPKG:stBody
IL_00d2:     ldfld [04001B50]    //cspkg.CSPKGBODY:dwDataLen
IL_00d7:     callvirt [06002B15] //NetworkManager/NetworkResponseHandler:Invoke (byte[],int)
IL_00dc:     ldloc.0 
IL_00dd:     ldfld [04001B54]    //cspkg.CSPKG:stHead
IL_00e2:     ldfld [04001B4C]    //cspkg.CSPKGHEAD:dwSequenceId
IL_00e7:     ldc.i4.0 
IL_00e8:     ble.un IL_0176

IL_00ed:     ldarg.0 
IL_00ee:     ldfld [04001B14]    //NetworkManager:unAckPkgList
IL_00f3:     brfalse IL_0176

IL_00f8:     ldarg.0 
IL_00f9:     ldfld [04001B14]    //NetworkManager:unAckPkgList
IL_00fe:     callvirt [0A000D88] //System.Collections.Generic.LinkedList`1<NetworkManager/PendingAckPkg>:get_Count ()
IL_0103:     ldc.i4.0 
IL_0104:     ble IL_0176

IL_0109:     ldarg.0 
IL_010a:     ldfld [04001B14]    //NetworkManager:unAckPkgList
IL_010f:     callvirt [0A000D89] //System.Collections.Generic.LinkedList`1<NetworkManager/PendingAckPkg>:GetEnumerator ()
IL_0114:     stloc.s 5
.try {  0
  IL_0116:   br IL_0158

  IL_011b:   ldloca.s 5
  IL_011d:   call [0A000D8A] //System.Collections.Generic.LinkedList`1/Enumerator<NetworkManager/PendingAckPkg>:get_Current ()
  IL_0122:   stloc.s 4
  IL_0124:   ldloc.0 
  IL_0125:   ldfld [04001B54]    //cspkg.CSPKG:stHead
  IL_012a:   ldfld [04001B4C]    //cspkg.CSPKGHEAD:dwSequenceId
  IL_012f:   ldloc.s 4
  IL_0131:   ldfld [04001B15]    //PendingAckPkg:pkg
  IL_0136:   ldfld [04001B54]    //cspkg.CSPKG:stHead
  IL_013b:   ldfld [04001B4C]    //cspkg.CSPKGHEAD:dwSequenceId
  IL_0140:   bne.un IL_0158

  IL_0145:   ldarg.0 
  IL_0146:   ldfld [04001B14]    //NetworkManager:unAckPkgList
  IL_014b:   ldloc.s 4
  IL_014d:   callvirt [0A000D95] //System.Collections.Generic.LinkedList`1<NetworkManager/PendingAckPkg>:Remove (NetworkManager/PendingAckPkg)
  IL_0152:   pop 
  IL_0153:   br IL_0164

  IL_0158:   ldloca.s 5
  IL_015a:   call [0A000D8B] //System.Collections.Generic.LinkedList`1/Enumerator<NetworkManager/PendingAckPkg>:MoveNext ()
  IL_015f:   brtrue IL_011b

  IL_0164:   leave IL_0176

} // end .try 0
.finally {  0
  IL_0169:   ldloc.s 5
  IL_016b:   box [1B00018A] //type
  IL_0170:   callvirt [0A00010B] //System.IDisposable:Dispose ()
  IL_0175:   endfinally 
} // end handler 0
IL_0176:     ret """
    a, err = cil_compile(s, None)
    if not a:
        print err
    else:
        code = err[0]
        print len(code)
        f = open("1.bin", "wb")
        f.write(code)
        f.close()
        n = 0
        x = []
        for i in code:
            if n % 16 == 0:
                print " ".join(x)
                x = []
            x.append("{0}".format(hex(ord(i))))
            n = n + 1
        print " ".join(x)
        ex = err[1]
        for i in ex.keys():
            e = ex[i]
            print e.try_offset, e.try_len
            print e.handler_offset, e.handler_len
