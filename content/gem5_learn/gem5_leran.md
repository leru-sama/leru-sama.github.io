---
title: "GEM5 学习"
date: 2025-11-06
draft: false
---

本文档主要参考 [gem5 官方入门教程](https://www.gem5.org/documentation/learning_gem5/introduction/) 编写。

## 什么是GEM5
贴一个原文：gem5 is a modular discrete event driven computer system simulator platform. That means that:

1. gem5’s components can be rearranged, parameterized, extended or replaced easily to suit your needs.
2. It simulates the **passing of time as a series of discrete events**.
3. Its intended use is to simulate one or more computer systems in various ways.
4. It’s more than just a simulator; it’s a simulator platform that lets you use as many of its premade components as you want to build up your own simulation system.

## 环境配置
使用vscode + docker安装gem5的环境，安装需要很大的内存，我给了14G+32Gswap。

安装完成后，vscode装上各种插件成功访问到容器，写python的时候会发现找不到gem5的库，需要在vscode的settings.json中添加如下配置：
```json
//这个是从gem5官网上搞下来的容器，然后安装路径是/gem5/gem5
{
    "python.autoComplete.extraPaths": [
        "/usr/lib/python3.12/dist-packages",
        "/usr/lib/python3.12/lib-dynload",
        "/usr/local/lib/python3.12/dist-packages",
        "/usr/lib/python3/dist-packages",
        "/gem5/gem5/src/python",  // gem5 Python源码路径
        "/gem5/gem5/build/ALL"    // gem5编译后的库路径
    ],
    "python.analysis.extraPaths": [
        "/usr/lib/python3.12/dist-packages",
        "/usr/lib/python3.12/lib-dynload",
        "/usr/local/lib/python3.12/dist-packages",
        "/usr/lib/python3/dist-packages",
        "/gem5/gem5/src/python",  // gem5 Python源码路径
        "/gem5/gem5/build/ALL"    // gem5编译后的库路径
    ],
    "python.languageServer": "Pylance"
}
```
python的安装路径可以使用以下命令：
```python
python3 -c "import sys; print(sys.path)"
```
搞定过后就可以愉快的跑例程了~

## 简介

### 整体概述啊
GEM5有一个standard library，里面包含了许多仿真用的模块，分为以下几类：
- Board: **不像顶层模块，更像互联网络**。The “backbone” of the system. You plug components into the board. The board also contains the system-level things like devices, workload, etc. It’s the boards job to negotiate the connections between other components.
- Processor: **处理器**。Processors connect to boards and have one or more cores.
- Cache hierarchy: **cache结构**。A cache hierarchy is a set of caches that can be connected to a processor and memory system.
- Memory system: **内存系统**。A memory system is a set of memory controllers and memory devices that can be connected to the cache hierarchy.

GEM5定义的模块使用C++代码写（继承自SimObject类），然后用python脚本来调用，称为SimObject。

### 简单例程
以下是一个简单的例程：例化一个board，然后把例化的processor、cache hierarchy、memory system、workload都连接到board上，最后开始仿真就完了。
注意代码里有一行
```python 
board.workload.wait_for_remote_gdb = False                  #不等待GDB链接
```
不然的话仿真就不动，除非新开一个remote gdb然后给他连上
```python
    from gem5.components.boards.simple_board import SimpleBoard
    from gem5.components.processors.simple_processor import SimpleProcessor
    from gem5.components.cachehierarchies.ruby.mesi_two_level_cache_hierarchy import (
        MESITwoLevelCacheHierarchy,
    )
    from gem5.components.memory.single_channel import SingleChannelDDR4_2400
    from gem5.components.processors.cpu_types import CPUTypes
    from gem5.isas import ISA
    from gem5.resources.resource import obtain_resource
    from gem5.simulate.simulator import Simulator

    cache_hierarchy = MESITwoLevelCacheHierarchy(
        l1d_size="16KiB",
        l1d_assoc=8,
        l1i_size="16KiB",
        l1i_assoc=8,
        l2_size="256KiB",
        l2_assoc=16,
        num_l2_banks=1,
    )
    memory = SingleChannelDDR4_2400()
    processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.ARM, num_cores=1)
    board = SimpleBoard(
        clk_freq="3GHz",
        processor=processor,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )
    board.set_workload(obtain_resource("arm-gapbs-bfs-run"))    #使用在线的workload，自己的workload也可以自己写
    board.workload.wait_for_remote_gdb = False                  #不等待GDB链接

    simulator = Simulator(board=board)
    simulator.run()
```

### gem5模块
GEM5提供了一些预定义的模块，在``` gem5/src/python/gem5/components ```目录下边，有board、processor、cache hierarchy、memory system等。

processor比较特殊，可能包含多核，多核的情况下的processor已经定义好了core之间的互联，好像也定义好了private cache？再看看。processor有一个CPUTYPE参数，如下所述：
- CPUTypes.TIMING: 简单的单周期处理器，一个周期处理一个指令，memory的延迟影响很大（阻塞流水）。
- CPUTypes.O3: 一个Out-of-Order处理器模型，使用simple processor时不可配置这个。
- CPUTypes.ATOMIC: 四级流水的单指令处理器，比TIMING要厉害一点，使用simple processor时不可配置。

### FS和SE模式
gem5提供了两种仿真模式：SE和FS，SE不用启动仿真CPU的OS，直接使用宿主机的systemcall，而FS可以做SE所有的事，并且可以启动仿真的OS。
{{<admonition info "info">}}
我用的话就直接用SE mode就行，还快一点。
{{</admonition>}}

