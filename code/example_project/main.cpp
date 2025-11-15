// 这是一个示例C++文件
// 用于演示代码链接功能

#include <iostream>
#include <vector>
#include <algorithm>

class DataProcessor {
private:
    std::vector<int> data;
    
public:
    DataProcessor() {}
    
    void addValue(int value) {
        data.push_back(value);
    }
    
    // 计算平均值
    double calculateAverage() {
        if (data.empty()) return 0.0;
        
        int sum = 0;
        for (int val : data) {
            sum += val;
        }
        
        return static_cast<double>(sum) / data.size();
    }
    
    // 查找最大值
    int findMax() {
        if (data.empty()) return 0;
        return *std::max_element(data.begin(), data.end());
    }
    
    // 排序数据
    void sortData() {
        std::sort(data.begin(), data.end());
    }
    
    void printData() {
        std::cout << "Data: ";
        for (int val : data) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
};

int main() {
    DataProcessor processor;
    
    // 添加一些测试数据
    processor.addValue(42);
    processor.addValue(17);
    processor.addValue(89);
    processor.addValue(23);
    processor.addValue(56);
    
    std::cout << "原始数据:" << std::endl;
    processor.printData();
    
    std::cout << "平均值: " << processor.calculateAverage() << std::endl;
    std::cout << "最大值: " << processor.findMax() << std::endl;
    
    processor.sortData();
    std::cout << "排序后数据:" << std::endl;
    processor.printData();
    
    return 0;
}