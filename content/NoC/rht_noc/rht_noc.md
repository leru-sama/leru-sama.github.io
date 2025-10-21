+++
date = '2025-10-20T20:58:23+08:00'
draft = false
title = 'Rht_noc'
+++

# RHT NoC A Reconfigurable Hybrid Topology Architecture for Chiplet-Based Multicore System

这篇论文[@11026095]来自VLSI，今天先看看。

这篇论文做了一个半带宽mesh，然后用一个bufferless tours来弥补半带宽mesh的不足。

## 半带宽mesh
使用依据是[@10286455]中写的，百分之80情况下的相邻router带宽都是单向的，所以索性就只做半带宽的mesh来节省资源，router之间通过一个channel的adaptable link[@6835942]连接，然后再路由器里面增加了一级LC（link control），用来判定link是朝哪个方向走。

{{< figure src="/NoC/rht_noc/router_with_link_controller.jpg" alt="加入 LC 的 router" caption="加入 LC 的 router" >}}




LC的判断逻辑是根据两端待传输的flit进行协商，优先传输flit多也就是带宽需求大的。这玩意会带来一个等待链路授权的时间，这个文章里面通过一个bufferless torus解决。

## bufferless torus
用一个bufferless torus来处理mesh的剩余带宽。这个torus也是用adaptable link来做的，也是只能做单向的流量，里面的路由器称作Ring Interface（RI）

因为是bufferless的，所以构造和普通的有点不一样，如下图所示啊，东南西北各一条adaptable link，然后local是双向带宽ejection、injection
此外还有一个到mesh router的buffer的单向链路。

{{< figure src="/NoC/rht_noc/RI_microarchetecture.jpg" alt="RI_microarchetecture" caption="RI_microarchetecture" >}}

{{< figure src="/NoC/rht_noc/RI_loop_conbine.jpg" alt="RI_loop_conbine" caption="RI_loop_conbine" >}}

如上面右边的图所示，通过某种算法确定路由器的转弯方向，最终保证每个垂直的环与水平的环形成一个逻辑上的大环，如下图所示：

{{< figure src="/NoC/rht_noc/logical_loop.jpg" alt="垂直环和水平环形成的逻辑环" caption="垂直环和水平环形成的逻辑环" >}}
