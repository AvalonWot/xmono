#define DEBUG_MODE

#include <stdio.h>
#include "inject.h"

int main(int argc, char *argv[])
{
	NDKINJECT inj;
	pid_t target_pid;

	//根据进程名找PID
	target_pid = inj.find_pid_of("com.tencent.WeIsland");
	//注入参数：PID，要注入的SO路径，注入后立即执行的SO入口函数名，入口函数字符串参数，字符串参数长度，执行完入口函数是否立即释放SO
	inj.inject_remote_process(target_pid, "/data/local/tmp/xmono/libxmono.so", "so_main", NULL, 0, 0);
	
	return 0;
}