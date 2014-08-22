# -*- coding: utf-8 -*-
#file: py2exe_xmono.py
#author: skeu
#description: 构建 xmono.exe

from glob import glob
from distutils.core import setup
import py2exe

includes = ["sip", "PyQt4.QtGui", "PyQt4.QtCore", "PyQt4.QtNetwork"]
packages = ["google.protobuf"]
#"google.protobuf.descriptor", "google.protobuf.message","google.protobuf.reflection","google.protobuf.descriptor_pb2"
            
#data_files = [("Microsoft.VC90.CRT", glob(r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\VC\redist\x86\Microsoft.VC90.CRT\*.*'))]
dll_excludes = ["msvcm90.dll", "msvcp90.dll", "msvcr90.dll"]
setup(  version="1.0.0",
        description = u"xmono客户端",
        name = "xmono.exe",
        zipfile = None,
        windows = [{"script":"main.pyw", "icon_resources":[(1, "logo.ico")]}],
		options = {
                    "py2exe":{
                        "compressed": 2,
                        "bundle_files": 1,
                        "includes": includes,
                        "dll_excludes":dll_excludes,
                        "packages":packages
                        }
                    }
        )
