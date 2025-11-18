/*
 * Copyright (c) 2015, University of Kaiserslautern
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Matthias Jung
 */

#include <stdio.h>

// 声明获取core id的汇编函数
extern "C" unsigned int get_core_id();

// SimpleMatrix寄存器地址定义
#define SIMPLE_MATRIX_BASE_ADDR    0x2F000000
#define MATRIX_A_BASE              (SIMPLE_MATRIX_BASE_ADDR + 0x1000)  // 0x2F001000
#define MATRIX_B_BASE              (SIMPLE_MATRIX_BASE_ADDR + 0x2000)  // 0x2F002000
#define RESULT_BASE                (SIMPLE_MATRIX_BASE_ADDR + 0x3000)  // 0x2F003000
#define CONFIG_REG                 (SIMPLE_MATRIX_BASE_ADDR + 0x4000)  // 0x2F004000
#define STATUS_REG                 (SIMPLE_MATRIX_BASE_ADDR + 0x4004)  // 0x2F004004

// 状态寄存器位定义
#define STATUS_BUSY_BIT   0x1  // Bit 0: 计算进行中标志
#define STATUS_DONE_BIT   0x2  // Bit 1: 计算完成标志

// 矩阵维度
#define MATRIX_ROWS 16
#define MATRIX_COLS 16
#define MATRIX_SIZE (MATRIX_ROWS * MATRIX_COLS)

// 简单的内存映射I/O函数
volatile unsigned char* get_reg_ptr8(unsigned int addr) {
    return (volatile unsigned char*)addr;
}

volatile unsigned int* get_reg_ptr32(unsigned int addr) {
    return (volatile unsigned int*)addr;
}

// 测试寄存器可访问性的函数
void test_register_access() {
    printf("开始测试SimpleMatrix寄存器可访问性...\n");
    
    volatile unsigned char* matrix_a = get_reg_ptr8(MATRIX_A_BASE);
    volatile unsigned char* matrix_b = get_reg_ptr8(MATRIX_B_BASE);
    volatile unsigned int* result = get_reg_ptr32(RESULT_BASE);
    volatile unsigned char* config = get_reg_ptr8(CONFIG_REG);
    volatile unsigned char* status = get_reg_ptr8(STATUS_REG);
    
    printf("测试地址范围:\n");
    printf("Matrix A: 0x%X\n", MATRIX_A_BASE);
    printf("Matrix B: 0x%X\n", MATRIX_B_BASE);
    printf("Result: 0x%X\n", RESULT_BASE);
    printf("Config: 0x%X\n", CONFIG_REG);
    printf("Status: 0x%X\n", STATUS_REG);
    
    // 测试状态寄存器读取
    printf("\n1. 测试状态寄存器读取...\n");
    unsigned char status_val = *status;
    printf("状态寄存器值: 0x%02X\n", status_val);
    
    // 测试配置寄存器读写
    printf("\n2. 测试配置寄存器读写...\n");
    *config = 0xAB;
    // unsigned char config_read = *config;
    // printf("写入: 0xAB, 读取: 0x%02X\n", config_read);
    
    // 测试Matrix A缓冲区读写
    printf("\n3. 测试Matrix A缓冲区读写...\n");
    matrix_a[0] = 0xDE;
    matrix_a[1] = 0xAD;
    matrix_a[2] = 0xBE;
    matrix_a[3] = 0xEF;
    unsigned char a0 = matrix_a[0];
    unsigned char a1 = matrix_a[1];
    unsigned char a2 = matrix_a[2];
    unsigned char a3 = matrix_a[3];
    printf("写入[0]: 0xDE, 读取: 0x%02X\n", a0);
    printf("写入[1]: 0xAD, 读取: 0x%02X\n", a1);
    printf("写入[2]: 0xBE, 读取: 0x%02X\n", a2);
    printf("写入[3]: 0xEF, 读取: 0x%02X\n", a3);
    
    // 测试Matrix B缓冲区读写
    printf("\n4. 测试Matrix B缓冲区读写...\n");
    matrix_b[0] = 0x12;
    matrix_b[1] = 0x34;
    matrix_b[2] = 0x56;
    matrix_b[3] = 0x78;
    unsigned char b0 = matrix_b[0];
    unsigned char b1 = matrix_b[1];
    unsigned char b2 = matrix_b[2];
    unsigned char b3 = matrix_b[3];
    printf("写入[0]: 0x12, 读取: 0x%02X\n", b0);
    printf("写入[1]: 0x34, 读取: 0x%02X\n", b1);
    printf("写入[2]: 0x56, 读取: 0x%02X\n", b2);
    printf("写入[3]: 0x78, 读取: 0x%02X\n", b3);
    
    // 测试Result缓冲区读取
    printf("\n5. 测试Result缓冲区读取...\n");
    unsigned int r0 = result[0];
    unsigned int r1 = result[1];
    unsigned int r2 = result[2];
    unsigned int r3 = result[3];
    printf("读取[0]: 0x%08X\n", r0);
    printf("读取[1]: 0x%08X\n", r1);
    printf("读取[2]: 0x%08X\n", r2);
    printf("读取[3]: 0x%08X\n", r3);
    
    // 测试Result缓冲区写入（应该是只读的）
    // printf("\n6. 测试Result缓冲区写入（应该是只读的）...\n");
    // result[0] = 0xAA;
    // unsigned char r0_after = result[0];
    // printf("尝试写入[0]: 0xAA, 读取: 0x%02X\n", r0_after);
    
    printf("\n寄存器可访问性测试完成！\n");
}

// 测试SimpleMatrix矩阵运算加速器的函数
void test_simple_matrix() {
    printf("开始测试SimpleMatrix矩阵运算加速器...\n");
    
    volatile unsigned char* matrix_a = get_reg_ptr8(MATRIX_A_BASE);
    volatile unsigned char* matrix_b = get_reg_ptr8(MATRIX_B_BASE);
    volatile unsigned int* result = get_reg_ptr32(RESULT_BASE);
    volatile unsigned char* config = get_reg_ptr8(CONFIG_REG);
    volatile unsigned char* status = get_reg_ptr8(STATUS_REG);
    
    // 检查初始状态
    printf("初始状态寄存器值: 0x%02X\n", *status);
    
    // 初始化矩阵A和B
    printf("初始化矩阵数据...\n");
    for (int i = 0; i < MATRIX_SIZE; i++) {
        // 矩阵A: 简单的递增值 (0-255循环)
        matrix_a[i] = (i % 256) + 1;
        
        // 矩阵B: 简单的递减值 (255-0循环)
        matrix_b[i] = 255 - (i % 256);
    }
    
    // 启动矩阵运算 - 写入任意值到配置寄存器
    printf("启动矩阵运算...\n");
    *config = 1;  // 写入任意值启动计算
    
    // 等待运算完成 - 检查完成标志位
    printf("等待运算完成...\n");
    while ((*status & STATUS_DONE_BIT) == 0) {
        // 等待状态寄存器的完成位被设置
        if (*status & STATUS_BUSY_BIT) {
            printf("计算进行中...\n");
        }
    }
    
    printf("最终状态寄存器值: 0x%02X\n", *status);
    
    // 检查结果
    printf("矩阵运算完成！检查结果...\n");
    int error_count = 0;
    int checked_count = 0;
    
    // 检查前几个元素
    for (int i = 0; i < 4 && i < MATRIX_ROWS; i++) {   // 只检查前4行
        for (int j = 0; j < 4 && j < MATRIX_COLS; j++) {   // 只检查前4列
            unsigned int expected = 0;
            // 计算期望值: A[i][k] * B[k][j] 的和
            for (int k = 0; k < MATRIX_COLS; k++) {
                unsigned char a_val = matrix_a[i * MATRIX_COLS + k];
                unsigned char b_val = matrix_b[k * MATRIX_COLS + j];
                expected += (unsigned int)a_val * (unsigned int)b_val;
            }
            
            unsigned int actual = result[i * MATRIX_COLS + j];
            checked_count++;
            
            printf("result[%d][%d] = %u, 期望值 = %u\n", i, j, actual, expected);
            
            if (actual != expected) {
                printf("错误: result[%d][%d] = %u, 期望值 = %u\n", i, j, actual, expected);
                error_count++;
            }
        }
    }
    
    if (error_count == 0) {
        printf("SimpleMatrix测试成功！检查了 %d 个元素，结果正确。\n", checked_count);
    } else {
        printf("SimpleMatrix测试失败！检查了 %d 个元素，发现 %d 个错误。\n", checked_count, error_count);
    }
}

int main(void)
{
    unsigned int core_id = get_core_id();
    unsigned int r = 1337;

    // 简单的延迟，使不同核心的输出有时间差
    for(int i=0;i<1000*core_id;i++);

    printf("Hello World from Core %d! Value: %d\n", core_id, r);
    
    // 只有核心0执行测试
    if (core_id == 0) {
        printf("\n=== 核心0开始测试 ===\n");
        
        // 先测试寄存器可访问性
        test_register_access();
        
        printf("\n=== 寄存器测试完成，开始矩阵运算测试 ===\n");
        
        // 然后测试矩阵运算
        test_simple_matrix();
    }

	while (1)
	{
	}
}

