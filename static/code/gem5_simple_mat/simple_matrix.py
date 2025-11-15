# Copyright (c) 2025
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.objects.Device import BasicPioDevice
from m5.params import *
from m5.proxy import *


class SimpleMatrix(BasicPioDevice):
    type = "SimpleMatrix"
    cxx_header = "leru/simple_mat/simple_matrix.hh"
    cxx_class = "gem5::SimpleMatrix"

    # 内部缓存大小
    buffer_size = Param.MemorySize("32KiB", "The size of internal buffer for matrix data")

    # 计算带宽（使用浮点数表示，单位：字节/周期）
    compute_bandwidth = Param.MemoryBandwidth ("16GiB/s", "Computation bandwidth in bytes per cycle")

    # 流水线延迟（周期数）
    pipeline_latency = Param.Cycles(5, "Pipeline latency in cycles")

    # 系统参数
    system = Param.System(Parent.any, "The system this matrix unit is part of")

    # 矩阵维度参数
    matrix_rows = Param.Unsigned(16, "Number of rows in the matrix")
    matrix_cols = Param.Unsigned(16, "Number of columns in the matrix")

    # 内存映射地址参数
    matrix_a_base = Param.Addr(0x1000, "Base address for matrix A")
    matrix_b_base = Param.Addr(0x2000, "Base address for matrix B")
    result_base = Param.Addr(0x3000, "Base address for result matrix")
    config_reg = Param.Addr(0x4000, "Configuration register address")
    status_reg = Param.Addr(0x4004, "Status register address")