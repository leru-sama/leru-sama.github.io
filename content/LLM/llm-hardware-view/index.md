+++
date = '2026-08-31T10:40:00+08:00'
draft = false
title = '大语言模型到底是什么：从张量形状到晶体管的完整推导'
description = '面向无 LLM 背景的硬件工程师。全文以 Llama-3-8B + NVIDIA H100 为贯穿算例，所有数字均给出推导过程与出处。'
categories = ['LLM']
tags = ['LLM', '硬件', 'Transformer', '推理优化', 'Roofline']
math = true
ShowToc = true
TocOpen = false
+++

## 读前的约定

这份报告不假设你懂任何机器学习，但假设你懂数字电路、存储层次、带宽与流水。我的目标不是让你"了解 AI"，而是让你**能自己拿一张芯片的手册，算出某个模型在上面能跑多快、瓶颈在哪、为什么**。

{{% admonition note "三条贯穿全文的原则" %}}
<strong>1. 一切都是计数。</strong>LLM 推理没有玄学，全部结论都可以从"搬了多少字节、做了多少次乘加"推出来。本报告所有数字你都能自己复算。

<strong>2. 区分两个瓶颈。</strong>任何一个算子，要么是被算力卡住（compute-bound），要么是被带宽卡住（memory-bound）。判据只有一个：算术强度与机器平衡点的比较（§3.5）。

<strong>3. 警惕宣传数字。</strong>厂商给的峰值算力几乎没有意义——真实利用率在 LLM 推理中常常低于 1%。本报告会专门指出几个常见的数据陷阱。
{{% /admonition %}}

### 0.1 符号表

| 符号 | 含义 | Llama-3-8B 取值 |
| --- | --- | --- |
| `N` | 模型参数量 | 8.03 × 10⁹ |
| `L` | Transformer 层数 | 32 |
| `d` | 隐藏层维度（残差流宽度） | 4096 |
| `h` | 查询头数 | 32 |
| <code>h<sub>kv</sub></code> | 键值头数（GQA） | 8 |
| <code>d<sub>h</sub></code> | 每个头的维度，<code>d = h · d<sub>h</sub></code> | 128 |
| <code>d<sub>ff</sub></code> | FFN 中间层维度 | 14336 |
| `V` | 词表大小 | 128256 |
| `s` | 上下文长度（已生成的 token 数） | 变量 |
| `B` | 批大小（同时处理的序列数） | 变量 |
| `p` | 每个参数占用的字节数 | 2（bf16） |
| <code>P<sub>peak</sub></code> | 峰值算力（FLOP/s） | 989.4 T（H100 bf16） |
| `BW` | 显存带宽（byte/s） | 3.35 T |
| `I` | 算术强度（FLOP/byte） | 导出 |
| <code>I<sub>m</sub></code> | 机器平衡点 = <code>P<sub>peak</sub> / BW</code> | 295 |

### 0.2 单位约定（务必先看）

- **FLOP**（大写）= 浮点运算次数，是*量*；**FLOPS**（小写 s）= 每秒浮点运算次数，是*速率*。二者差一个时间量纲，混用是文献中最常见的低级错误。
- <strong>1 FLOP = 1 次乘加（MAC）的 2 倍。</strong>一次 `a = a + b·c` 记 2 FLOP。本报告统一采用此约定（与 NVIDIA、MLPerf 一致，但部分论文按 1 MAC = 1 FLOP 计，看到数字差 2 倍时先检查这个）。
- **byte 一律按 10 进制**：1 GB = 10⁹ byte，1 TB/s = 10¹² byte/s。厂商带宽数字均为 10 进制，混用 2³⁰ 会引入 7% 误差。
- **bf16**（brain float 16）：1 符号 + 8 指数 + 7 尾数。指数位与 fp32 相同，所以从 fp32 转换只需截断尾数，硬件代价极低。

{{% admonition danger "数据陷阱 #1：NVIDIA 官方规格表里的星号" %}}
NVIDIA H100 的公开规格表列出的常常是<strong>结构化稀疏（2:1 sparsity）</strong>下的数字，用小星号标注。例如常见的一行"BF16 Tensor Core: 1,979 TFLOPS*"，去掉稀疏是 **989.4 TFLOPS**。而 **494.7 是 TF32 的稠密值，不是 BF16**——这两个数极易互换。

本报告**一律使用稠密（dense）算力**，因为结构化稀疏需要 2:1 的权重剪枝模式，生产环境中的开源模型基本不使用。H100 SXM 关键值：TF32 494.7 T、BF16/FP16 989.4 T、FP8 1978.9 T（稠密）。[[1]](#r1)
{{% /admonition %}}

## LLM 的形式化定义

### 1.1 它就是一个函数，只不过参数极多

抛开所有叙事，一个大语言模型是一个确定的参数化函数：

{{< eq >}}
f_\theta : \{0,1,\dots,V-1\}^{s} \;\longrightarrow\; \mathbb{R}^{V}
{{< /eq >}}

输入是 `s` 个整数（token id，每个取值范围 `0…V−1`），输出是一个长度 `V` 的实数向量——词表上未归一化的对数概率（logits）。`θ` 就是"模型权重"，一组浮点张量的集合。

这个视角立刻带来三个硬件上的事实：

- <strong>它是纯函数，无状态。</strong>所谓"记忆"全部来自输入的 `s` 个 token（上下文）。因此每次生成，理论上都要把整个上下文重新算一遍——这正是 KV cache 存在的理由（§4.3）。
- <strong>它是确定性计算。</strong>同样的输入、同样的权重，逐比特可复现。不存在"模糊匹配"或"检索"，没有查表（除了最开头的一次 embedding 查表）。
- <strong>它的算力需求完全可静态分析。</strong>计算图是固定的，没有数据相关的分支。所以 §4 的那些数字是精确的，不是估算。

### 1.2 分词：文本 → 整数

计算机无法直接处理字符，第一步是把字符串切成 token。主流方案是 <strong>BPE（Byte-Pair Encoding）</strong>：从字节开始，反复合并语料中出现频率最高的相邻符号对，直到词表达到预设大小。

对硬件而言，分词部分几乎无关紧要（在 CPU 上跑，微秒级），但有三个后果必须知道：

| 后果 | 说明 | 影响 |
| --- | --- | --- |
| 序列长度不可预测 | 同一段文本，中英文 token 数可差 3–5 倍 | 显存占用与延迟按 token 计，不按字符计 |
| 词表很大 | Llama-3 用 128256，早期模型常用 32000 | LM Head 矩阵为 `V × d`，是显存大户 |
| 数字/代码切分很碎 | 一个数字可能被切成多个 token | 算术能力差的一个结构性原因 |

{{% admonition note "量级感" %}}
Llama-3 的分词器下，1 个 token ≈ 0.75 个英文单词 ≈ 1.5–2 个汉字。所以"上下文 8192"大致相当于 6000 英文词或 12000–16000 汉字，而不是 8192 个汉字。
{{% /admonition %}}

### 1.3 自回归分解：为什么要一个一个地生成

模型输出的是一个*条件分布*——给定前 `t−1` 个 token，下一个 token 的概率分布。语言模型的概率建模通过链式法则精确分解：

{{< eq >}}
P(x_1,\dots,x_T) \;=\; \prod_{t=1}^{T} P\big(x_t \,\big|\, x_1,\dots,x_{t-1}\big)
{{< /eq >}}

这个分解是**恒等式**，不是近似。它的意义在于：把"给整段话打分"这件事，精确拆成 `T` 次"预测下一个 token"。而每一次预测，就是一次 <code>f<sub>θ</sub></code> 的前向传播。

生成（解码）时，流程是一个严格的串行循环：

```
# 伪代码：自回归生成
context = tokenize(prompt)          # s 个 token
for t in range(s, s + n_out):
    logits = f_theta(context)       # 一次完整前向，输入长度 = t
    x_next = sample(logits[-1])     # 只取最后一个位置的分布
    context.append(x_next)          # 反馈，产生串行依赖
```

{{% admonition tip "这是理解一切硬件优化的起点" %}}
循环体里 `f_theta(context)` 每次都重新计算了前 `t−1` 个 token 的全部中间结果，但只有**最后一个位置**的输出被使用。第 1 次到第 `t−1` 次的计算结果中，绝大部分是可以复用的——**这就是 KV cache**。它把 O(T²) 的重复计算降到 O(T)，代价是需要 O(T) 的显存来存那些中间结果。
{{% /admonition %}}

### 1.4 训练与推理的算力关系

参数量 `N` 与算力需求的关系极其简洁：

- <strong>前向一次（推理 1 个 token）：</strong>每个参数参与 1 次乘加 → `2N` FLOP。
- <strong>训练一步（前向 + 反向）：</strong>反向传播约为前向的 2 倍 → `6N` FLOP/token。

{{< eq >}}
C_{\text{train}} \;\approx\; 6 N D \qquad C_{\text{infer}} \;\approx\; 2 N \cdot (\text{token 数})
{{< /eq >}}

其中 `D` 是训练语料的 token 总数。这个 `6ND` 是估算训练成本的金标准公式（推导见 §6.1）。

对 Llama-3-8B：`N = 8.03×10⁹`，训练 `D = 15×10¹²` token（Meta 公开数据），则

{{< eq >}}
C_{\text{train}} = 6 \times 8.03\times10^{9} \times 15\times10^{12} \approx 7.2\times10^{23}\ \text{FLOP}
{{< /eq >}}

在 H100 上以 40% MFU 运行：`7.2×10²³ / (9.894×10¹⁴ × 0.4) ≈ 1.8×10⁶` 秒 ≈ **21 天/卡**，即约 **1.6 万卡·天**。Meta 实际用了约 1.6 万张 H100 跑数月——量级完全吻合。

### 1.5 "模型文件"到底是什么

不存在"AI 程序"这种东西。所谓模型，就是一个**张量字典**（safetensors / pickle 格式），存着一批多维数组。加载模型 = 把数组读进显存。下面把 Llama-3-8B 全部拆开，这是后面所有计算的账本。

| 张量 | 形状 | 每层参数量 | ×32 层 | 说明 |
| --- | --- | --- | --- | --- |
| <code>W<sub>q</sub></code> | [4096, 4096] | 16.78 M | 536.9 M | Q 投影 |
| <code>W<sub>k</sub></code> | [1024, 4096] | 4.19 M | 134.2 M | K 投影（8 头 × 128） |
| <code>W<sub>v</sub></code> | [1024, 4096] | 4.19 M | 134.2 M | V 投影 |
| <code>W<sub>o</sub></code> | [4096, 4096] | 16.78 M | 536.9 M | 输出投影 |
| <code>W<sub>gate</sub></code> | [14336, 4096] | 58.72 M | 1 879 M | SwiGLU 门控 |
| <code>W<sub>up</sub></code> | [14336, 4096] | 58.72 M | 1 879 M | 升维 |
| <code>W<sub>down</sub></code> | [4096, 14336] | 58.72 M | 1 879 M | 降维 |
| `norm 权重` | [4096] × 2 | 0.008 M | 0.26 M | 可忽略 |
| <strong>小计（每层）</strong> |  | **218.1 M** | **6 979 M** |  |
| `embed` | [128256, 4096] | 525.3 M | — | 输入嵌入表 |
| `lm_head` | [4096, 128256] | 525.3 M | — | 输出投影（未绑定） |
| **合计** |  | **8 030 M** |  | **8.03 B** |

{{% admonition note "立刻可以算出的三件事" %}}
<strong>显存：</strong>`8.03×10⁹ × 2 B = 16.06 GB`。一张 80 GB 的 H100 装得下，还剩 64 GB 给 KV cache 和激活。

<strong>每 token 算力：</strong>`2N = 16.06 GFLOP`。

<strong>每 token 访存下限：</strong>每个参数至少要被读一次 → `16.06 GB`（batch=1 时）。

注意最后一条：**16 GB 的数据换 16 GFLOP 的计算**。把这个比值和芯片的 <code>P<sub>peak</sub>/BW</code> 对比，就得到了全报告最重要的那个数。我们到 §4.4 再算。
{{% /admonition %}}

## Transformer 的张量级结构

这一章把模型拆到"每个算子的输入形状、输出形状、FLOPs、访存量"这个粒度。这是做硬件分析唯一有用的粒度——停留在"注意力机制"这种概念层面算不出任何东西。

### 2.1 一层的整体结构：残差流视角

不要按"输入→输出"的直线去理解 Transformer，要按<strong>残差流（residual stream）</strong>去理解：有一条宽度恒为 `d` 的主干通道贯穿全部 `L` 层，每层的注意力和 FFN 都是*旁路*，它们读取主干的内容，算出结果后**加回**主干。

{{< figure src="fig1.svg" alt="残差流视角下的单层 Transformer" caption="图 1 — 残差流视角。主干宽度恒为 d=4096，注意力与 FFN 是两个并联的旁路，输出以加法（⊕）写回。这个结构决定了：注意力负责'跨 token 通信'，FFN 负责'单 token 内的特征变换'，两者的硬件行为完全不同。" >}}

{{% admonition tip "为什么这个视角重要" %}}
残差流意味着**没有信息瓶颈**：第 1 层的输出可以原封不动地传到第 32 层。也意味着每层的输出张量形状与输入完全相同——`[B, s, d]`。因此"跑一层"的数据量是可预测的，`L` 层就是简单乘以 32，不存在复杂的跨层依赖。

对硬件的直接后果：**推理必须串行通过 32 层**，因为第 `L+1` 层依赖第 `L` 层的输出。这就是流水线并行（pipeline parallelism）能被用于推理的原因，也是单用户延迟无法靠堆算力降低的原因——你只能让每一层更快，不能并行跑 32 层。
{{% /admonition %}}

### 2.2 RMSNorm：几乎不计算的算子

前置归一化（pre-norm）使用 RMSNorm 而非 LayerNorm：[[2]](#r2)

{{< eq >}}
\mathrm{RMSNorm}(\mathbf{x}) = \frac{\mathbf{x}}{\sqrt{\dfrac{1}{d}\sum_{i=1}^{d} x_i^2 + \epsilon}} \odot \mathbf{g}, \qquad \mathbf{g} \in \mathbb{R}^{d}
{{< /eq >}}

没有减均值、没有加偏置，比 LayerNorm 少一次归约。它的硬件特征：

- <strong>FLOPs ≈ 0（相对而言）。</strong>每个元素约 5 次浮点操作（平方、求和、除法、开方、乘 g），共 `5d ≈ 2×10⁴` FLOP。相比同层的 470 MFLOP，是 <strong>0.004%</strong>。
- <strong>访存 = 读 d 个元素 + 读 d 个权重 + 写回 d 个元素。</strong>对 d=4096、bf16：8 KB + 8 KB + 8 KB。
- <strong>但它是延迟杀手。</strong>归约（reduction）需要跨整个向量求平方和，是一次全局同步。在 GPU 上需要 block 内规约 + 跨 block 两趟 kernel，或者用 warp shuffle。单次 kernel 启动开销（约 3–5 µs）可能比计算本身还长。

{{% admonition warning "研究要点：element-wise 算子的真实成本" %}}
像 RMSNorm、RoPE、残差加法、激活函数这类算子，FLOPs 都近乎为零，但在真实推理中可能占用 **10–30% 的墙钟时间**。原因有二：(1) 它们各自需要一次 kernel 启动与一次 HBM 往返，而小 kernel 无法打满带宽；(2) 它们打断了大 GEMM 的执行流。

这就是为什么"算子融合（kernel fusion）"是推理引擎的核心工程——把所有逐元素操作合并进相邻 GEMM 的 epilogue，让它们不产生额外的 HBM 往返。判断一个推理框架是否成熟，看它融合了多少算子比看它的峰值算力更有意义。
{{% /admonition %}}

### 2.3 注意力：完整推导与 GQA

设输入 `X ∈ ℝ^{s×d}`（`s` 个 token，每个 `d` 维）。注意力的计算过程：

{{< eq >}}
\begin{aligned}
Q &= X W_q^{\top},\quad K = X W_k^{\top},\quad V = X W_v^{\top} \\
\text{Attn}(Q,K,V) &= \mathrm{softmax}\!\left(\frac{QK^{\top}}{\sqrt{d_h}}\right) V \\
\mathrm{Out} &= \mathrm{Attn}\; W_o^{\top}
\end{aligned}
{{< /eq >}}

形状追踪（这是必须练熟的基本功）：

| 步骤 | 算子 | 输入形状 | 权重形状 | 输出形状 |
| --- | --- | --- | --- | --- |
| Q 投影 | `X W_qᵀ` | [s, 4096] | [4096, 4096] | [s, 4096] |
| K 投影 | `X W_kᵀ` | [s, 4096] | [1024, 4096] | [s, 1024] |
| V 投影 | `X W_vᵀ` | [s, 4096] | [1024, 4096] | [s, 1024] |
| 分头 | reshape | [s, 4096] | — | [s, 32, 128] |
| 打分 | `Q Kᵀ` | [s,32,128] × [s,8,128] | — | [32, s, s] |
| 加权和 | `P V` | [32,s,s] × [s,8,128] | — | [s, 32, 128] |
| 合并 + 输出 | `· W_oᵀ` | [s, 4096] | [4096, 4096] | [s, 4096] |

#### GQA：为什么 K、V 的头数只有 8 个

标准 MHA 中 `h = h_kv = 32`。Llama-3 采用 <strong>GQA（Grouped-Query Attention）</strong>：`h_kv = 8`，每 4 个 Q 头共享 1 组 K/V。[[3]](#r3)

这个改动的动机**不是省计算**（FLOPs 只降了不到 20%），而是**省 KV cache 的带宽**：

{{< eq >}}
\text{KV cache 字节/token} = 2 \cdot L \cdot h_{kv} \cdot d_h \cdot p
{{< /eq >}}

- MHA（`h_kv=32`）：`2 × 32 × 32 × 128 × 2 = 524288 B = 512 KB/token`
- GQA（`h_kv=8`）：`2 × 32 × 8 × 128 × 2 = 131072 B = 128 KB/token`

在 4096 上下文下，一个请求的 KV cache 从 2.1 GB 降到 **0.52 GB**——直接决定了单卡能同时服务多少并发。这是纯硬件驱动的架构选择。

#### Attention 的 FLOPs 推导

对单个 token 的解码步（上下文长度 `s`）：

- 打分 `QKᵀ`：每个 Q 头对 `s` 个 K 做长度 `d_h` 的点积 → `h · s · d_h` 次 MAC = `2 h s d_h` FLOP
- 加权和 `PV`：同样形状 → `2 h s d_h` FLOP

{{< eq >}}
F_{\text{attn}} = 4\,h\,d_h\,s = 4 \times 32 \times 128 \times s = 16384\,s \ \text{FLOP}
{{< /eq >}}

在 `s = 2048` 时 = 33.6 MFLOP，仅占全层 470 MFLOP 的 <strong>7%</strong>。注意 FLOPs 随 `s` *线性*增长，而 KV cache 的*访存量*也随 `s` 线性增长——两者增长率相同，这个观察在 §4.5 会推出一个重要结论。

#### Softmax 是这里的异类

softmax 是整层唯一不符合"矩阵乘"形态的算子：

{{< eq >}}
p_i = \frac{e^{z_i}}{\sum_j e^{z_j}}
{{< /eq >}}

硬件上的麻烦：

- **需要两趟扫描**：先求 max（数值稳定），再求 exp 与 sum，最后归一化。三趟遍历或者用 online softmax 合并成两趟。
- **exp 是指令级的特殊单元**：Tensor Core 不干这个，必须走 SFU（Special Function Unit）或 MUFU.EX2，吞吐远低于 MAC。
- **它是数据相关的**：分母依赖全部输入，无法像 GEMM 那样自由分块。

FlashAttention 的核心贡献就是解决了 softmax 的分块问题（online softmax），见 §5.2。

### 2.4 RoPE：位置编码的硬件代价

Llama-3 使用旋转位置编码（RoPE）。它不给向量*加*位置信息，而是对 Q/K 向量的每一对相邻分量做一次二维旋转：[[4]](#r4)

{{< eq >}}
\begin{pmatrix} q'_{2i} \\ q'_{2i+1} \end{pmatrix} = \begin{pmatrix} \cos m\theta_i & -\sin m\theta_i \\ \sin m\theta_i & \cos m\theta_i \end{pmatrix} \begin{pmatrix} q_{2i} \\ q_{2i+1} \end{pmatrix}, \quad \theta_i = 10000^{-2i/d_h}
{{< /eq >}}

其中 `m` 是 token 的位置索引。它的硬件特征：

- 纯逐元素：4 次乘 + 2 次加，FLOPs 相对全层可忽略（但同样有 kernel 开销问题）。
- 只作用于 Q 和 K，不作用于 V。
- 三角函数通常预计算成查表，或利用旋转的递推性质 `R(m+n) = R(m)R(n)` 复用。
- **关键性质：它是相对位置编码**——`q_mᵀ k_n` 只依赖于 `m−n`。这是长上下文外推有效的结构性原因。

### 2.5 SwiGLU FFN：参数与访存的大头

Llama-3 用 SwiGLU 替代了原始的 ReLU FFN：[[5]](#r5)

{{< eq >}}
\mathrm{FFN}(\mathbf{x}) = \Big( \mathrm{swish}(\mathbf{x} W_{gate}^{\top}) \odot \mathbf{x} W_{up}^{\top} \Big) W_{down}^{\top}, \quad \mathrm{swish}(z) = z \cdot \sigma(z)
{{< /eq >}}

注意有三个矩阵而不是两个，且中间维度 `d_ff = 14336 ≈ 3.5 d`（原始 Transformer 是 4d，但因为多了一个门控矩阵，等效参数量与 4d 的两矩阵版本相当）。

<strong>FLOPs：</strong>

{{< eq >}}
F_{\text{ffn}} = 2 \cdot d \cdot d_{ff} \cdot 3 = 2 \times 4096 \times 14336 \times 3 = 3.52 \times 10^{8} \ \text{FLOP} = 352\ \text{MFLOP}
{{< /eq >}}

<strong>访存：</strong>`3 × 4096 × 14336 × 2 B = 352 MB`（batch=1 时每个权重只读一次）

{{% admonition note "FFN 的访存/算力比是恒定的" %}}
注意这个巧合：FFN 的 FLOPs（352 MFLOP）与访存量（352 MB）在数值上相等，因为 `FLOPs = 2 × 参数量`，而 `字节数 = 2 × 参数量`（bf16）。<strong>这意味着 batch=1 时，FFN 的算术强度恒为 1 FLOP/byte，与矩阵形状完全无关。</strong>

这个结论可以推广：任何 GEMV（矩阵×向量）操作的算术强度都是 `2/p`（`p` = 每参数字节数）。bf16 下就是 1.0，fp8 下是 2.0，int4 下是 4.0。记住这个数，它能让你在 5 秒内判断任何 LLM 算子的瓶颈性质。
{{% /admonition %}}

### 2.6 完整单层：算子清单

### 2.7 一层的账本（decode 阶段，B=1，s=2048）

| # | 算子 | 参数量 | FLOPs | HBM 访存 | 形态 | 瓶颈 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | RMSNorm | 4 K | ~2×10⁴ | 24 KB | 逐元素 | 延迟 |
| 2 | QKV 投影 | 25.2 M | 50.3 M | 50.3 MB | GEMV | 带宽 |
| 3 | RoPE | 0 | ~10⁵ | 16 KB | 逐元素 | 延迟 |
| 4 | KV 写入 | — | 0 | 4 KB（写） | 访存 | 带宽 |
| 5 | Attention 打分+加权 | 0 | 33.6 M | 8.4 MB（读 KV） | GEMV+softmax | 带宽 |
| 6 | O 投影 | 16.8 M | 33.6 M | 33.6 MB | GEMV | 带宽 |
| 7 | 残差加 | 0 | 4 K | 24 KB | 逐元素 | 延迟 |
| 8 | RMSNorm | 4 K | ~2×10⁴ | 24 KB | 逐元素 | 延迟 |
| 9 | FFN（3 个 GEMV） | 176.2 M | 352.3 M | 352.3 MB | GEMV | 带宽 |
| 10 | 残差加 | 0 | 4 K | 24 KB | 逐元素 | 延迟 |
|  | **合计** | **218.1 M** | **469.6 M** | **≈ 445 MB** |  |  |

{{% admonition tip "读出这张表的三个结论" %}}
<strong>(a) FFN 占 79% 的访存、75% 的 FLOPs。</strong>任何不针对 FFN 的优化都是抓小放大。这也解释了为什么 MoE（混合专家）只在 FFN 上做稀疏化——那里是唯一值得动的地方。

<strong>(b) 注意力只占 7% 的 FLOPs，但占 2% 的访存 + 全部的动态显存。</strong>它的麻烦不在算力，在 KV cache 的容量与带宽。

<strong>(c) 逐元素算子的访存合计不到 0.1 MB，但它们会产生 7 次额外的 kernel 启动。</strong>在 batch=1 且每层只有 133 µs 的预算下，每次 3 µs 的启动开销 = 21 µs = **16% 的时间**。这就是为什么 CUDA Graph 和算子融合对低 batch 推理是必需的，而不是锦上添花。
{{% /admonition %}}

### 2.8 LM Head 与采样

最后一层的输出 `[1, 4096]` 乘以 `W_{lmhead} ∈ ℝ^{V×d}`：

{{< eq >}}
\text{logits} = \mathbf{x}\, W_{lmhead}^{\top} \in \mathbb{R}^{128256}, \qquad F = 2 d V = 1.05\ \text{GFLOP}
{{< /eq >}}

这个矩阵是 **525 M 参数 = 1.05 GB**，占模型总权重的 6.5%。它有几个特殊之处：

- **它是纯 GEMV 且完全无法被 batch 之外的手段优化**——每个 token 都必须完整读一遍这 1.05 GB。
- 占总访存的 `1.05 / 15.3 = 6.9%`。不大，但在优化到极致时不能忽略（有些框架会把它单独量化到 int8 而保持主体为 bf16）。
- **采样阶段需要读全部 128256 个 logits**，`top-p` 采样还需要排序，这在 GPU 上效率不高，有时会挪到 CPU 做。

## 硬件：执行单元与存储层次

### 3.1 GPU 的物理组织

以 H100 SXM5 为例，自顶向下的层级：[[1]](#r1)

| 层级 | 数量 | 每单元内容 | 合计 |
| --- | --- | --- | --- |
| GPC（图形处理簇） | 8 | 9 个 TPC | 66 TPC |
| TPC（纹理处理簇） | 66 | 2 个 SM | 132 SM |
| SM（流多处理器） | 132 | 128 个 FP32 核 + 4 个 Tensor Core | 16 896 核 / 528 TC |
| L2 Cache | 1 | 50 MB，切分为两半 | 50 MB |
| HBM3 | 5 堆栈 | 10 个 512-bit 控制器 | 80 GB @ 3.35 TB/s |

关键数字自检（建议自己算一遍，这是验证你是否理解规格表的标准练习）：

{{< eq >}}
P_{\text{peak}} = 132\ \text{SM} \times 4096\ \frac{\text{FLOP}}{\text{clk·SM}} \times 1.83\ \text{GHz} = 9.89 \times 10^{14}\ \text{FLOP/s} = 989\ \text{TFLOPS}
{{< /eq >}}

其中 4096 FLOP/clk/SM 是 Hopper SM 的 BF16 吞吐（= 2048 MAC/clk/SM，每 SM 4 个 Tensor Core，每个 512 MAC/clk）。这个数字能与官方 989.4 TFLOPS 对上，说明你没理解错。

{{% admonition note "为什么是 132 而不是 144" %}}
完整的 GH100 芯片有 144 个 SM（8 GPC × 9 TPC × 2 SM），但 H100 产品只启用 132 个。这是**良率与产品分档**的结果，不是架构限制。H100 PCIe 版只有 114 个 SM。看论文时如果看到"GH100 144 SM"与"H100 132 SM"打架，原因就在这里。
{{% /admonition %}}

### 3.2 Tensor Core 与脉动阵列（RTL 视角）

一个 Tensor Core 不是"一个很快的乘法器"，它是一个**小型的二维乘加阵列**。理解它的最好方式是从脉动阵列（systolic array）出发——Google TPU 的 MXU 就是 128×128 的纯脉动阵列，而 NVIDIA 的实现是经过封装的变体。

{{< figure src="fig2.svg" alt="4×4 脉动阵列的数据流" caption="图 2 — 4×4 脉动阵列（真实 Tensor Core / TPU MXU 是 16×16 到 128×128 的同构放大）。激活 A 沿行向右流，权重 B 沿列向下流，部分和向下累积。数据只在物理相邻的 PE 之间传递，这是它在同等工艺下能实现极高算力密度的根本原因。" >}}

{{% admonition tip "从 RTL 视角看，脉动阵列的本质是一次性的权衡" %}}
<strong>它买到了什么：</strong>把"取操作数"的能耗和延迟降到最低。一次 HBM 读取的数据被 N 个 PE 复用 N 次。数据通路上只有相邻寄存器，没有 N² 扇出的广播网络，没有复杂的旁路（bypass）逻辑，布线拥塞低 → 面积小、频率高、功耗低。

<strong>它付出了什么：</strong>(1) <strong>填充与排空延迟（latency）</strong>——2N−1 个周期才能填满 N×N 阵列，小矩阵进去会浪费大半；(2) **灵活性为零**——它只能做稠密矩阵乘，任何稀疏、分支、逐元素操作都要绕开它走通用 ALU；(3) **利用率依赖形状**——矩阵维度必须是阵列尺寸的整数倍才好，否则需要 padding。

第 (3) 点直接解释了 §2.5 的现象：GEMV（`M=1`）在脉动阵列上的利用率是 `1/16 ≈ 6%` 量级。但即使如此，**GEMV 在 GPU 上依然是被带宽而非算力限制的**——因为 TLB 和 HBM 才是真正的天花板，阵列根本喂不饱。
{{% /admonition %}}

### 3.3 存储层次：完整参数表

| 层级 | 容量 | 带宽 | 延迟 | 管理者 | 存什么 |
| --- | --- | --- | --- | --- | --- |
| 主机 DDR5 | ~2 TB | ~200 GB/s | ~100 ns | OS | 请求队列、offload 的 KV |
| PCIe Gen5 ×16 | — | 64 GB/s / 向 | ~2 µs | 驱动 | 权重加载、token 回传 |
| **HBM3** | 80 GB | **3.35 TB/s** | ~400 ns | 显式（kernel） | 权重、KV cache、激活 |
| L2 Cache | 50 MB | ~10 TB/s | ~200 ns | 硬件自动 | 权重 tile、激活 |
| SMEM / L1 | 256 KB / SM | ~20 TB/s | ~30 ns | 显式（CUDA） | GEMM 分块暂存 |
| 寄存器文件 | 256 KB / SM | 最高 | ~1 周期 | 编译器 | 操作数、累加器 |

{{% admonition warning "数据陷阱 #2：H100 的共享内存到底多大" %}}
常见说法有 228 KB 和 256 KB 两个，都是对的：**每个 SM 的 L1/共享内存统一容量为 256 KB**，但**单个 thread block 最多只能申请 227 KB**（约 228 KB），且需要 `cudaFuncSetAttribute` 动态申请。默认静态上限只有 48 KB。写 kernel 时踩这个坑很常见。
{{% /admonition %}}

#### 容量对比：把模型放进哪一层

| 放进去的东西 | Llama-3-8B 大小 | 能放进 L2 (50 MB) 吗 |
| --- | --- | --- |
| 单个权重矩阵（FFN gate） | 117 MB | 否，必须切块 |
| 单层全部权重 | 436 MB | 否 |
| 全部模型权重 | 16.06 GB | 否，差 320 倍 |
| 单个 GEMM tile（128×128 bf16） | 32 KB | 是 |
| 一个 token 的 KV cache（单层） | 4 KB | 是 |

{{% admonition note "这张表说明了 GPU 运行 LLM 的基本节奏" %}}
模型永远放不进片上，所以**每个推理步都必须把全部权重从 HBM 流过一遍**。L2 的作用不是"缓存模型"，而是"让同一个 tile 在被多个 SM / 多个 batch 行使用时不必重复访问 HBM"。在 batch=1 的 decode 中，L2 几乎无效（每个权重块只用一次，无时间局部性）；在 batch=256 时，L2 能把权重读放大降低一个数量级。
{{% /admonition %}}

### 3.4 一个 GEMM 是怎么被切块执行的

Tensor Core 一次只能处理一小块（例如 128×128×64）。所以任何大矩阵乘都要先切成 tile 再喂进去。以 FFN 的 gate 投影为例，输入 `X ∈ ℝ^{B×4096}`，权重 `W ∈ ℝ^{4096×14336}`，输出 tile 取 128×128：

{{< eq >}}
\underbrace{X_{[128\times128]}}_{\text{tile A}} \times \underbrace{W_{[128\times128]}}_{\text{tile B}} \Rightarrow \underbrace{Y_{[128\times128]}}_{\text{累加 32 个 k-step}}
{{< /eq >}}

归约维度 K=4096 需要 32 个 k-step，每一步从 SMEM 取两个 32 KB 的 tile。输出共 `(B/128) × (14336/128)` 个 tile。

关键量是**算术强度随形状的变化**。对一般的 GEMM `[M,K] × [K,N]`（`p` = 每元素字节）：

{{< eq >}}
I = \frac{2MNK}{p\,(MK + KN + MN)} \;\xrightarrow[K \gg M,N]{}\; \frac{2MN}{p\,(M+N)}
{{< /eq >}}

代入 FFN gate（`M = B`，`N = 14336`，`p = 2`）：

| batch B | 算术强度 I | 与机器平衡点 295 比较 | 瓶颈 |
| --- | --- | --- | --- |
| 1 | 1.00 | 1 / 295 | 带宽 |
| 32 | 63.7 | 1 / 4.6 | 带宽 |
| 128 | 126.9 | 1 / 2.3 | 带宽 |
| 256 | 251.5 | 1 / 1.17 | 带宽（临界） |
| 300 | 293.9 | ≈ 1 | 平衡点 |
| 512 | 494.3 | 1.7 × | 算力 |

{{% admonition note "读出这个表的正确方式" %}}
单个 GEMM 在 `B ≈ 300` 时达到平衡。这看上去是个好消息——只要 batch 开到 300 就解决了。但这只是**单个 GEMM**。整层还有 KV cache 的读取，它随 batch 线性增长且完全无法复用（§4.5 会证明它会把平衡点在长上下文下彻底推走）。

此外 `B=300` 时 KV cache 需要 `300 × 0.268 GB = 80 GB`，已经超出 80 GB 显存。<strong>容量约束会先于算力约束到达。</strong>
{{% /admonition %}}

### 3.5 Roofline 模型：唯一的判据

Roofline 模型给出一个算子在给定硬件上的性能上界：[[6]](#r6)

{{< eq >}}
P(I) \;=\; \min\big(\,P_{\text{peak}},\; BW \times I \,\big), \qquad I = \frac{\text{FLOPs}}{\text{bytes}}
{{< /eq >}}

两条屋脊线交于<strong>机器平衡点（ridge point）</strong>：

{{< eq >}}
I_m = \frac{P_{\text{peak}}}{BW} = \frac{9.894\times10^{14}}{3.35\times10^{12}} = 295.3\ \text{FLOP/byte}
{{< /eq >}}

这个数字的物理含义极其直白：<strong>在这块芯片上，你从 HBM 每读 1 个字节，必须配活 295 次浮点运算，算力才不会饿死。</strong>

{{< figure src="fig3.svg" alt="H100 的 Roofline 模型与三个 LLM 工作点" caption="图 3 — H100 SXM（bf16 稠密）的 Roofline。三个工作点分别是 decode batch=1、decode batch=256 上下文 2K、prefill 2048 token。注意后两者虽然形状不同，但都落在带宽受限区。阴影部分是被浪费掉的算力。" >}}

{{% admonition tip "Roofline 的使用方法（这是你以后做分析的标准动作）" %}}
<strong>第一步：</strong>数出算子的 FLOPs（= 2 × MAC 数）。  
<strong>第二步：</strong>数出它必须从 HBM 读写的字节数（注意：是 HBM，不是 L2；片上复用不算）。  
<strong>第三步：</strong>相除得到 `I`，与 `I_m = P_peak/BW` 比较。  
<strong>第四步：</strong>若 `I < I_m`，任何"提升算力"的优化（更快的 Tensor Core、更高的频率）都**完全没有用**，唯一有效的方向是减少 HBM 字节数或提高复用。
{{% /admonition %}}

### 3.6 代际趋势：差距在扩大还是缩小

把三代 NVIDIA 数据中心 GPU 的机器平衡点算出来：

| 型号 | 年份 | BF16 稠密 | FP8 稠密 | 带宽 | I<sub>m</sub> (BF16) | I<sub>m</sub> (FP8) |
| --- | --- | --- | --- | --- | --- | --- |
| A100 80GB SXM | 2021 | 312 T | — | 2.04 TB/s | 153 | — |
| H100 SXM | 2022 | 989 T | 1 979 T | 3.35 TB/s | 295 | 591 |
| H200 SXM | 2024 | 989 T | 1 979 T | 4.8 TB/s | 206 | 412 |
| B200 SXM | 2025 | 2 250 T | 4 500 T | 8.0 TB/s | 281 | 563 |
| B200（FP4） | 2025 | — | 9 000 T | 8.0 TB/s | — | 1 125 |

{{% admonition danger "这是一张必须盯着看三十秒的表" %}}
<strong>(1) 从 A100 到 B200，机器平衡点从 153 涨到 281（BF16）。</strong>也就是说，硬件对"每字节要配多少次运算"的要求**提高了一倍**。而 LLM decode 的算术强度没有变（还是 1 左右）。<strong>两者的差距在扩大，不是在缩小。</strong>

<strong>(2) 唯一让机器平衡点下降的是 H200——它只加了带宽，没加算力。</strong>这说明产业界已经意识到了问题：H200 相对 H100 的卖点就是 141 GB / 4.8 TB/s。

<strong>(3) 精度越低，机器平衡点越高。</strong>FP8 让峰值算力翻倍，但带宽不变，所以 `I_m` 直接翻倍（295 → 591）。这意味着**降低精度会让"打满算力"变得更难**——虽然绝对速度确实快了。这个反直觉的结论在 §4.6 会严格推导。
{{% /admonition %}}

{{< figure src="fig4.svg" alt="decode 算术强度区间与硬件机器平衡点区间的对比" caption="图 4 — 两个区间完全不相交。这不是某个实现的缺陷，而是结构性的：自回归解码的算术强度被'每个权重每个 token 只用一次'这条物理约束锁死在个位数到几十，而硬件的算力/带宽比被工艺和封装锁死在几百。所有推理优化的本质，都是在这两个区间之间搭桥。" >}}

### 3.7 多卡互联：带宽阶梯

| 互联 | 带宽 | 相对 HBM | 用途 |
| --- | --- | --- | --- |
| NVLink 4（H100，18 link） | 900 GB/s 双向 | 1 / 3.7 | 节点内 8 卡全互联 |
| NVLink 5（B200） | 1 800 GB/s 双向 | 1 / 4.4 | 同上 |
| NVSwitch（节点内） | 全对全无阻塞 | — | DGX 机箱内交换 |
| PCIe Gen5 ×16 | 128 GB/s 双向 | 1 / 26 | CPU ↔ GPU |
| InfiniBand NDR 400G ×8 | ~50 GB/s 有效/卡 | 1 / 67 | 跨节点 |

{{% admonition warning "这张表决定了并行策略的边界" %}}
注意 **NVLink 比 HBM 慢 3.7 倍，PCIe 慢 26 倍，跨节点网络慢 67 倍**。任何并行策略，只要需要在卡间传递的数据量接近模型权重量，就会被互联带宽卡死。

直接推论：**张量并行（TP）只在节点内可行**（它每层要传 2 次激活，数据量 = `B × s × d`，尚可控）；**流水线并行（PP）跨节点可行**（只在层边界传一次）；**数据并行（DP）的梯度同步最贵**，必须靠 ring-all-reduce 把通信量摊薄。
{{% /admonition %}}

## 推理的两个阶段：prefill 与 decode

### 4.1 形态差异：GEMM 与 GEMV

同一个模型，同一块芯片，两个阶段的行为完全相反：

|  | prefill（处理 prompt） | decode（逐 token 生成） |
| --- | --- | --- |
| 输入张量 | [1, s, d]，s 很大 | [B, 1, d] |
| 矩阵乘形态 | **GEMM**：矩阵 × 矩阵 | **GEMV**：矩阵 × 向量（B>1 时是瘦 GEMM） |
| 每个权重被复用 | s 次（一批 token 共享） | B 次 |
| 算术强度 | ~10³ FLOP/byte | 1 ~ 60 FLOP/byte |
| 瓶颈 | Tensor Core 算力 | HBM 带宽 |
| 优化目标 | 提高 MFU（算力利用率） | 提高 MBU（带宽利用率） |
| 关键指标 | TTFT（首 token 延迟） | TPOT（每输出 token 时间） |

{{% admonition note "两个必须分开看的指标" %}}
服务一个请求的总时间 ≈ `TTFT + n_out × TPOT`。前者是 prefill（算力问题），后者是 decode（带宽问题）。**它们的优化手段基本不重叠**，所以生产系统常常把两个阶段拆到不同的 GPU 池上运行（prefill-decode disaggregation），这是 2024 年以来的一个重要架构趋势。
{{% /admonition %}}

### 4.2 Llama-3-8B on H100：完整账本

现在把所有东西合起来，做一次完整的算账。条件：batch=1，上下文 s=2048，bf16，H100 SXM（989.4 TFLOPS 稠密 / 3.35 TB/s）。

#### 访存量

{{< eq >}}
\begin{aligned}
\text{权重} &= N_{mm} \cdot p = 7.505\times10^{9} \times 2 = 15.01\ \text{GB} \\
\text{KV cache} &= 2 \cdot L \cdot h_{kv} \cdot d_h \cdot p \cdot s = 2 \times 32 \times 8 \times 128 \times 2 \times 2048 = 0.268\ \text{GB} \\
\text{合计} &= 15.28\ \text{GB}
\end{aligned}
{{< /eq >}}

{{% admonition warning "注意 N_mm 不是 N" %}}
表中的 `N_mm = 7.505 B` 是**参与矩阵乘法的参数量**，等于 `N − V·d = 8.030 − 0.525 = 7.505 B`。embedding 表（525 M 参数）是**查表**而不是矩阵乘，只读取一行（8 KB），不参与 `2N` 这个算力公式。这是文献中常见的 7% 误差来源。
{{% /admonition %}}

#### 算力

{{< eq >}}
\begin{aligned}
\text{权重矩阵乘} &= 2 N_{mm} = 15.01\ \text{GFLOP} \\
\text{注意力} &= 4 \cdot L \cdot h \cdot d_h \cdot s = 4 \times 32 \times 32 \times 128 \times 2048 = 1.07\ \text{GFLOP} \\
\text{合计} &= 16.08\ \text{GFLOP}
\end{aligned}
{{< /eq >}}

#### 时间

{{< eq >}}
T_{mem} = \frac{15.28\times10^{9}}{3.35\times10^{12}} = 4.56\ \text{ms} \qquad T_{cmp} = \frac{16.08\times10^{9}}{9.894\times10^{14}} = 16.3\ \mu\text{s}
{{< /eq >}}

{{% admonition tip "这就是全报告最重要的一个数字" %}}
**4.56 ms ÷ 16.3 µs = 280**

生成一个 token，搬运数据要 4.56 毫秒，而真正的计算只要 16 微秒。<strong>Tensor Core 有 99.6% 的时间在空转。</strong>把 989 TFLOPS 的峰值算力拿来做算术，实际性能是 `16.08e9 / 4.56e-3 = 3.5 TFLOPS`，即<strong>峰值的 0.36%</strong>。

单用户吞吐上限 = `1 / 4.56 ms = 219 token/s`。这是 H100 跑 Llama-3-8B 在 batch=1 时的**物理天花板**，与软件实现无关。实测值通常在 120–180 tok/s（受 kernel 开销、算子未完美融合影响）。
{{% /admonition %}}

### 4.3 KV cache 的定量分析

KV cache 是自回归生成的显性代价，必须能随手算出来。

{{< eq >}}
\text{KV/token} = 2 \cdot L \cdot h_{kv} \cdot d_h \cdot p \qquad \text{KV/请求} = s \times \text{KV/token}
{{< /eq >}}

对 Llama-3-8B：`2 × 32 × 8 × 128 × 2 = 131 072 B = 128 KB/token`。不同上下文长度下：

| 上下文 s | 单请求 KV | B=32 总 KV | B=128 总 KV | H100 剩余容量能否装下 |
| --- | --- | --- | --- | --- |
| 2 048 | 0.27 GB | 8.6 GB | 34 GB | 可以 |
| 8 192 | 1.07 GB | 34 GB | 137 GB | B=128 装不下 |
| 32 768 | 4.29 GB | 137 GB | 549 GB | 需多卡 |
| 131 072 | 17.2 GB | 549 GB | 2.2 TB | 需 28 卡以上 |

（可用容量取 `80 − 16.06 = 63.9 GB`）

{{< rawhtml >}}
<div class="rpt-calc">
<div class="calc">
<div class="row">
<div><label>参与矩阵乘的参数 N_mm (B)</label><input type="number" id="i_n" value="7.505" step="0.1"></div>
<div><label>层数 L</label><input type="number" id="i_l" value="32" step="1"></div>
<div><label>查询头数 h</label><input type="number" id="i_h" value="32" step="1"></div>
<div><label>KV 头数 h_kv</label><input type="number" id="i_hkv" value="8" step="1"></div>
<div><label>每头维度 d_h</label><input type="number" id="i_dh" value="128" step="8"></div>
<div><label>每参数字节 p</label><input type="number" id="i_p" value="2" step="0.25"></div>
<div><label>上下文 s</label><input type="number" id="i_s" value="2048" step="512"></div>
<div><label>批大小 B</label><input type="number" id="i_b" value="1" step="1"></div>
<div><label>显存带宽 (TB/s)</label><input type="number" id="i_bw" value="3.35" step="0.05"></div>
<div><label>峰值算力 (TFLOPS)</label><input type="number" id="i_pp" value="989.4" step="10"></div>
</div>
<div class="out">
<div><span>每 token KV</span><b id="o_kv1">—</b></div>
<div><span>单请求 KV cache</span><b id="o_kv2">—</b></div>
<div><span>全部请求 KV</span><b id="o_kv3">—</b></div>
<div><span>权重显存</span><b id="o_w">—</b></div>
<div><span>每步访存 (HBM)</span><b id="o_by">—</b></div>
<div><span>每步算力</span><b id="o_fl">—</b></div>
<div><span>算术强度 I</span><b id="o_ai">—</b></div>
<div><span>机器平衡点 I_m</span><b id="o_im">—</b></div>
<div><span>访存 / 计算时间</span><b id="o_tt">—</b></div>
<div><span>单步时延（理论）</span><b id="o_t">—</b></div>
<div><span>吞吐（理论上限）</span><b id="o_tp">—</b></div>
<div><span>瓶颈判定</span><b id="o_bn">—</b></div>
</div>
</div>
</div>
<script>
(function(){
  var ids=["i_n","i_l","i_h","i_hkv","i_dh","i_p","i_s","i_b","i_bw","i_pp"];
  function v(id){var e=document.getElementById(id);var x=parseFloat(e.value);return isFinite(x)?x:0;}
  function fmt(x,u){return (Math.round(x*100)/100).toLocaleString()+" "+u;}
  function calc(){
    var N=v("i_n")*1e9, L=v("i_l"), h=v("i_h"), hkv=v("i_hkv"), dh=v("i_dh"),
        p=v("i_p"), s=v("i_s"), B=v("i_b"), BW=v("i_bw")*1e12, PP=v("i_pp")*1e12;
    var kvTok=2*L*hkv*dh*p;
    var kvReq=kvTok*s;
    var bytesW=N*p;
    var bytesStep=bytesW+B*kvReq;
    var flopsStep=B*(2*N+4*L*h*dh*s);
    var tmem=bytesStep/BW*1000, tcmp=flopsStep/PP*1000;
    var T=Math.max(tmem,tcmp);
    var Im=PP/BW;
    document.getElementById("o_kv1").textContent=(kvTok/1024).toFixed(1)+" KB";
    document.getElementById("o_kv2").textContent=fmt(kvReq/1e9,"GB");
    document.getElementById("o_kv3").textContent=fmt(B*kvReq/1e9,"GB");
    document.getElementById("o_w").textContent=fmt(bytesW/1e9,"GB");
    document.getElementById("o_by").textContent=fmt(bytesStep/1e9,"GB");
    document.getElementById("o_fl").textContent=fmt(flopsStep/1e12,"TFLOP");
    document.getElementById("o_ai").textContent=(flopsStep/bytesStep).toFixed(2);
    document.getElementById("o_im").textContent=Im.toFixed(1);
    document.getElementById("o_tt").textContent=tmem.toFixed(2)+" / "+tcmp.toFixed(3)+" ms";
    document.getElementById("o_t").textContent=T.toFixed(2)+" ms";
    document.getElementById("o_tp").textContent=Math.round(B/T*1000).toLocaleString()+" tok/s";
    var bn=document.getElementById("o_bn");
    if(tmem>tcmp*1.2){bn.textContent="带宽受限";bn.style.color="#A32D2D";}
    else if(tcmp>tmem*1.2){bn.textContent="算力受限";bn.style.color="#0F6E56";}
    else{bn.textContent="接近平衡";bn.style.color="#854F0B";}
  }
  ids.forEach(function(id){document.getElementById(id).addEventListener("input",calc);});
  calc();
})();
</script>
{{< /rawhtml >}}

{{% admonition note "用这个计算器做几个实验，你会自己发现结论" %}}
<strong>实验 1：</strong>把 `B` 从 1 调到 256，看吞吐怎么涨、算术强度怎么涨、瓶颈什么时候切换。  
<strong>实验 2：</strong>把 `s` 调到 32768，再把 `B` 调到 256。**你会发现算术强度不再随 B 上升**——这就是 §4.5 要讲的长上下文反超。  
<strong>实验 3：</strong>把 `p` 从 2 改成 1（FP8），同时把峰值算力改成 1978.9。你会发现**绝对时间减半，但"带宽受限"的判定不变**——这就是 §4.6 的结论。
{{% /admonition %}}

### 4.4 一个反直觉的对照：prefill 有多快

同样 2048 个 token，prefill 一次性处理：

{{< eq >}}
\begin{aligned}
F_{\text{prefill}} &= 2 N_{mm} s \;+\; 4 L h d_h \frac{s^2}{2} = 30.7\ \text{TFLOP} + 1.10\ \text{TFLOP} = 31.8\ \text{TFLOP} \\
\text{bytes} &= 15.01\ \text{GB}\ (\text{权重读一次}) + \sim\!0.5\ \text{GB} = 15.5\ \text{GB} \\
I &= 2054\ \text{FLOP/byte} \;\gg\; I_m = 295 \\
T &= \max\!\left(\frac{15.5}{3350},\ \frac{31.8\times10^{12}}{9.894\times10^{14}}\right) = \max(4.6\ \text{ms},\ 32.1\ \text{ms}) = 32.1\ \text{ms}
\end{aligned}
{{< /eq >}}

32 ms 处理 2048 个 token → **64 000 token/s**，是 decode 的 **290 倍**。

{{% admonition tip "这个 290 倍的差距，是整个推理系统设计的出发点" %}}
它意味着：<strong>让 GPU 一次多干点活，比让它多干几次活，效率高两个数量级。</strong>所有提升吞吐的技术——continuous batching、speculative decoding、prompt caching、chunked prefill——本质上都在试图把 decode 变成 prefill。

它也解释了为什么"首 token 慢、后续 token 快"不是 bug：TTFT 中包含了一次完整的 prefill，它的成本约等于生成 300 个 token。
{{% /admonition %}}

### 4.5 batch 的收益上限，以及长上下文的反超

把 batch 作为变量，写出完整的时延模型：

{{< eq >}}
\begin{aligned}
\text{bytes}(B,s) &= N_{mm}\,p \;+\; B \cdot \underbrace{2 L h_{kv} d_h p\,s}_{\text{每请求 KV}}\\
\text{FLOPs}(B,s) &= B \cdot \big(2N_{mm} + 4 L h d_h s\big)\\[2pt]
T(B,s) &= \max\!\left(\frac{\text{bytes}}{BW},\ \frac{\text{FLOPs}}{P_{peak}}\right), \qquad \text{吞吐} = \frac{B}{T}
\end{aligned}
{{< /eq >}}

#### 固定 s = 2048，扫 batch

| B | 每步访存 | 访存耗时 | 计算耗时 | 单步时延 | 吞吐 | Tensor Core 利用率 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 15.3 GB | 4.56 ms | 0.016 ms | 4.56 ms | 219 | 0.4% |
| 8 | 17.2 GB | 5.12 ms | 0.13 ms | 5.12 ms | 1 562 | 2.5% |
| 32 | 23.6 GB | 7.04 ms | 0.52 ms | 7.04 ms | 4 543 | 7.4% |
| 64 | 32.2 GB | 9.61 ms | 1.04 ms | 9.61 ms | 6 660 | 10.8% |
| 128 | 49.4 GB | 14.7 ms | 2.08 ms | 14.7 ms | 8 684 | 14.1% |
| **238** | 78.9 GB | 23.6 ms | 3.87 ms | 23.6 ms | **10 106** | 16.4% |

注：`B = 238` 是显存容量上限（`63.9 GB ÷ 0.268 GB/请求`）。表中为 100% 带宽利用率与 100% MFU 下的**理论值**，实测通常为 40–60%，即 **4 000–6 000 tok/s**——这与公开 benchmarks 中 vLLM / TRT-LLM 在 H100 上跑 Llama-3-8B 的数字吻合。

#### 长上下文反超：一个必须自己推一遍的结论

让 `B → ∞`（权重访存被完全摊薄），算术强度的极限是：

{{< eq >}}
I_\infty(s) = \frac{2N_{mm} + 4 L h d_h s}{2 L h_{kv} d_h p\,s}
{{< /eq >}}

| 上下文 s | I<sub>∞</sub>（B 再大也到不了更高） | 与 I<sub>m</sub>=295 的关系 | 含义 |
| --- | --- | --- | --- |
| 393 | 295 | = 1.0 × | 临界点 |
| 512 | 228 | 0.77 × | 差一点 |
| 1 024 | 116 | 0.39 × | 够不着 |
| 2 048 | 59.9 | 0.20 × | 够不着 |
| 4 096 | 32.0 | 0.11 × | 远不够 |
| 8 192 | 18.0 | 0.061 × | 远不够 |
| 32 768 | 7.49 | 0.025 × | 极远 |
| 131 072 | 4.87 | 0.016 × | 极远 |
| s → ∞ | 4.00 = h / h<sub>kv</sub> | 0.014 × | 渐近线 |

{{% admonition danger "结论：bf16 下，上下文超过约 400 token，decode 就永远打不满 H100 的算力" %}}
这个结论值得反复咀嚼：<strong>不是"batch 不够大"，是"batch 再大也没用"。</strong>因为当 batch 增大时，权重访存被摊薄了，但 KV cache 的访存量随 batch *同步线性增长*——它完全不可复用（每个请求读自己的 KV）。于是算术强度卡死在上面那个极限值上。

<strong>而 KV cache 的读取量与 s 成正比，注意力的计算量也与 s 成正比，两者同阶。</strong>所以极限算术强度只由架构比例决定：

{{< eq >}}
I_\infty(s\to\infty) = \frac{4 L h d_h}{2 L h_{kv} d_h p} = \frac{2h}{h_{kv}\,p} = \frac{2}{p}\cdot\frac{h}{h_{kv}}
{{< /eq >}}

这就是 **GQA 的真实价值**：它把长上下文下的渐近算术强度从 `2/p = 1.0`（MHA）提升到 `2/p × h/h_kv = 4.0`。四个 KV 头不是省显存，是**把渐近算术强度提高了 4 倍**。
{{% /admonition %}}

### 4.6 量化能带来多少：一个严格的上界

设压缩比 `c`（新字节数 = `c ×` 原字节数，如 bf16→fp8 为 `c = 0.5`），算力提升比 `r`（新峰值 = `r ×` 旧峰值）：

{{< eq >}}
I_{\text{new}} = \frac{I}{c}, \qquad I_{m,\text{new}} = r\,I_m, \qquad \frac{I_{\text{new}}}{I_{m,\text{new}}} = \frac{1}{c\cdot r}\cdot\frac{I}{I_m}
{{< /eq >}}

| 方案 | c（字节压缩） | r（算力提升） | c·r | 相对瓶颈位置改善 | 绝对速度提升 |
| --- | --- | --- | --- | --- | --- |
| bf16 → fp8（Hopper） | 0.5 | 2 | 1.0 | 无改善 | 2.0 × |
| bf16 → fp4（Blackwell） | 0.25 | 4 | 1.0 | 无改善 | 4.0 × |
| 权重 int4 + fp16 计算 | 0.25 | 1 | 0.25 | 4.0 × | 4.0 × |
| 权重 int4 + KV fp8（混合） | ~0.3 | 1 | ~0.3 | ~3.3 × | ~3.3 × |

{{% admonition tip "量化加速的本质：买的是字节数，不是算力" %}}
**加速比 ≈ 压缩比**（在带宽受限区，`T ∝ bytes`）。fp8 快一倍，是因为字节数少了一半，*不是*因为算力翻了一倍。

**但量化不改变"memory-bound"这个定性判断**——只要厂商同步把算力提上去（fp8 算力 = bf16 的 2 倍），机器平衡点也翻倍，你就还是踩在带宽屋脊上，只是位置沿斜线往上挪了。要真正改变性质，必须让 `c·r < 1`，即**压缩比要超过算力提升比**。只有"权重低比特 + 计算用高精度"（如 W4A16、W8A8 之外的 A16 方案）能满足这一点。

<strong>推论：长上下文下，KV cache 量化比权重量化更重要。</strong>在 s=32K 时，KV 访存已占总访存的 70% 以上，此时量化权重收益有限，量化 KV 才是关键。
{{% /admonition %}}

## 优化技术的物理本质：一个分类学

有了 Roofline 这套语言，市面上所有的推理优化技术都可以归到**五个物理动作**里。看到任何一篇新论文，先问它动的是哪一项：

| 动作 | 改变 Roofline 里的什么 | 代表技术 |
| --- | --- | --- |
| **① 减少字节数** | 降低 bytes → T 线性下降 | 量化、剪枝、蒸馏、稀疏化 |
| **② 提高复用率** | 提高 I（同样字节多干活） | batching、speculative decoding、prompt caching |
| **③ 减少数据量本身** | 降低 KV 的 bytes | GQA、MLA、KV 量化、KV 驱逐/压缩 |
| **④ 减少访存次数** | 去掉中间结果的 HBM 往返 | 算子融合、FlashAttention、CUDA Graph |
| **⑤ 换硬件** | 改变 BW 或 I<sub>m</sub> | SRAM 方案、存内计算、wafer-scale |

### 5.2 FlashAttention：它优化的是访存，不是算力

标准注意力的 HBM 访存（单个头，序列长 `s`，bf16）：

{{< eq >}}
\underbrace{s^2 p}_{\text{写 S}} + \underbrace{s^2 p}_{\text{读 S 做 softmax}} + \underbrace{s^2 p}_{\text{写 P}} + \underbrace{s^2 p}_{\text{读 P 乘 V}} = 4 s^2 p
{{< /eq >}}

FlashAttention 把 Q、K、V 分块载入 SRAM，在片上完成整个 `softmax(QKᵀ)V`，中间矩阵**从不落 HBM**：[[7]](#r7)

{{< eq >}}
\underbrace{s d_h p}_{Q} + \underbrace{2 s d_h p}_{K,V} + \underbrace{2 s d_h p}_{\text{写 O}} \approx 5 s d_h p \quad(\text{加上 softmax 统计量的分块处理})
{{< /eq >}}

{{< eq >}}
\frac{\text{标准}}{\text{Flash}} = \frac{4 s^2 p}{5 s d_h p} = \frac{4s}{5 d_h} = \frac{4 \times 2048}{5 \times 128} = 12.8\times
{{< /eq >}}

12.8 倍的访存削减。但必须注意两件事：

{{% admonition warning "FlashAttention 的两个常见误解" %}}
<strong>误解 1："它加速了注意力计算。"</strong>错。它的 FLOPs *一点没少*（甚至略多，因为 online softmax 有重算）。在算力受限的场景它没有加速效果。

<strong>误解 2："它对 decode 帮助最大。"</strong>错。decode 时 `s_新 = 1`，中间矩阵只有一行，本来就没有 O(s²) 的问题。**FlashAttention 的主战场是 prefill 和训练**。它对 decode 的价值是*显存容量*（让 128K 上下文成为可能），而非速度。

真正的算法创新是 **online softmax**：把 softmax 改造成可以增量合并的形式，从而允许分块计算。这是"用数学变换换掉访存"的典范。
{{% /admonition %}}

### 5.3 MLA：把 KV cache 压到极致

DeepSeek-V2 提出的 MLA（Multi-head Latent Attention）走得更远：不缓存 K/V 本身，而是缓存一个低秩压缩的潜向量。[[8]](#r8)

{{< eq >}}
\mathbf{c}_{KV} = W_{DKV}\,\mathbf{x} \in \mathbb{R}^{d_c}, \qquad K = W_{UK}\,\mathbf{c}_{KV},\quad V = W_{UV}\,\mathbf{c}_{KV}
{{< /eq >}}

KV cache 从 `2 h_kv d_h` 降到 `d_c`（DeepSeek-V2 中 `d_c = 512`，对比 MHA 的 8192）：

| 方案 | KV cache / token（Llama-3-8B 尺度，bf16） | 相对 MHA |
| --- | --- | --- |
| MHA（h_kv = 32） | 512 KB | 1 × |
| GQA（h_kv = 8） | 128 KB | 1/4 |
| MLA（d_c = 512，含 RoPE 解耦部分） | ~36 KB | 1/14 |

代价是**额外的解压缩计算**：每步要把潜向量乘回 `W_UK`、`W_UV`。这正是典型的"用算力换带宽"交易——而鉴于 §4.2 已经证明算力有 280 倍富余，这笔交易非常划算。这也是模型-硬件协同设计（co-design）最清晰的例子。

### 5.4 Speculative Decoding：把 decode 变成 prefill

用一个小模型（draft）先猜 `γ` 个 token，再让大模型一次性验证这 `γ+1` 个位置。[[9]](#r9)

{{< eq >}}
\mathbb{E}[\text{接受数}] = \frac{1-\alpha^{\gamma+1}}{1-\alpha}, \qquad \text{加速比} \approx \frac{\mathbb{E}[\text{接受数}]}{1 + \gamma \cdot c}
{{< /eq >}}

其中 `α` 是单 token 接受率，`c` 是 draft 模型相对 target 的单步成本比（典型 0.02–0.05，因为 draft 小 20–50 倍）。

{{% admonition tip "它为什么有效：它改变了 Roofline 上的工作点" %}}
验证步骤一次性处理 `γ+1` 个 token → 这**不是 GEMV，而是一个 (γ+1) 行的 GEMM**。每个权重被复用 `γ+1` 次，算术强度乘以 `γ+1`。同时，总访存量几乎不变（权重还是读一遍，KV 多读一点）。

所以投机解码的本质是：<strong>用"多算几次小模型的账"换取"把大模型的 GEMV 变成 GEMM"</strong>。它直接攻击 §4.2 那个 280 倍的差距。

一个重要的边界：当 batch 已经很大（算力接近饱和）时，投机解码**不再有效甚至变慢**，因为此时大模型本来就不缺复用度。这就是为什么它主要用在低延迟、低并发场景。
{{% /admonition %}}

### 5.5 MoE：甜蜜的陷阱

以 Mixtral 8×7B 为例：8 个专家，每 token 激活 top-2。总参数 46.7 B，激活参数 12.9 B。

| 指标 | MoE 8×7B | 等效 dense 13B | 差异 |
| --- | --- | --- | --- |
| 每 token 算力 | 2 × 12.9 B = 25.8 GFLOP | 26 GFLOP | 相同 |
| 每 token 访存（B=1） | 25.8 GB | 26 GB | 相同 |
| **权重显存容量** | **93 GB** | 26 GB | **3.6 ×** |
| 知识容量 | 46.7 B 参数 | 13 B 参数 | 3.6 × |

{{% admonition note "MoE 的准确表述" %}}
<strong>MoE 不是一个"省计算"的技术，而是一个"用显存容量换模型质量"的技术。</strong>在算力与带宽上，Mixtral 8×7B 的每 token 代价与一个 dense 13B 模型完全相同；它换来的是 3.6 倍的参数容量（更好的质量），代价是 3.6 倍的显存占用。

<strong>但它有一个隐藏的带宽陷阱：</strong>每个专家实际只能看到 `B·k/E` 个 token（k=激活数，E=专家数）。对 Mixtral，若 B=64，每个专家只看到 `64×2/8 = 16` 个 token。也就是说 **MoE 把"有效 batch"缩小了 E/k = 4 倍**，从而把每个专家 GEMM 的算术强度也降低了约 4 倍。  
→ <strong>MoE 需要比 dense 模型大 4 倍的全局 batch，才能达到同样的带宽效率。</strong>这就是为什么 MoE 在高吞吐场景表现优异，而在单用户低延迟场景常常不如同质量的 dense 模型。
{{% /admonition %}}

### 5.7 硬件路线之争：HBM 容量 vs SRAM 带宽

既然瓶颈是带宽，一个自然的想法是：**把权重放进 SRAM**。Groq 的 LPU 就是这条路线的极端代表。[[10]](#r10)

| 维度 | NVIDIA H100（HBM 路线） | Groq LPU（SRAM 路线） |
| --- | --- | --- |
| 片上存储 | 50 MB L2 + 33 MB SMEM | 230 MB 全局 SRAM |
| 片外存储 | 80 GB HBM3 | 无 |
| 带宽 | 3.35 TB/s | **80 TB/s** |
| 算力（FP16 / INT8） | 989 TFLOPS / 1 979 TOPS | 188 TFLOPS / 750 TOPS |
| **机器平衡点 I<sub>m</sub>** | **295** | **2.35** |
| 工艺 | TSMC 4N | 14 nm |
| TDP | 700 W | 215 W |

{{% admonition tip "这张表最该看的是机器平衡点那一行" %}}
Groq 的 `I_m = 2.35 FLOP/byte`，而 decode 的算术强度是 1.05——**两者只差 2.2 倍，几乎是平衡的**。这就是它能跑到 250+ tok/s（Llama-2-70B 实测 241 tok/s）的原因：它的算力虽只有 H100 的 1/5，但**利用率高了两个数量级**。

<strong>代价是容量。</strong>230 MB SRAM 连一个 INT8 量化的 8B 模型（约 8 GB）都装不下，需要 **35 颗芯片**才够放权重。SemiAnalysis 记录：服务 Mixtral 8×7B 用了 <strong>576 颗 GroqChip（8 个机柜）</strong>，而单张 H100 就能装下。

<strong>所以这不是"更好的架构"，而是一个不同的取舍点：</strong>SRAM 路线把带宽提高 24 倍、把容量降低 348 倍。它赢在**单流延迟**（低 batch、实时场景），输在**成本每 token**（高吞吐场景）。理解这一点，就能预判任何一种新型加速器的适用边界——只需要看它的 `I_m` 与目标负载的 `I` 是否匹配。
{{% /admonition %}}

同一思路的其他形态：Cerebras 的 wafer-scale engine 把整片晶圆做成一个芯片，用海量片上 SRAM + 极高片上带宽解决同一问题；各种存内计算（PIM / CIM）方案则试图让"乘加"直接在存储阵列里发生，从根本上消除搬运。它们的评估方法完全一致：<strong>先算 `I_m`，再看目标负载的 `I`。</strong>

## 训练为什么完全是另一回事

### 6.1 6ND 的推导

前向传播：每个权重张量参与一次矩阵乘 → `2N` FLOP/token。

反向传播需要两类梯度，各对应一次矩阵乘：

- **输入梯度** `∂L/∂X = (∂L/∂Y) Wᵀ` —— 用权重张量做一次矩阵乘 → `2N`
- **权重梯度** `∂L/∂W = Xᵀ (∂L/∂Y)` —— 再做一次矩阵乘 → `2N`

{{< eq >}}
C_{\text{step}} = \underbrace{2N}_{\text{前向}} + \underbrace{4N}_{\text{反向}} = 6N \ \text{FLOP/token}, \qquad C_{\text{total}} = 6 N D
{{< /eq >}}

{{% admonition warning "6N 里没有算什么" %}}
`6N` 只覆盖矩阵乘法。它**不包含**：注意力里的 softmax、激活函数、归一化、（若开启）激活重计算带来的一次额外前向（会再加 `2N`，使总系数变成 `8N`）。对大模型这些通常占 5–15%，所以 `6ND` 是一个**下界估计**，精度在 ±20% 内——对量级判断足够，对成本核算需实测。
{{% /admonition %}}

### 6.2 显存账：为什么训练 8B 需要好几张卡

| 组成部分 | 字节 / 参数 | 8B 模型 | 能否省掉 |
| --- | --- | --- | --- |
| 权重（bf16，用于前向） | 2 | 16.1 GB | 否 |
| 梯度（bf16） | 2 | 16.1 GB | ZeRO-2 可分片 |
| fp32 主权重（优化器用） | 4 | 32.1 GB | ZeRO-3 可分片 |
| Adam 一阶动量 m（fp32） | 4 | 32.1 GB | 8-bit Adam 可降到 1 |
| Adam 二阶动量 v（fp32） | 4 | 32.1 GB | 8-bit Adam 可降到 1 |
| <strong>小计（不含激活）</strong> | **16** | **128.5 GB** |  |
| 激活（batch 相关） | — | 10–100 GB | 可用重计算换 |

{{% admonition tip "记住这条经验法则" %}}
<strong>训练显存 ≈ 16N，推理显存 ≈ 2N，比值 8 倍。</strong>所以"一张卡装得下模型"和"一张卡训得了模型"是完全不同的问题——H100 的 80 GB 装得下 16 GB 的 Llama-3-8B，但装不下它的 128 GB 优化器状态。

这也解释了为什么 **ZeRO / FSDP 是训练大模型的必需品**：ZeRO-1 分片优化器状态（16N → 6N）、ZeRO-2 再分片梯度（→4N）、ZeRO-3 再分片参数（→ 16N/DP 度）。8 卡 ZeRO-3 下，每张卡的静态显存降到 `128.5/8 = 16 GB`。
{{% /admonition %}}

### 6.3 并行策略的带宽需求：给出临界值

#### 张量并行（TP）：必须待在节点内

每层需要 2 次 all-reduce（注意力输出后、FFN 输出后），每次传输 `T_mb × d × p` 字节（`T_mb` = 每个微批次的 token 数）。

{{< eq >}}
\frac{t_{\text{comm}}}{t_{\text{comp}}} = \frac{4 L d p / BW_{\text{nvlink}}}{2 N_{mm} / P_{peak}} = \frac{4\times32\times4096\times2 / 9\times10^{11}}{15.01\times10^{9} / 9.894\times10^{14}} = \frac{1.17\ \mu s}{15.2\ \mu s} = 7.7\%
{{< /eq >}}

{{% admonition note "关键性质：这个比值与 batch 无关" %}}
分子分母都正比于 token 数，所以**加大 batch 救不了 TP 的通信**。若把链路换成 PCIe（128 GB/s 双向），同一比值变成 <strong>54%</strong>——直接不可用。这就是"TP 只能在 NVLink 域内做"的定量依据。
{{% /admonition %}}

#### 数据并行（DP）：必须开大 batch

每步一次梯度 all-reduce，通信量 `≈ 2 × N × p`（ring all-reduce 传输量），与 batch 无关；而计算量正比于 batch。

{{< eq >}}
t_{comm} = \frac{2Np}{BW_{net}} = \frac{30\times10^{9}}{5\times10^{10}} = 0.6\ \text{s}, \qquad t_{comp} = \frac{6N \cdot T_{mb}}{P_{peak}}
{{< /eq >}}

要求通信开销 < 10%，解得每个 rank 每步的最小 token 数：

{{< eq >}}
T_{mb} \;\ge\; \frac{2 p P_{peak}}{0.6 \cdot BW_{net}} = \frac{2 \times 2 \times 9.894\times10^{14}}{0.6 \times 5\times10^{10}} \approx 1.3\times10^{5}\ \text{token}
{{< /eq >}}

即**每个 rank 每步至少约 13 万 token**。实际训练的 global batch 常在 1M–16M token，远超此值，这就是为什么大规模训练里 DP 的通信能被有效隐藏。

### 6.4 一句话对比

|  | 训练 | 推理（decode） |
| --- | --- | --- |
| 运算形态 | 大 GEMM | GEMV / 瘦 GEMM |
| 算术强度 | 10³ ~ 10⁴ | 1 ~ 60 |
| 瓶颈 | 算力（看 MFU） | 带宽（看 MBU） |
| 优化方向 | 提高矩阵乘效率、减少重计算 | 减少字节数、提高复用 |
| 显存主要占用 | 优化器状态（16N） | 权重 + KV cache |
| MFU / MBU 典型值 | 35–50% | 带宽 60–80%，算力 0.4–17% |

## 研究方法与开放问题

### 7.1 把这套分析变成可复现的脚本

本报告的每一个数字都来自下面这个 20 行的函数。建议把它抄进自己的工具箱，以后拿到任何模型配置 + 任何硬件规格，都能在 30 秒内给出结论。

```
def analyze(N_mm_B, L, h, h_kv, d_h, p, s, B, BW_TBps, P_TFLOPS):
    """N_mm_B: 参与矩阵乘的参数量(B, 需扣除 embedding 表)
       p: 每参数字节数(bf16=2, fp8=1, int4=0.5)
       BW_TBps / P_TFLOPS: 硬件的 HBM 带宽与稠密算力峰值"""
    N, BW, Pk = N_mm_B * 1e9, BW_TBps * 1e12, P_TFLOPS * 1e12
    kv_per_token = 2 * L * h_kv * d_h * p          # 每 token 的 KV 字节
    bytes_step   = N * p + B * kv_per_token * s    # 权重读一次 + 每请求读自己的 KV
    flops_step   = B * (2 * N + 4 * L * h * d_h * s)
    t_mem, t_cmp = bytes_step / BW, flops_step / Pk
    T = max(t_mem, t_cmp)
    return {
        "bytes_GB":   bytes_step / 1e9,
        "flops_TFLOP": flops_step / 1e12,
        "I":          flops_step / bytes_step,     # 算术强度
        "I_machine":  Pk / BW,                     # 机器平衡点
        "t_mem_ms":   t_mem * 1e3,
        "t_cmp_ms":   t_cmp * 1e3,
        "T_ms":       T * 1e3,
        "tok_per_s":  B / T,
        "tc_util":    t_cmp / T,                   # Tensor Core 利用率
        "bound":      "memory" if t_mem > t_cmp else "compute",
    }

# Llama-3-8B, H100 SXM, bf16, batch=1, 上下文 2048
print(analyze(7.505, 32, 32, 8, 128, 2, 2048, 1, 3.35, 989.4))
# {'I': 1.05, 'I_machine': 295.3, 't_mem_ms': 4.56, 't_cmp_ms': 0.016,
#  'T_ms': 4.56, 'tok_per_s': 219, 'tc_util': 0.0036, 'bound': 'memory'}
```

{{% admonition note "必做的三个敏感性实验" %}}
用这个函数扫参数，你会独立发现本报告的所有结论：  
<strong>(1)</strong> 固定 `s=2048`，扫 `B = 1…512` → 看到吞吐 - 算术强度的 S 形曲线，以及容量天花板。  
<strong>(2)</strong> 固定 `B=256`，扫 `s = 256…131072` → 看到算术强度跌向渐近线 `2h/(h_kv·p)`。  
<strong>(3)</strong> 把 `p` 与 `P_TFLOPS` 按 fp8（`p=1, P=1978.9`）同时改 → 看到绝对时间减半，但 `bound` 字段不变。
{{% /admonition %}}

### 7.2 读一篇系统论文的检查清单

1. <strong>它用的是稠密还是稀疏算力？</strong>看到 989.4 之外的数字先查是不是 2:1 结构化稀疏值。
2. <strong>它报的是哪种精度下的峰值？</strong>TF32 / BF16 / FP8 / FP4 之间差 8 倍，必须对齐。
3. <strong>分母里有没有扣掉 embedding？</strong>`2N` 与 `2(N − V·d)` 差 5–15%。
4. <strong>瓶颈判定有没有给算术强度？</strong>只报"我们通过 XX 提速 3 倍"而不给 Roofline 分析的论文，无法判断结论能否迁移到别的硬件。
5. <strong>batch 与上下文是多少？</strong>这是最关键的两个条件。很多"提速"只是把工作点挪到了更有利的位置，而不是真的改进了效率。
6. <strong>报的是 TTFT 还是 TPOT，还是两者的加权？</strong>把 prefill 的吞吐提升说成"推理提速"是常见的偷换。
7. <strong>基线是什么？</strong>与未优化的 PyTorch eager 比，提速 10 倍不难；与 FlashInfer / TRT-LLM 比，2 倍就很难。

### 7.3 开放问题（按硬件重要性排序）

#### ① 带宽墙能不能被打破

HBM 带宽受限于封装引脚密度与功耗：H100 的 3.35 TB/s 对应约 **3–5 pJ/byte** 的访问能耗，仅 HBM 一项就消耗数百瓦。而一次 bf16 MAC 的能耗约 **20–50 fJ**——**搬运 1 字节的能量 ≈ 做 100 次乘加**。这个 100 倍的能耗差，和 §4.2 那个 280 倍的时间差，是同一个物理事实的两种表现。

候选路线：HBM4（更宽的接口 + 更高 pin 速率）、3D 堆叠（把 HBM 直接堆在逻辑层上）、共封装光学（co-packaged optics，把 SerDes 能耗降一个数量级）、以及彻底换架构（SRAM / 存内计算）。

#### ② 能不能从架构上消灭 KV cache 的 O(s) 带宽

§4.5 已经证明：长上下文下算术强度的渐近线被 KV cache 锁死。唯一的结构性出路是**让状态不随上下文增长**：

- <strong>线性注意力 / 状态空间模型（Mamba、RWKV）</strong>：把注意力改写为循环形式，状态是固定大小的矩阵，KV cache 从 `O(s)` 降到 `O(1)`。代价是丧失了"精确检索任意历史位置"的能力，在需要精确回忆的任务上有损失。
- **MLA 这类低秩压缩**：把 `O(s)` 的常数压小 14 倍，但没改渐近复杂度。
- **混合架构**（如 Jamba、Samba）：大部分层用 SSM，少数层保留全注意力。这是 2024–2026 年最活跃的方向之一。

从硬件视角判断这类工作的标准非常简单：<strong>它的每 token 访存量是否还是 `∝ s`？</strong>如果不是，就是真正的架构突破。

#### ③ 稀疏性：硬件已经准备好，算法还没

自 Ampere 起，NVIDIA Tensor Core 就支持 2:1 结构化稀疏（每 4 个元素中至少 2 个为零），硬件上直接给 2 倍吞吐。但**要在精度损失可接受的前提下剪出这种模式的权重，至今没有可靠的通用方法**。这是硬件能力与算法能力错位的典型案例。

#### ④ 低比特训练的下限在哪里

FP8 训练已在 H100/B200 上可用（Transformer Engine 自动管理缩放）。FP4 训练（Blackwell）仍是开放问题，主要障碍是梯度分布的动态范围与随机舍入的硬件支持。若 FP4 训练可行，训练成本可再降 2–3 倍。

#### ⑤ 编译与调度：tile 选择的组合爆炸

给定一层 GEMM 的形状与硬件参数，最优 tile 尺寸、双缓冲深度、swizzle 模式、融合策略构成一个巨大的搜索空间。手工写 kernel（cuBLAS / CUTLASS / Triton）与自动搜索（AutoTVM、Ansor）仍在竞争中。**对推理而言，小算子融合策略的影响往往超过 GEMM 本身**——因为 §2.7 已经算过，低 batch 下 kernel 启动开销可以占到 16%。

### 7.4 术语表

| 英文 | 中文 | 一句话定义 |
| --- | --- | --- |
| **Arithmetic intensity** | 算术强度 | FLOPs / 从 HBM 搬运的字节数，判断瓶颈的唯一指标 |
| **Machine balance / ridge point** | 机器平衡点 | `P_peak / BW`，芯片"每字节要求多少次运算" |
| **MFU** | 算力利用率 | 实测算力 / 峰值算力。训练中关注 |
| **MBU** | 带宽利用率 | 实测带宽 / 峰值带宽。decode 中关注 |
| **GEMM / GEMV** | 矩阵-矩阵 / 矩阵-向量乘 | prefill 是 GEMM，decode 是 GEMV，决定一切 |
| **KV cache** | 键值缓存 | 缓存历史 token 的 K/V，用显存换重复计算 |
| **GQA / MQA / MHA** | 分组/多查询/多头注意力 | 减少 KV 头数以削减 KV cache 带宽 |
| **MLA** | 多头潜注意力 | 缓存低秩压缩向量，KV cache 再降 3–4 倍 |
| **MoE** | 混合专家 | 每 token 只激活部分专家：省计算，不省显存 |
| **prefill / decode** | 预填充 / 解码 | 处理 prompt（算力受限）/ 逐 token 生成（带宽受限） |
| **TTFT / TPOT** | 首 token 时间 / 每输出 token 时间 | 分别对应 prefill 与 decode 的延迟指标 |
| **Continuous batching** | 连续批处理 | 新请求随时插入、完成的请求随时退出，保持高 batch |
| **Speculative decoding** | 投机解码 | 小模型猜、大模型批量验证，把 GEMV 变 GEMM |
| **PagedAttention** | 分页注意力 | 把 KV cache 按块管理，消除显存碎片 |
| **TP / PP / DP** | 张量/流水/数据并行 | 层内切 / 层间切 / 批切，通信量依次降低 |
| **ZeRO** | 零冗余优化器 | 把优化器状态/梯度/参数分片到各卡，省训练显存 |
| **Operator fusion** | 算子融合 | 把多个小算子合并，消除中间的 HBM 往返 |
| **Systolic array** | 脉动阵列 | 数据只在相邻 PE 间流动的二维乘加阵列 |
| **Safetensors** | — | 模型权重存储格式（张量字典，无代码执行风险） |
| **MFU-aware roofline** | — | 把实测 MFU 代入 Roofline 上界的修正模型 |

### 7.5 一句话总结

{{% admonition tip %}}
<strong>大语言模型在硬件上就是一件事：把 16 GB 的权重反复从 HBM 搬到 Tensor Core 去乘一遍 4096 维的向量。</strong>算力在三年里涨了 7 倍，带宽只涨了 4 倍，而这个任务本身只要求 1 FLOP/byte。  
差距不是工程问题，是物理问题。所有推理优化——量化、批处理、GQA、FlashAttention、投机解码、SRAM 架构——都是在**减少字节数**或**提高每个字节的复用次数**这两件事之间做选择。理解这一点，你就有了评估任何新模型、新芯片、新论文的独立判断能力。
{{% /admonition %}}

## 参考文献

{{< rawhtml >}}
<ol class="refs">
<li id="r1">NVIDIA. <em>NVIDIA H100 Tensor Core GPU Architecture Whitepaper</em>, 2022；及 <em>H100 Datasheet</em>. （注意：公开规格表多列稀疏值，稠密值见白皮书 Table 1）</li>
<li id="r2">Zhang, B., Sennrich, R. <em>Root Mean Square Layer Normalization</em>. NeurIPS 2019.</li>
<li id="r3">Ainslie, J. et al. <em>GQA: Training Generalized Multi-Query Transformer Models from Multi-Head Checkpoints</em>. EMNLP 2023.</li>
<li id="r4">Su, J. et al. <em>RoFormer: Enhanced Transformer with Rotary Position Embedding</em>. Neurocomputing, 2024（arXiv 2021）.</li>
<li id="r5">Shazeer, N. <em>GLU Variants Improve Transformer</em>. arXiv:2002.05202, 2020.</li>
<li id="r6">Williams, S., Waterman, A., Patterson, D. <em>Roofline: An Insightful Visual Performance Model for Multicore Architectures</em>. CACM, 2009.</li>
<li id="r7">Dao, T., Fu, D., Ermon, S., et al. <em>FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness</em>. NeurIPS 2022.</li>
<li id="r8">DeepSeek-AI. <em>DeepSeek-V2: A Strong, Economical, and Efficient Mixture-of-Experts Language Model</em>. 2024.</li>
<li id="r9">Leviathan, Y., Kalman, M., Matias, Y. <em>Fast Inference from Transformers via Speculative Decoding</em>. ICML 2023.</li>
<li id="r10">Groq Inc. <em>GroqChip™ Processor Product Brief v1.7</em>, 2024.</li>
<li id="r11">Dubey, A. et al. <em>The Llama 3 Herd of Models</em>. 2024.（模型配置与训练 token 数的来源）</li>
<li id="r12">Korthikanti, V. et al. <em>Reducing Activation Recomputation in Large Transformer Models</em>. MLSys 2023.</li>
<li id="r13">Pope, R. et al. <em>Efficiently Scaling Transformer Inference</em>. MLSys 2023.（并行策略与带宽需求分析的经典参考）</li>
<li id="r14">Kaplan, J. et al. <em>Scaling Laws for Neural Language Models</em>. 2020.（<code>6ND</code> 的出处）</li>
<li id="r15">Horowitz, M. <em>1.1 Computing's Energy Problem</em>. ISSCC 2014.（数据搬运与计算的能耗对比数据）</li>
</ol>
{{< /rawhtml >}}

---

全部数值均按本文给出的公式推导，可用 §7.1 的脚本复现。硬件规格取自厂商公开文档；未标注来源的性能数字为理论上限（100% 带宽 / 100% MFU），实测通常需要打 40–60% 的折扣。若发现任何数字与你的复算不符，以复算为准并请回查单位与精度定义。
