---
title: "代码链接功能演示"
date: 2024-01-15T10:00:00+08:00
draft: false
tags: ['shortcode']
categories: ['网站开发']
series: ['Hugo功能开发']
---

# 代码链接功能演示

这个页面演示了如何使用 `codelink` shortcode 来链接到存储在 `static/code/` 目录中的代码文件。

## 功能说明

通过 `codelink` shortcode，你可以：

1. 链接到项目中的特定代码文件
2. 指定显示的文本
3. 可选地链接到特定行号

## 使用方法

### 基本用法

```markdown
{{</* codelink path="example_project/main.cpp" */>}}
```

效果：{{< codelink path="example_project/main.cpp" >}}

### 自定义显示文本

```markdown
{{</* codelink path="example_project/main.cpp" text="主程序文件" */>}}
```

效果：{{< codelink path="example_project/main.cpp" text="主程序文件" >}}

### 链接到配置文件

```markdown
{{</* codelink path="example_project/config.toml" text="项目配置" */>}}
```

效果：{{< codelink path="example_project/config.toml" text="项目配置" >}}

### 链接到特定行号

```markdown
{{</* codelink path="example_project/main.cpp" line="25" text="calculateAverage方法" */>}}
```

效果：{{< codelink path="example_project/main.cpp" line="25" text="calculateAverage方法" >}}

### 深层目录结构示例

```markdown
{{</* codelink path="ml/models/neural_networks/cnn.py" text="CNN模型实现" */>}}
```

效果：{{< codelink path="ml/models/neural_networks/cnn.py" text="CNN模型实现" >}}

## 目录结构

代码文件存储在 `static/code/` 目录中，支持任意深度的目录结构：

```
static/
└── code/
    ├── example_project/
    │   ├── main.cpp
    │   └── config.toml
    ├── data_analysis/
    │   ├── analyzer.py
    │   └── README.md
    ├── ml/
    │   └── models/
    │       └── neural_networks/
    │           ├── cnn.py
    │           └── rnn.py
    └── scripts/
        ├── build.sh
        └── deploy.sh
```

## 参数说明

- `path` (必需): 相对于 `static/code/` 的路径，支持深层目录结构
- `text` (可选): 链接显示的文本，默认为路径
- `line` (可选): 行号，会在URL中添加锚点

## 示例场景

在技术文档中，你可以这样使用：

> 我们在 {{< codelink path="example_project/main.cpp" text="主程序" >}} 中实现了数据处理功能，特别是 {{< codelink path="example_project/main.cpp" line="25" text="calculateAverage方法" >}} 用于计算平均值。

这样可以让读者直接点击链接查看相关的代码实现。

## 更多示例

### 数据分析项目

链接到数据分析脚本：
```markdown
{{</* codelink path="data_analysis/analyzer.py" text="数据分析器" */>}}
```
效果：{{< codelink path="data_analysis/analyzer.py" text="数据分析器" >}}

链接到项目说明文档：
```markdown
{{</* codelink path="data_analysis/README.md" text="项目文档" */>}}
```
效果：{{< codelink path="data_analysis/README.md" text="项目文档" >}}

### 链接到特定行号

链接到数据分析器的构造函数：
```markdown
{{</* codelink path="data_analysis/analyzer.py" line="8" text="DataAnalyzer构造函数" */>}}
```
效果：{{< codelink path="data_analysis/analyzer.py" line="8" text="DataAnalyzer构造函数" >}}

链接到数据加载方法：
```markdown
{{</* codelink path="data_analysis/analyzer.py" line="15" text="load_data方法" */>}}
```
效果：{{< codelink path="data_analysis/analyzer.py" line="15" text="load_data方法" >}}

### 深层目录结构示例

链接到机器学习模型：
```markdown
{{</* codelink path="ml/models/neural_networks/cnn.py" text="CNN模型" */>}}
```
效果：{{< codelink path="ml/models/neural_networks/cnn.py" text="CNN模型" >}}

链接到构建脚本：
```markdown
{{</* codelink path="scripts/build.sh" text="构建脚本" */>}}
```
效果：{{< codelink path="scripts/build.sh" text="构建脚本" >}}

## 注意事项

1. 确保代码文件确实存在于 `static/code/` 目录中
2. 路径区分大小写（在某些系统中）
3. 使用正斜杠 `/` 作为路径分隔符
4. 如果指定的文件不存在，链接会指向404页面
5. 行号锚点需要浏览器支持才能正常跳转
6. 支持任意深度的目录结构