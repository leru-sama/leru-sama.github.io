+++
date = '2025-11-10T15:16:46+08:00'
draft = false
title = '做的实验'
+++

## 目标
搭一个多核的系统，测试一下片上网络+缓存策略+同步策略的性能。

{{<admonition info "info">}}
    benchmark用什么？这个要想一想
{{</admonition>}}

实验分几个阶段：
1. 自己写的程序，先跑一个hello world。
2. 多核+自己写的程序，测试一下多核的cache hierarchy的性能。
3. 自己写一个CPU以外的东西，就当是个CIM吧，跑通
4. 自己写的片上网络，跑通。
5. 片上网络+CIM+多核+自己的程序

## 跑自己的程序

### 装环境
首先是装环境，参考的是[官方文档的 Docker 指南](https://www.gem5.org/documentation/general_docs/building#docker)。

```bash
docker pull ghcr.io/gem5/ubuntu-24.04_all-dependencies:latest # 拉取最新的docker镜像

docker run -u $UID:$GID --volume ~/gem5_workspace:/gem5 --rm -it sha256:bf02f9de3631a35d95c429e579a62438e99f724ff873996e24b93b8a00c4ffec # 运行docker容器，挂载本地目录~/gem5_workspace到容器中，用户ID和组ID保持一致，那一堆哈希码就是容器的名字

git clone https://github.com/gem5/gem5 # 克隆gem5仓库

scons build/{ISA}/gem5.{variant} -j {cpus} # 编译gem5，{ISA}是架构，{variant}是编译选项，{cpus}是线程数，ISA有ARM、NULL、MIPS、POWER、RISCV、SPARC、X86，编译选项有debug、opt、fast，我这儿图方便就直接fast然后X86了
```
### 跑hello world
docker跑起来了就直接开干，没跑起来的话先给跑起来
```bash
docker run -u $UID:$GID --volume ~/gem5_workspace:/gem5 --rm -it sha256:bf02f9de3631a35d95c429e579a62438e99f724ff873996e24b93b8a00c4ffec # 运行docker容器，挂载本地目录~/gem5_workspace到容器中，用户ID和组ID保持一致，那一堆哈希码就是容器的名字
```
vscode打开容器，点左下角的那个连接的小点点，如下图：
{{< figure src="/gem5_learn/experiment/vscode connect.jpg" alt="vscode连接容器" width="500" >}}

连上了就可可以愉快的写代码了，以下是一个简单的python配置程序：
{{<admonition info "info">}}
    注意脚本里面使用自己写的程序的workload配置，网上到处都找不到
{{</admonition>}}
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
from gem5.resources.resource import Resource
# from pathlib import Path

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
processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=1)
board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# 设置workload, 代码存储在本地：/gem5/configs/leru/hello

from gem5.resources.resource import BinaryResource
binary_path = "/gem5/configs/leru/hello"
binary_resource = BinaryResource(local_path=binary_path)
board.set_se_binary_workload(binary_resource)

board.workload.wait_for_remote_gdb = False                  #不等待GDB链接

simulator = Simulator(board=board)
simulator.run()
```
{{<admonition info "info">}}
    编译自己的程序的时候，记得加-static，用于静态链接。因为我用的是X86的CPU，所以直接用gcc来编译就ok，用arm的话得用交叉编译链。
{{</admonition>}}

运行脚本，我编译的是x86的fast
```bash
./build/X86/gem5.fast  ./configs/leru/simple_x86.py 
```
最后，在gem5的输出中可以看到hello world的输出。

### 裸机跑hello world
网上全是上了操作系统内核的，教程都找不到。

裸机跑 helloworld 分两部分：gem5 配置文件和裸机 kernel 程序。kernel 程序直接借用 [tukl-msd/gem5.bare-metal](https://github.com/tukl-msd/gem5.bare-metal) 仓库里的 `hello` 示例，无需自己从头写汇编。

他的这个其实写的很简单，初始化栈顶就直接跳到main去了，用printf打印的话，gem5会直接输出到一个remote的terminal，注意打印信息就ok。intrrupt里面有中断的汇编，加了一个timer的中断。

实现的过程如下：
1. 编译kernel程序main.elf
2. 直接跑
```bash
./build/ARM/gem5.fast ./configs/example/arm/baremetal.py --kernel /gem5/main.elf
```
出现类似于以下的输出：
```bash
ubuntu@6aae7a680a71:/gem5$ ./build/ARM/gem5.fast ./configs/example/arm/baremetal.py --kernel /gem5/
main.elf
gem5 Simulator System.  https://www.gem5.org
gem5 is copyrighted software; use the --copyright option for details.

gem5 version 25.0.0.1
gem5 compiled Nov 12 2025 07:01:04
gem5 started Nov 13 2025 06:03:30
gem5 executing on 6aae7a680a71, pid 97929
command line: ./build/ARM/gem5.fast ./configs/example/arm/baremetal.py --kernel /gem5/main.elf

Global frequency set at 1000000000000 ticks per second
src/mem/dram_interface.cc:690: warn: DRAM device capacity (8192 Mbytes) does not match the address range assigned (2048 Mbytes)
src/sim/kernel_workload.cc:46: info: kernel located at: /gem5/main.elf
src/arch/arm/system.cc:97: warn: Highest ARM exception-level set to AArch64 but the workload is for AArch32. Assuming you wanted these to match.
src/base/statistics.hh:279: warn: One of the stats is a legacy stat. Legacy stat is a stat that does not belong to any statistics::Group. Legacy stat is deprecated.
system.vncserver: Listening for connections on port 5900
system.terminal: Listening for connections on port 3456
system.realview.uart1.device: Listening for connections on port 3457
system.realview.uart2.device: Listening for connections on port 3458
system.realview.uart3.device: Listening for connections on port 3459
src/dev/arm/energy_ctrl.cc:252: warn: Existing EnergyCtrl, but no enabled DVFSHandler found.
```
注意里面的system.terminal，这是gem5的一个remote terminal，用来输出打印信息。使用
```bash
leru@DESKTOP-G9S8DTU:~/gem5.bare-metal$ telnet localhost 3456
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
==== m5 terminal: Terminal 0 ====
Hello World! 1337
Hello The fucking world!
```
### 配置多核
GEM5上配置多核很简单，直接num_core狠狠加就行了，程序这边麻烦一点，首先要在link脚本里增加stack的容量，不然容易越界，然后通过get_core_ID等方法获取core ID，否则GEM5默认所有核心运行同一个ELF文件，会出问题。[多核的hello world](https://github.com/leru-sama/gem5.bare-metal)其实就是fork的单核改了改。，最终效果如下：
```bash
leru@DESKTOP-G9S8DTU:~$ telnet localhost 3456
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
==== m5 terminal: Terminal 0 ====
Hello World from Core 0! Value: 1337
Hello World from Core 1! Value: 1337
Hello World from Core 2! Value: 1337
Hello World from Core 3! Value: 1337
Hello World from Core 4! Value: 1337
Hello World from Core 5! Value: 1337
Hello World from Core 6! Value: 1337
Hello World from Core 7! Value: 1337
```

## 自己写一个CPU以外的东西

自己用GEM5建模一个CPU以外的东西，目前来说目标是一个矩阵计算的加速器。

大致分3步，挺八股文的：
1. python注册这个component。
2. C++实现这个component的功能。
3. 写SConscript脚本，然后**重新编译整个GEM5** ~~真服了，编译一年~~
4. 写python脚本，把建模的东西给连到系统上。

### Hello Object例程
这个例程来自官方教程，完整步骤见 [HelloObject 文档](https://www.gem5.org/documentation/learning_gem5/part2/helloobject/)。写的代码的位置都是放在src下面的

#### python注册component
八股文，type和C++的class名要相同，路径认为src是根目录
```python
# src/learning_gem5/part2/hello_object.py
from m5.params import *
from m5.SimObject import SimObject

class HelloObject(SimObject):
    type = 'HelloObject'
    cxx_header = "learning_gem5/part2/hello_object.hh"
    cxx_class = "gem5::HelloObject"
```

#### C++实现component的功能
照抄就行，值得注意的是，构造函数里面有一个参数 HelloObjectParams &p，这个是自动生成的类名+Params，应该使用了反射之类的机制

```C++
// src/learning_gem5/part2/hello_object.hh
#ifndef __LEARNING_GEM5_HELLO_OBJECT_HH__
#define __LEARNING_GEM5_HELLO_OBJECT_HH__

#include "params/HelloObject.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class HelloObject : public SimObject
{
  public:
    HelloObject(const HelloObjectParams &p);
};

} // namespace gem5

#endif // __LEARNING_GEM5_HELLO_OBJECT_HH__
```

教程里边说需要实现两个函数，一个是构造函数，一个是debug用的输出函数，cout一般是不用的，但是这儿用，教程里说是留到下一节在讲。
```C++
// src/learning_gem5/part2/hello_object.cc
#include "learning_gem5/part2/hello_object.hh"

#include <iostream>

namespace gem5
{

HelloObject::HelloObject(const HelloObjectParams &params) :
    SimObject(params)
{
    std::cout << "Hello World! From a SimObject!" << std::endl;
}

} // namespace gem5

```

#### 写SConscript脚本

照抄完事。
```python
# src/learning_gem5/part2/SConscript
Import('*')

SimObject('HelloObject.py', sim_objects=['HelloObject'])
Source('hello_object.cc')

```

写完了就可以重新编译了

```bash
scons build/ARM/gem5.fast
```

~~然后就等一年，复制粘贴完这一段还没编译完。。。。~~
