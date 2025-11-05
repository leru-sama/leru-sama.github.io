+++
date = '2025-11-05T10:08:49+08:00'
draft = false
title = 'Cache Coherence for GPU Architectures'
+++

## 概述

这篇文章{{<cite singh2013>}}挺老的，介绍的是一种GPU的缓存一致性协议。解决的问题是CPU的严格一致性在
GPU上不好用，因为GPU有成千上万个线程，导致的一致性流量会非常大，且所需存储的一致性相关查找表也需要的
memory非常大，所以GPU是没法做类似于 访问->noop->授权->修改 这样的严格一致性协议，文中一句话写得很
透彻：**Reducing the worst case storage overhead requires throttling the net work via back-pressure flow-control mechanisms when theend-point queues fill up.**这句话引用自{{<cite martin2000>}}这篇远古文章，估计是讲的基于时间戳的snoop，有时间看看。

{{<admonition type="info" title="Note">}}
结论就是，对于超级多核系统，{{<cite martin2000>}}称"Symmetric Multycore Processor",使用基于反压的流控机制，可以减少一致性流量。
{{</admonition>}}

## 主要内容
主要设计了三种缓存一致性协议：
- GPU-VI：优化的 VI一致性协议{{<cite kongetira2005>}}，VI协议为：写穿，即写操作无论hit还是miss，都将直接发给主存（或HA），由主存去invalid那些其他拥有缓存行的主机副本。
- GPU-VIni：在L2缓存单独开了一个目录的缓存，本质上就是GPU-VI
- TC-weak/strong：本文提出的协议，强的不需要软件参与，但是性能弱一点，弱的需要软件进行显式的fence操作，但性能会好一点。


### TC协议
**这个协议只能用在单片上**，因为它基于一个global的计数器。

协议中有2个时间戳：L2每条cacheline的global timestamp和L1每条cacheline的local timestamp。读请求会附带一个预测生命周期，L2将对应cacheline的时间戳设置为全局计数器+预测生命周期。多条读请求就如下图所示，将继续增加global timestamp。

{{<figure src="/cachecohence/CacheCoherenceforGPUArchitectures/TC hardware extention.png" alt="TC hardware extention.png" caption="时间戳扩展" >}}

{{<admonition type="info" title="Note">}}
这样做有一个好处，查看L2的cacheline的时间戳，小于全局计数器，就说明已经没有其他主机持有这个cacheline了，文中没有做处理，但可以可以安全地将它invalid。
{{</admonition>}}

{{<figure src="/cachecohence/CacheCoherenceforGPUArchitectures/GPU-VI vs TC.png" alt="GPU-VI vs TC.png" caption="GPU-VS vs TC协议" >}}

如下图所示，TC协议又分为TC-strong和TC-weak两种，TC-strong将阻塞L2的写入，直到对应cacheline的所有持有者都超时紫砂，**这种操作保证了严格的一致性**。TC-weak不会阻塞任何写入，一致性维护用显式的“fence”操作实现。
 
{{<admonition type="info" title="Note">}}
它的这个只能用在单芯粒上，能不能扩展到多芯粒？感觉用TC-strong会比TC-weak靠谱。
{{</admonition>}}

{{<figure src="/cachecohence/CacheCoherenceforGPUArchitectures/TC_Strong vs TC_Weak.png" alt="TC_Strong vs TC_Weak.png" caption="TC-strong/weak协议" >}}

### 生命周期的预测
其实也很简单，首先有一个全局的计数器，cacheline被分配过后记录它的时间戳和生命周期，以后访问的时候看看它似了没。这篇文章对cacheline的同一个bank使用同一生命周期。

调整策略全部依据**L2的本地观察结果**如下：
1. L2 evict了未过期的cacheline：说明这行cacheline早该似了，减少生命周期
2. L2 接收load请求，指向一个未过期的cacheline：说明有主机读了这行cacheline，然后失效了，不得已又读一次，增加生命周期
3. L2 接收store请求，指向一个未过期的cacheline：说明外面还有主机拿着cacheline，不能直接写，需要等它超时过期，减少生命周期。

全局计数器溢出时将所有的cacheline杀掉，保证不出错。

这文章没有做电路，跑跑仿真就溜了，用的是 [GPGPU-Sim 3.1.2](https://github.com/gpgpu-sim/gpgpu-sim_distribution) 来仿真 GPU，用[GEM5](https://zhuanlan.zhihu.com/p/487737252)的RUBY模型来建模内存，用[Garnet](https://github.com/microsoft/Garnet)来建模片上网络。

{{<admonition note "全局计数器怎么解决？">}}
    硬做。
{{</admonition>}}


{{<references>}}