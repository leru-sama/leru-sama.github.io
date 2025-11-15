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

整体读下来，难还是不难的，整体思路为：python注册component时，可以注册这个conpenent的参数。C++写代码时，先把参数拿进来，然后处理port，实现这个conponent的各个port的接收、处理和发送。

port分为req的port和resp的port，分别包含接受、发送和重传的函数，倒是建模的很好，终极教程其实也就是这个[cache](https://www.gem5.org/documentation/learning_gem5/part2/simplecache/)，不想复制粘贴了，直接看这个cache就行。

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


### 事件驱动的仿真

参考[官方教程](https://www.gem5.org/documentation/learning_gem5/part2/events/)

还是按照HelloObject的流程来走。

GEM5靠事件驱动，通过创造一个类似于事件句柄的东西来构造事件，在Object的构造函数中加入，然后通过一个schedule方法来调度事件。

这个例子的调度流程为：startup()函数第一次调度=>processEvent()函数处理事件并schedule下一次调度=>下一次调度又触发processEvent()函数=>重复以上过程

以下的代码和HelloObject的代码基本相同，加入了一个processEvent函数，用来处理事件，加了一个事件句柄event，加了一堆参数，用来配置事件的延迟和次数。

头文件多了两个，一个是base/trace.hh，一个是debug/HelloExample.hh。都是用来Debug的，暂时不管。

```C++
// src/learning_gem5/part2/hello_object.hh
#ifndef __LEARNING_GEM5_HELLO_OBJECT_HH__
#define __LEARNING_GEM5_HELLO_OBJECT_HH__

#include "params/HelloObject.hh"
#include "sim/sim_object.hh"

#include "base/trace.hh"
#include "debug/HelloExample.hh"


namespace gem5
{

class HelloObject : public SimObject
{
  private:
    void processEvent();

    EventFunctionWrapper event;

    const Tick latency;

    int timesLeft;

  public:
    HelloObject(const HelloObjectParams &p);

    void startup() override;
};

} // namespace gem5

#endif // __LEARNING_GEM5_HELLO_OBJECT_HH__
```
这里要注意的是，事件句柄event的构造函数中，第一个参数是一个lambda表达式，用来指定事件触发时调用的函数。这里是调用processEvent函数。第二个参数是事件的名称，这里是name()，就是SimObject的名称。相当于把processEvent函数绑定到事件句柄上，当事件触发时，就会调用processEvent函数。

{{<admonition info "info">}}
lambda表达式形式为 \[捕获列表\]{函数体} -> 返回值类型，返回值类型可以省略。捕获列表为该表达式将要使用的外部变量，这里是this，就是指当前对象，函数体为this->processEvent()，就是调用当前对象的processEvent函数，此处省略了this。
{{</admonition>}}


```C++
// src/learning_gem5/part2/hello_object.cc
#include "learning_gem5/part2/hello_object.hh"

namespace gem5
{

HelloObject::HelloObject(const HelloObjectParams &params) :
    SimObject(params), event([this]{processEvent();}, name()),
    latency(100), timesLeft(10)
{
    DPRINTF(HelloExample, "Created the hello object\n");
}

void HelloObject::startup()
{
    schedule(event, latency);
}

void HelloObject::processEvent()
{
    timesLeft--;
    DPRINTF(HelloExample, "Hello world! Processing the event! %d left\n", timesLeft);

    if (timesLeft <= 0) {
        DPRINTF(HelloExample, "Done firing!\n");
    } else {
        schedule(event, curTick() + latency);
    }
}

} // namespace gem5
```

### 自己写的一个矩阵加速器

不得不说vibe coding真是太强了，边看小说边写就搞定了，正好现在边记录边回顾一下。

#### 规格定义
首先确定建模的是个什么东西，一个矩阵加速器，用来加速矩阵乘法。

输入：
- 两个矩阵A和B，均为固定大小（16x16）的矩阵，使用uint8_t类型存储。
- 触发计算的寄存器，写入任意值开始计算
- 状态寄存器，只读，用于查询计算是否完成。

输出：
- 一个矩阵C，使用uint32_t类型存储。

#### 实现

首先，写python，定义一些parameter
{{< codelink path="gem5_simple_mat/simple_matrix.py" text="配置信息" >}}

然后写C++，注意这个东西是继承自BasicPioDevice，{{< codelink path="gem5_simple_mat/simple_matrix.hh" text="头文件" >}}，{{< codelink path="gem5_simple_mat/simple_matrix.cc" text="源文件" >}}。

这里就不把所有代码贴过来了，只看看关键的地方：

注意C++头文件中，继承自BasicPioDevice，这个函数自带了PIO接口，就不用管接口的声明了。
此处继承了read和write函数，即外部对这个设备的读写操作。这两个函数返回值为tick类型，代表模块接口的访问延迟（以时钟为单位）。只需要实现这两个函数，其实就已经完成了对外部的交互了，非常友好，点赞！

```C++
class SimpleMatrix : public BasicPioDevice{
    
     /**
     * Handle a read from the device.
     * @param pkt The packet to handle
     * @return Tick delay
     */
    Tick read(PacketPtr pkt) override;
    
    /**
     * Handle a write to the device.
     * @param pkt The packet to handle
     * @return Tick delay
     */
    Tick write(PacketPtr pkt) override;

    //other declaration
}
```

read函数的处理根据ADDR分成了很多个read函数，这里看一个readMatrixA函数。

PacketPtr pkt是一个指向Packet类的指针，用于表示一个内存访问请求。主要的其实也就是getAddr、getSize、setData函数，分别用来获取访问的地址和大小，以及设置返回的数据。makeResponse函数用来发响应回去。

```C++
void
SimpleMatrix::readMatrixA(PacketPtr pkt)
{
    Addr addr = pkt->getAddr() - pioAddr;
    
    // Read matrix A
    uint32_t offset = (addr - matrixABase) / sizeof(uint8_t);
    
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        pkt->setData((uint8_t*)(matrixA.data() + offset));
    }
    
    DPRINTF(SimpleMatrix, "Reading matrix A at offset %#x\n", offset);
    pkt->makeResponse();
}

```

write函数类似，也是直接根据ADDR分成了很多个write函数，这里看一个updateMatrixA函数。

注意这里pkt->getConstPtr<uint8_t>()用来获取写入的数据，注意要根据数据类型来转换。

```C++
void
SimpleMatrix::updateMatrixA(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Updating matrix A at offset %#x\n", 
            pkt->getAddr() - pioAddr - matrixABase);
    
    // Calculate the offset in the matrix
    uint32_t offset = (pkt->getAddr() - pioAddr - matrixABase) / sizeof(uint8_t);
    
    // Copy data to matrix A
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        const uint8_t* data = pkt->getConstPtr<uint8_t>();
        for (uint32_t i = 0; i < pkt->getSize()/sizeof(uint8_t); i++) {
            matrixA[offset + i] = data[i];
        }
    }
    
    pkt->makeResponse();
}
```

有的寄存器是控制寄存器，写入后会触发一些操作，比如开始计算，这里是写入控制寄存器的write函数：
，在pipelineLantency个时钟周期后安排了计算事件。

```C++

void
SimpleMatrix::startComputation(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Starting matrix multiplication\n");
    
    if (computationInProgress) {
        DPRINTF(SimpleMatrix, "Computation already in progress\n");
        pkt->makeResponse();
        return;
    }
    
    // Set computation in progress
    computationInProgress = true;
    computationComplete = false;
    
    // Schedule the computation event
    schedule(new ComputationEvent(this), clockEdge(pipelineLatency));
    
    pkt->makeResponse();
}

```

这个就是事件的句柄类，只需要重载process函数，这个句柄可以去simple_matrix.hh中看，其实也就是继承了Event然后把这个simple_matrix类作为参数传进去，就可以直接调用this->performComputation()，这个matrix就是this。

```C++
void
SimpleMatrix::ComputationEvent::process()
{
    // Perform the actual computation
    matrix->performComputation();
}

```

**搞定！**





