---
title: "一些例子"
date: 2024-01-15
draft: false
tags: ['shortcode']
---

字体测试：
*这是斜体* _这也是斜体_ **加粗！** ***又粗又斜！***  ~~删掉的字~~

列表：
- 这是列表
- 的第二项
- 第三项

多级列表：
- 第一级
  - 第二级
    - 第三级

> 区块引用，拿来做背景什么的不错
>> 可以多级
>>> 这是第三级

代码块：
```verilog
//居然可以支持verilog！
module hello_world (
    input clk,
    input rst_n,
    output reg [7:0] led
);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        led <= 8'b00000000;
    end else begin
        led <= led + 1'b1;
    end
end

endmodule
```
内嵌公式，支持latex语法！
例如：$E=mc^2$

还有独占一行的公式！

$$
F(x) = \int_{-\infty}^\infty e^{-x^2} dx
$$





这是一张图，放在/static/img目录下面的，不知道为什么，用figure短代码显示不出来：

![测试图片](/img/test_photo.jpg)

这是一张图，和这个md文档放一起的，这个就能显示出来：

{{< figure src="/example/test.jpg" alt="测试图片" caption="这是一张测试图片" >}}

这里引用一篇关于NoC的论文：{{< cite xuRHTNoCReconfigurable2025 >}}

这里再随便引用一篇论文： {{< cite zhengAdaptNoCFlexibleNetworkonChip2021 >}}

然后就可以生成参考文献列表了，这个参考文献列表有这一页的参考文献：
{{< references >}}

这个参考文献列表有现在所有的参考文献：
{{< references all="true" >}}


## Admonition 提示框示例

### 默认提示框（note类型）
{{< admonition >}}
这是一个默认的提示框，类型为note。
{{< /admonition >}}

### 自定义标题的提示框
{{< admonition note "自定义标题" >}}
这是一个带有自定义标题的提示框。
{{< /admonition >}}

### 提示（tip类型）
{{< admonition tip "提示" >}}
这里可以放一些提示信息，帮助用户更好地理解内容。
{{< /admonition >}}

### 警告（warning类型）
{{< admonition warning "警告" >}}
这里可以放一些警告信息，提醒用户注意潜在问题。
{{< /admonition >}}

### 危险（danger类型）
{{< admonition danger "危险" >}}
这里可以放一些危险信息，提示用户避免错误操作。
{{< /admonition >}}

### 重要信息（important类型）
{{< admonition important "重要" >}}
这里可以放一些重要信息，需要用户特别关注。
{{< /admonition >}}

### 问题（question类型）
{{< admonition question "问题" >}}
这里可以放一些问题或FAQ内容。

这样可以吗？
{{< /admonition >}}

