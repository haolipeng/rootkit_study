#ifndef _TP_SAMPLES_TRACE_H
#define _TP_SAMPLES_TRACE_H

#include <linux/proc_fs.h>	/* for struct inode and struct file */
#include <linux/tracepoint.h>

//声明 struct tracepoint _tracepoint_subsys_event 结构体
//声明跟踪点：subsys_event
DECLARE_TRACE(subsys_event,
	//跟踪点调用的函数原型,跟踪点的probe函数也是相同的函数参数
	//将跟踪点的参数传递给probe函数，就像使用这些参数调用回调函数一样
	TP_PROTO(struct inode *inode, struct file *file),
	//跟踪点调用的函数原型中相同的参数名称
	TP_ARGS(inode, file));
#endif
