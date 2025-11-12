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
1. X86的单核+自己写的程序，先跑一个hello world。
2. X86的多核+自己写的程序，测试一下多核的cache hierarchy的性能。
3. 自己写一个CPU以外的东西，就当是个CIM吧，跑通
4. 自己写的片上网络，跑通。
5. 片上网络+CIM+多核+自己的程序

## X86单核上跑自己的程序

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