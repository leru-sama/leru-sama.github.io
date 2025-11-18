+++
date = '2025-10-20T20:56:19+08:00'
draft = false
title = 'Adapt-NoC: A Flexible Network-on-Chip Design for Heterogeneous Manycore Architectures'
tags = ['NoC', 'Adaptable NoC']
+++

这篇论文{{< cite zhengAdaptNoCFlexibleNetworkonChip2021 >}}做了一个动态可配置的NoC，可以将一个大的mesh网络分割为任意拓扑的子网，通过设计adaptable router和adaptable link来实现。

整体来说就是一个基础款的mesh网络加上一些可配置的link，这些link可以用将子网络组成不同的拓扑结构。

{{< figure src="/noc/adapt_noc/adaptable_NoC_overview.jpg" alt="Adapt-NoC总体结构" caption="Adapt-NoC总体结构" >}}

## adaptable router and adaptable link
adaptable router就是再普通的router外部加了一堆mux，具体路由算法可能得再看看，
adaptable link有点意思，使用了 link segmentation and link reversal。

{{< figure src="/noc/adapt_noc/adaptable_router_adaptable_link.jpg" alt="adaptable router and adaptable link" caption="adaptable router and adaptable link" >}}

- **link segmentation**: 这玩意将链路给分段，每个segmentaion就是相邻两个路由器之间的link，然后通过控制逻辑进行控制，可以配置为bypass什么的。
- **link reversal** {{< cite ditomasoQOREFaultTolerant2014 >}}: 这玩意将每段链路设计为可反转的形态，获取可配置的双向带宽。 (引用的这篇文章{{< cite ditomasoQOREFaultTolerant2014 >}}，做这个的目的是做fault tolerence)

## SubNoC
通过adaptable link来组子网。里面每2乘4个router下边放了一个memory controllor，可以做子网间的数据共享什么的，不重要。

通过{{< cite lysneMethodologyDevelopingDeadlockfree2005 >}}的方法实现**无死锁的动态路由策略切换**，具体而言有如下几步：

1. 由一个控制端口？（这个论文里没讲）发送一个切换路由策略的消息，接到消息的路由器加载Rmesh（mesh的路由算法）
2. 等到没有需要由老的路由算法传递的包以后，移除旧路由策略Rold及其对应的link configuration并加载新路由策略Rnew以及对应的link configuration。
3. 最后，Rnew配置完成后，移除Rmesh。

组子网的举例：

{{< figure src="/noc/adapt_noc/constructionOfSubNoC.jpg" alt="SubNoC的构造" caption="SubNoC的构造" >}}

- **cmesh**: 每个2 × 2的相邻路由器做了一个concentrate link,把2 × 2的Core连到一个路由器上面去，然后用 adaptable link 来连接 2 × 2 的Core，形成一个稀疏的NoC
- **torus**: 普通的mesh加上adaptable link作为bypass路径，形成完整拓扑。
- **tree**: 可以为更高层的父节点提供更大的带宽，一个router有4个端口，分别可以通过基础款的mesh以及adaptation link吃满下游的3个带宽，然后回复1个带宽给上游。**这个不拿来做adaptable，拿来做广播好像也挺好**。
  这个例子里面将最右下角的router作为根节点，以获取最多的空闲接口（右边和下面都是空着的），将边上的router作为第一级节点，获得次大的空闲接口（下边是空着的）


## 小结
> 这个东西可以作为缓存一致性域的高速连接:
> - **强缓存一致性域**: 一个HNF，然后多个Core。这样的结构感觉上是适合于星形或者是树形拓扑。
> - **弱缓存一致性域**: 应该就是没有HNF？自己每个核心内部处理，这个得看看
> - **无缓存一致性域**: 要不搞一个奇技淫巧的东西，类似于{{<cite xuRHTNoCReconfigurable2025>}}  
> 域间通信直接上到消息网络，所以消息网络和内存网络需要有一个专有的link，这个link也可以做成是adaptable的，做域间通信的时候可以选择节点来连接。消息网络估计就是一个固定拓扑的网络，具体拓扑还得研究研究。


{{< references >}}

