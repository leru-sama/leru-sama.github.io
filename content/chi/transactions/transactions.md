+++
date = '2025-10-22T15:23:05+08:00'
draft = false
title = 'Transactions'
tags = ['CHI', 'Cache Coherence']
+++

直接从transaction开始看了，看点没看过的给记录下来

## 接口和信号

~~无数次都是从这儿开始，后边看不下去~~

| 通道 | RN接口 | 说明                             | SN接口 | 说明       |
|------|--------|----------------------------------|--------|------------|
| REQ  | O      | \                                | I      |  \          |
| WDAT | O      | \                                | I      |   \         |
| SRSP | O      | 用作snoop的resp以及发complete resp | 无     |  \          |
| CRSP | I      | 用作接收complete resp              | O      | 用作发响应   |
| RDAT | I      | \                                 | O      |   \         |
| SNP  | I      | 接收snoop                          | 无     |   \         |

{{< admonition type="note" >}}
snoop是HF来发的，RN只接收然后通过SRSP他回东西就OK
{{< /admonition >}}

### 各种ID相关
一个包包含的ID信息如下：

- For a **Request** packet: TgtID, SrcID, TxnID, StashNID, StashLPID, ReturnNID, ReturnTxnID, PGroupID, StashGroupID, and TagGroupID.
- For a **Response packet**: TgtID, SrcID, TxnID, DBID, PGroupID, StashGroupID, and TagGroupID.
- For a **Data packet**: TgtID, SrcID, TxnID, HomeNID, and DBID.
- For a **Snoop packet**: SrcID, TxnID, FwdNID, FwdTxnID, and StashLPID.

 TgtID、SrcID就是源和目的ID，剩余的ID如下介绍：
 - TxnID: 事务ID，类似于我们的TID，给了12位，但是只给用1024个ID，另外，retry包不用使用同一个TxnID。
 - StashNID：stash node id，暂存节点ID，payload里面有一个标志位标志stash是否有效，若有效，举一个例子：RN0向HN发送了一个带stash的写，这个StashNID指向RN1，HN会告诉RN1这个stash来了，建议RN1把这段数据给预加载，RN1自己决定要不要加载这段数据。这个操作是一个hint，可以被忽略。
 - StashLPID: 和上面差不多，CHI可以在一个RN内部划分logical processor，就是StashNID下面更细分的ID，payload有一个额外的valid位标志它有效
 - ReturnNID: 返回节点ID，只在Rquest里，对应这个请求的响应应该发到哪儿去，应用场景就是RN0向HN发读给miss了，HN向SN发读请求，这个返回的数据可以返回HN，也可以通过ReturnID指向RN0。
- ReturnTxnID: 和上面差不多，返回的事务ID
- PGroupID: persistence group id，标志一个需要持久内存的组，字段也复用为StashGroupID和TagGroupID
- DBID：Data Buffer ID，这个只存在于respond中，给SN一点管理自己内存的权限，举个例子：向SN发送一个写请求，SN收到过后分配一个DBID，并把DBID返回给发送方，发送方将这个DBID作为写数据的TxnID发送。
- HomeNID：数据对应HN的ID
- FwdNID: forward node id，用在Snoop Packet中，缩短访问路径用。举个例子：RN0向HN发一个read shared，HN发现自己没有但是RN1有，向RN1发送snoop，这个snoop里把RN0作为FwdNID，read shared的TxnID作为FwdTxnID，RN1收到snoop后直接发数据给RN0而不是HN，缩短访问路径，这个机制叫做**DCT**

{{< admonition type="note" title="DBID" >}}
基于DBID就能做out of order的写操作了！
{{< /admonition >}}

### 读ID的传输
CHI有两个机制，可以缩短数据传输的跳数：
- DCT：Direct Cache Transfer，使用FwdNID，直接在两个RN间传数据，不经过HN。
- DMT：Direct Memory Transfer，使用ReternNID，直接在SN与RN间传数据，不经过HN

#### DMT模式下的ID传输

RN和SN的直接通信，用的是RetrunNID和DBID
1. 读请求：RN -> ICN（HN），HN发现miss了（或者根本没有HN），
2. 读请求：ICN（HN）-> SN，ICN用新的一组TgtID、SrcID以及TxnID，把原来的src和tgt放在retrun id里
3. 读确认：SN -> ICN（HN），根据ICN的tgt id找到ICN（HN），返回ReadReceipt（类似于READACK）
4. 返回数据：SN -> RN，RetrunNID和ReturnTxnID找到RN，此外，DBID用来放ICN（HN）的TxnID，HomeNID用来放ICN（HN）的ID
5. CompAck：RN -> ICN（HN），根据HomeNID找到ICN（HN），根据DBID作为TxNID，返回CompAck

{{< admonition type="note" >}}
这里DBID用来存ICN的TxnID，用来给ICN来释放缓冲队列的。
{{< /admonition >}}

{{< admonition type="note" >}}
因为transaction的发起者是RN，接收者是HN，所以需要由RN向HN发送CompAck
{{< /admonition >}}

{{<figure src="/chi/transactions/ID transfer in DMT mode.jpg" caption="DMT模式下的ID传输">}}

后面有一个ID value transfer with DMT and separate Comp and Data，就是ICN有一半数据先给RN，后面和上述的一样。看看图就明白了

{{<figure src="/chi/transactions/ID transfer in DMT mode with seperated data.jpg" caption="DMT模式下分离的数据的ID值传输">}}

#### DCT模式下的ID传输

RN和RN的直接通信，用的是FwdNID和FwdTxnID，看图就明白了，这里DBID仍然是用来存ICN的TxnID，用来给ICN来释放缓冲队列的。

{{<figure src="/chi/transactions/ID transfer in DCT mode.jpg" caption="DCT模式下的ID传输">}}

#### 其他传输
其他传输源和目的换一换就行。



## Ordering
这一小节有点细节，具体实现的时候再查阅

有一个前提：CHI提了一种Multi copy atomicity，操作的原子性。
定义的对同一地址的所有写操作都是串行的，所有观察者也都是串行观察到的；读必须在**写数据被所有Requester**观察到之后才返回数据。

{{<admonition type="note" >}}
这种就是典型的以HA为中心的思想，虽然以内存为中心的缓存一致我还不知道是啥。。。

由于所有的写操作都是传给HA，所以可以在HA出进行排序，而先完成所有的提醒后，再返回读数据也是HA管理并完成的。
{{< /admonition >}}

### CompAck


在spec的p131，有一个表，列出来了哪些Request的CompAck是requre、optional或者NO的

CHI使用CompACK保证事务的原子性，一个事务由Requester发起request开始，由Requester返回CompAck结束。CompAck传输完成后（read会发一个CompAck，write就用write data作为CompAck），HN会snoop对应事务的地址空间，在Request和CompAck之间，不会snoop对应未完成的这个事务（由HN保证）

{{<admonition type="note">}}
对于读操作，除了ReadOnce和ReadNoSnoop，操作都为：读请求->乱七八糟的读响应->CompAck->snoop,这个snoop有何意义？比如ReadUnique或者ReadShare，要改变缓存行状态的，确实应该snoop。

对于写操作，直接就是写请求->一堆确认->写数据->snoop，这个确实应该snoop。
{{< /admonition >}}

## Address 

支持物理地址44\~52b，支持虚拟地址49\~53b

memory定义了4种属性：Normal、 Device、Cacheable and Allocate。Normal就不写了。
这个属性是定义在**request**里面的，也就是说这个属性只对本次传输负责。

一整个事务里面的包的memattr（就是上边定义的属性）原则上来说是一样的，但如果知道下游节点是normal的话，可以由HN改成Normal（也可以不改）。

### Device
对于有side-effects的设备，必须定义为Device类型，人话就是对于加速器的寄存器堆这种，写进去了还会发生其他的事的东西。

对它的定义都是保证这个读写操作和预期的读写操作对应，比如不能在任何地方缓存device的数据啦、不能prefetch啦什么的。

对于Device的读写操作只能是ReadNoSnp以及WriteNoSnp的变体。

### Cacheable
这里有一句定义很有意思：如果事务被assert了cacheable，这个事务必须被cache追踪，否则必须要到**final dest**，也就是不能进cache。

### Allocate
是一个hint，标记了allocate就可选的把这个东西cache了或者不cache。


## Retry

CHI有一个PCrd，也就是pacage credit，用来做协议级的流控，如下图，HN可以通过RetryAck让Requester retry。Requester接到RetryAck过后，需等待一个PCrdGrant，给你分配了空间以后才能继续retry，如下图：
{{<figure src="/chi/transactions/transaction retry flow.jpg" caption="Retry">}}

**完结撒花！ 没这么难嘛，也是草草看完了**