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

#ifndef __LERU_SIMPLE_MAT_SIMPLE_MATRIX_HH__
#define __LERU_SIMPLE_MAT_SIMPLE_MATRIX_HH__

#include <vector>

#include "base/statistics.hh"
#include "params/SimpleMatrix.hh"
#include "dev/io_device.hh"
#include "sim/eventq.hh"

namespace gem5
{

/**
 * A simple matrix multiplication accelerator.
 * This unit has fixed-shape matrices stored in internal buffers.
 * Matrix A and B are updated through memory-mapped writes to specific addresses.
 * Matrix multiplication is triggered by writing to a configuration register.
 */
class SimpleMatrix : public BasicPioDevice
{
  public:
    PARAMS(SimpleMatrix);
    
    SimpleMatrix(const SimpleMatrixParams &params);
    
    /**
     * Handle a read from the device.
     * @param pkt The packet to handle
     * @return Tick delay
     */
    Tick read(PacketPtr pkt) override;
    
    /**
     * Handle a write to the device.
     * @param pkt The packet to handle
     * @return Tick delay
     */
    Tick write(PacketPtr pkt) override;

  private:
    /**
     * Event to handle the completion of matrix computation.
     */
    class ComputationEvent : public Event
    {
      private:
        SimpleMatrix *matrix;

      public:
        ComputationEvent(SimpleMatrix *_matrix) :
            Event(Default_Pri, AutoDelete), matrix(_matrix)
        { }

        void process() override;
        const char *description() const override
        { return "SimpleMatrix computation completion"; }
    };

    /**
     * Handle a memory write to update matrix A.
     */
    void updateMatrixA(PacketPtr pkt);

    /**
     * Handle a memory write to update matrix B.
     */
    void updateMatrixB(PacketPtr pkt);

    /**
     * Handle a memory write to the configuration register to start computation.
     */
    void startComputation(PacketPtr pkt);

    /**
     * Handle a memory read to read matrix A.
     */
    void readMatrixA(PacketPtr pkt);

    /**
     * Handle a memory read to read matrix B.
     */
    void readMatrixB(PacketPtr pkt);

    /**
     * Handle a memory read to read the result matrix.
     */
    void readResultMatrix(PacketPtr pkt);

    /**
     * Handle a memory read to read the status register.
     */
    void readStatusRegister(PacketPtr pkt);

    /**
     * Perform the actual matrix multiplication.
     */
    void performComputation();

    /**
     * Perform matrix multiplication: C = A * B
     */
    void matrixMultiply();

    /// Pipeline latency in cycles
    const Cycles pipelineLatency;

    /// Matrix dimensions from parameters
    const uint32_t matrixRows;
    const uint32_t matrixCols;
    const uint32_t matrixSize;

    /// Memory-mapped I/O addresses from parameters (relative to pio_addr)
    const Addr matrixABase;
    const Addr matrixBBase;
    const Addr resultBase;
    const Addr configReg;
    const Addr statusReg;

    /// Internal buffers for matrices
    std::vector<uint8_t> matrixA;
    std::vector<uint8_t> matrixB;
    std::vector<uint32_t> resultMatrix;

    /// Status register
    bool computationInProgress;
    bool computationComplete;

    /// Matrix computation statistics
  protected:
    struct SimpleMatrixStats : public statistics::Group
    {
        SimpleMatrixStats(statistics::Group *parent);
        statistics::Scalar computations;
        statistics::Scalar multiplications;
        statistics::Scalar additions;
        statistics::Histogram computationLatency;
    } stats;



};

} // namespace gem5

#endif // __LERU_SIMPLE_MAT_SIMPLE_MATRIX_HH__