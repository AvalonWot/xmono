arm下的mono辅助工具

依赖的程序:
	1.python 2.7
	2.lua 5.1及以上
	3.protocol buffers
	4.pyqt4
	5.py2exe

gui执行:
	1.运行gui/make_ui.bat生成pyqt的UI文件
	2.运行gui/make_res.bat生成资源文件
	3.运行jni/parse_cmdid.bat生成const_cmdid.py[通信ID]
	3.运行main.py执行

gui生成exe:
	1.运行make_ui.bat生成pyqt的UI文件
	2.运行make_res.bat生成资源文件
	3.运行jni/parse_cmdid.bat生成const_cmdid.py[通信ID]
	4.运行make_exe.bat

server生成:
	1.在jni/目录下执行ndk-build
	2.若执行ndk-build时, 手机连接, 有adb且手机root的情况下, 会自动拷贝libxmono.so到手机的/data/local/tmp/xmono/目录下
	3.在libs目录可提取libxmono.so