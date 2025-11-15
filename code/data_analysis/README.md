# 数据分析项目

这个项目包含了数据分析的常用工具和脚本。

## 文件说明

- `analyzer.py`: 主要的数据分析类，包含数据加载、清洗、统计分析和可视化功能
- `requirements.txt`: 项目依赖包列表
- `README.md`: 项目说明文档

## 使用方法

```python
from analyzer import DataAnalyzer

# 创建分析器实例
analyzer = DataAnalyzer("your_data.csv")

# 加载数据
analyzer.load_data()

# 数据清洗
analyzer.clean_data()

# 基本统计分析
analyzer.basic_statistics()

# 数据可视化
analyzer.visualize_data("column_name")
```

## 功能特性

1. **数据加载**: 支持CSV格式数据文件
2. **数据清洗**: 自动处理重复值和缺失值
3. **统计分析**: 提供基本的描述性统计
4. **数据可视化**: 生成直方图和箱线图
5. **特征工程**: 数据标准化和训练集分割

## 依赖包

- pandas
- numpy
- matplotlib
- scikit-learn

安装方法：
```bash
pip install -r requirements.txt
```