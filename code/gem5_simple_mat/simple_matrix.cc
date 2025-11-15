/*
 * Copyright (c) 2025
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "leru/simple_mat/simple_matrix.hh"

#include "base/compiler.hh"
#include "debug/SimpleMatrix.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

SimpleMatrix::SimpleMatrix(const SimpleMatrixParams &params) :
    BasicPioDevice(params, 0x5000),  // PIO range size
    pipelineLatency(params.pipeline_latency),
    matrixRows(params.matrix_rows),
    matrixCols(params.matrix_cols),
    matrixSize(params.matrix_rows * params.matrix_cols),
    matrixABase(params.matrix_a_base),
    matrixBBase(params.matrix_b_base),
    resultBase(params.result_base),
    configReg(params.config_reg),
    statusReg(params.status_reg),
    matrixA(matrixSize, 0),
    matrixB(matrixSize, 0),
    resultMatrix(matrixSize, 0),
    computationInProgress(false),
    computationComplete(false),
    stats(this)
{
    DPRINTF(SimpleMatrix, "SimpleMatrix initialized with %dx%d matrices\n", 
            matrixRows, matrixCols);
}

Tick
SimpleMatrix::read(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Read request for addr %#x\n", pkt->getAddr());

    // Handle the read request based on the address (relative to pio_addr)
    Addr addr = pkt->getAddr() - pioAddr;
    
    if (addr >= matrixABase && addr < matrixBBase) {
        // Read from matrix A
        readMatrixA(pkt);
    } else if (addr >= matrixBBase && addr < resultBase) {
        // Read from matrix B
        readMatrixB(pkt);
    } else if (addr >= resultBase && addr < configReg) {
        // Read from result matrix
        readResultMatrix(pkt);
    } else if (addr == statusReg) {
        // Read status register
        readStatusRegister(pkt);
    } else if (addr == configReg) {
        // Config register is write-only, reject read
        DPRINTF(SimpleMatrix, "Attempt to read write-only config register\n");
        pkt->makeResponse();
        pkt->setBadAddress();
    } else {
        // Invalid address
        pkt->makeResponse();
        pkt->setBadAddress();
    }

    return 0;
}

Tick
SimpleMatrix::write(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Write request for addr %#x\n", pkt->getAddr());

    // Handle the write request based on the address (relative to pio_addr)
    Addr addr = pkt->getAddr() - pioAddr;
    
    if (addr >= matrixABase && addr < matrixBBase) {
        // Write to matrix A
        updateMatrixA(pkt);
    } else if (addr >= matrixBBase && addr < resultBase) {
        // Write to matrix B
        updateMatrixB(pkt);
    } else if (addr >= resultBase && addr < configReg) {
        // Result matrix is read-only, reject write
        DPRINTF(SimpleMatrix, "Attempt to write read-only result matrix\n");
        pkt->makeResponse();
        pkt->setBadAddress();
    } else if (addr == configReg) {
        // Write to configuration register to start computation
        startComputation(pkt);
    } else if (addr == statusReg) {
        // Status register is read-only, reject write
        DPRINTF(SimpleMatrix, "Attempt to write read-only status register\n");
        pkt->makeResponse();
        pkt->setBadAddress();
    } else {
        // Invalid address
        pkt->makeResponse();
        pkt->setBadAddress();
    }

    return 0;
}

void
SimpleMatrix::updateMatrixA(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Updating matrix A at offset %#x\n", 
            pkt->getAddr() - pioAddr - matrixABase);
    
    // Calculate the offset in the matrix
    uint32_t offset = (pkt->getAddr() - pioAddr - matrixABase) / sizeof(uint8_t);
    
    // Copy data to matrix A
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        const uint8_t* data = pkt->getConstPtr<uint8_t>();
        for (uint32_t i = 0; i < pkt->getSize()/sizeof(uint8_t); i++) {
            matrixA[offset + i] = data[i];
        }
    }
    
    pkt->makeResponse();
}

void
SimpleMatrix::updateMatrixB(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Updating matrix B at offset %#x\n", 
            pkt->getAddr() - pioAddr - matrixBBase);
    
    // Calculate the offset in the matrix
    uint32_t offset = (pkt->getAddr() - pioAddr - matrixBBase) / sizeof(uint8_t);
    
    // Copy data to matrix B
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        const uint8_t* data = pkt->getConstPtr<uint8_t>();
        for (uint32_t i = 0; i < pkt->getSize()/sizeof(uint8_t); i++) {
            matrixB[offset + i] = data[i];
        }
    }
    
    pkt->makeResponse();
}

void
SimpleMatrix::startComputation(PacketPtr pkt)
{
    DPRINTF(SimpleMatrix, "Starting matrix multiplication\n");
    
    if (computationInProgress) {
        DPRINTF(SimpleMatrix, "Computation already in progress\n");
        pkt->makeResponse();
        return;
    }
    
    // Set computation in progress
    computationInProgress = true;
    computationComplete = false;
    
    // Schedule the computation event
    schedule(new ComputationEvent(this), clockEdge(pipelineLatency));
    
    pkt->makeResponse();
}

void
SimpleMatrix::readMatrixA(PacketPtr pkt)
{
    Addr addr = pkt->getAddr() - pioAddr;
    
    // Read matrix A
    uint32_t offset = (addr - matrixABase) / sizeof(uint8_t);
    
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        pkt->setData((uint8_t*)(matrixA.data() + offset));
    }
    
    DPRINTF(SimpleMatrix, "Reading matrix A at offset %#x\n", offset);
    pkt->makeResponse();
}

void
SimpleMatrix::readMatrixB(PacketPtr pkt)
{
    Addr addr = pkt->getAddr() - pioAddr;
    
    // Read matrix B
    uint32_t offset = (addr - matrixBBase) / sizeof(uint8_t);
    
    if (offset + pkt->getSize()/sizeof(uint8_t) <= matrixSize) {
        pkt->setData((uint8_t*)(matrixB.data() + offset));
    }
    
    DPRINTF(SimpleMatrix, "Reading matrix B at offset %#x\n", offset);
    pkt->makeResponse();
}

void
SimpleMatrix::readResultMatrix(PacketPtr pkt)
{
    Addr addr = pkt->getAddr() - pioAddr;
    
    // Read result matrix (uint32_t elements)
    uint32_t offset = (addr - resultBase) / sizeof(uint32_t);
    
    if (offset + pkt->getSize()/sizeof(uint32_t) <= matrixSize) {
        pkt->setData((uint8_t*)(resultMatrix.data() + offset));
    }
    
    DPRINTF(SimpleMatrix, "Reading result matrix at offset %#x\n", offset);
    pkt->makeResponse();
}

void
SimpleMatrix::readStatusRegister(PacketPtr pkt)
{
    // Read status register
    uint32_t status = 0;
    if (computationInProgress) status |= 0x1;
    if (computationComplete) status |= 0x2;
    pkt->setData((uint8_t*)&status);
    
    DPRINTF(SimpleMatrix, "Reading status register: 0x%x\n", status);
    pkt->makeResponse();
}

void
SimpleMatrix::ComputationEvent::process()
{
    // Perform the actual computation
    matrix->performComputation();
}

void
SimpleMatrix::performComputation()
{
    DPRINTF(SimpleMatrix, "Performing matrix multiplication\n");
    
    // Perform the matrix multiplication
    matrixMultiply();
    
    // Update status
    computationInProgress = false;
    computationComplete = true;
    
    stats.computations++;
    stats.multiplications++;
    
    DPRINTF(SimpleMatrix, "Matrix multiplication completed\n");
}

void
SimpleMatrix::matrixMultiply()
{
    // Matrix multiplication: C = A * B (8-bit integer multiplication)
    for (uint32_t i = 0; i < matrixRows; i++) {
        for (uint32_t j = 0; j < matrixCols; j++) {
            uint32_t sum = 0;
            for (uint32_t k = 0; k < matrixCols; k++) {
                // Multiply two 8-bit numbers (result up to 16 bits)
                uint16_t product = matrixA[i * matrixCols + k] * matrixB[k * matrixCols + j];
                sum += product;
                stats.additions++;
            }
            // Store the full 32-bit result (no clamping needed)
            resultMatrix[i * matrixCols + j] = sum;
        }
    }
}

// ===========================================================================
// Statistics Implementation
// ===========================================================================

SimpleMatrix::SimpleMatrixStats::SimpleMatrixStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(computations, statistics::units::Count::get(), 
               "Number of matrix computations"),
      ADD_STAT(multiplications, statistics::units::Count::get(), 
               "Number of matrix multiplications"),
      ADD_STAT(additions, statistics::units::Count::get(),
               "Number of additions"),
      ADD_STAT(computationLatency, statistics::units::Tick::get(),
               "Ticks for matrix computations")
{
    computationLatency.init(16); // number of buckets
}

} // namespace gem5