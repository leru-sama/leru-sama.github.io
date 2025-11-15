import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import train_test_split

class DataAnalyzer:
    def __init__(self, file_path):
        """初始化数据分析器"""
        self.file_path = file_path
        self.data = None
        self.scaler = StandardScaler()
        
    def load_data(self):
        """加载数据"""
        try:
            self.data = pd.read_csv(self.file_path)
            print(f"数据加载成功，共 {len(self.data)} 行，{len(self.data.columns)} 列")
            return True
        except Exception as e:
            print(f"数据加载失败: {e}")
            return False
    
    def clean_data(self):
        """数据清洗"""
        if self.data is None:
            print("请先加载数据")
            return False
            
        # 删除重复行
        initial_count = len(self.data)
        self.data = self.data.drop_duplicates()
        print(f"删除了 {initial_count - len(self.data)} 行重复数据")
        
        # 处理缺失值
        missing_count = self.data.isnull().sum().sum()
        if missing_count > 0:
            print(f"发现 {missing_count} 个缺失值")
            # 对于数值列，用均值填充
            numeric_columns = self.data.select_dtypes(include=[np.number]).columns
            self.data[numeric_columns] = self.data[numeric_columns].fillna(
                self.data[numeric_columns].mean()
            )
            print("缺失值已用均值填充")
        
        return True
    
    def basic_statistics(self):
        """基本统计分析"""
        if self.data is None:
            print("请先加载数据")
            return
            
        print("\n=== 基本统计信息 ===")
        print(self.data.describe())
        
        print("\n=== 数据类型 ===")
        print(self.data.dtypes)
        
        print("\n=== 缺失值统计 ===")
        print(self.data.isnull().sum())
    
    def visualize_data(self, column=None):
        """数据可视化"""
        if self.data is None:
            print("请先加载数据")
            return
            
        if column is None:
            # 如果没有指定列，选择第一个数值列
            numeric_columns = self.data.select_dtypes(include=[np.number]).columns
            if len(numeric_columns) > 0:
                column = numeric_columns[0]
            else:
                print("没有找到数值列")
                return
        
        plt.figure(figsize=(10, 6))
        
        # 直方图
        plt.subplot(1, 2, 1)
        plt.hist(self.data[column].dropna(), bins=30, alpha=0.7)
        plt.title(f'{column} 分布')
        plt.xlabel(column)
        plt.ylabel('频次')
        
        # 箱线图
        plt.subplot(1, 2, 2)
        plt.boxplot(self.data[column].dropna())
        plt.title(f'{column} 箱线图')
        
        plt.tight_layout()
        plt.show()
    
    def prepare_features(self, target_column):
        """准备特征数据"""
        if self.data is None:
            print("请先加载数据")
            return None, None
            
        # 分离特征和目标
        X = self.data.drop(columns=[target_column])
        y = self.data[target_column]
        
        # 只对数值特征进行标准化
        numeric_columns = X.select_dtypes(include=[np.number]).columns
        X[numeric_columns] = self.scaler.fit_transform(X[numeric_columns])
        
        return X, y
    
    def split_data(self, X, y, test_size=0.2, random_state=42):
        """分割训练集和测试集"""
        return train_test_split(X, y, test_size=test_size, random_state=random_state)

# 使用示例
if __name__ == "__main__":
    # 创建分析器实例
    analyzer = DataAnalyzer("data.csv")
    
    # 加载数据
    if analyzer.load_data():
        # 数据清洗
        analyzer.clean_data()
        
        # 基本统计
        analyzer.basic_statistics()
        
        # 可视化（如果有数据的话）
        # analyzer.visualize_data()
        
        print("数据分析完成！")