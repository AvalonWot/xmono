# -*- coding: utf-8 -*-
#author: skeu
#date: 2014/6/12
#version: 1.0.0
#function: Log类

class Log(object):
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
