#ifndef CUDA_EXAMPLE_KERNELS_H_
#define CUDA_EXAMPLE_KERNELS_H_

/* -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2014 Stanford University and the Authors.           *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "ExampleKernels.h"
#include "openmm/cuda/CudaContext.h"
#include "openmm/cuda/CudaArray.h"

// #include "CudaPlatform.h"
#include "openmm/cuda/CudaFFT3D.h"
#include "openmm/cuda/CudaParameterSet.h"
#include "openmm/cuda/CudaSort.h"
#include <cufft.h>

namespace ExamplePlugin {

/**
 * This kernel is invoked by ExampleForce to calculate the forces acting on the system and the energy of the system.
 */
class CudaCalcExampleForceKernel : public CalcExampleForceKernel {
public:
    CudaCalcExampleForceKernel(std::string name, const OpenMM::Platform& platform, OpenMM::CudaContext& cu, const OpenMM::System& system) :
            CalcExampleForceKernel(name, platform), hasInitializedKernel(false), cu(cu), system(system), params(NULL) {
    }
    ~CudaCalcExampleForceKernel();
    /**
     * Initialize the kernel.
     * 
     * @param system     the System this kernel will be applied to
     * @param force      the ExampleForce this kernel will be used for
     */
    void initialize(const OpenMM::System& system, const ExampleForce& force);
    /**
     * Execute the kernel to calculate the forces and/or energy.
     *
     * @param context        the context in which to execute this kernel
     * @param includeForces  true if forces should be calculated
     * @param includeEnergy  true if the energy should be calculated
     * @return the potential energy due to the force
     */
    double execute(OpenMM::ContextImpl& context, bool includeForces, bool includeEnergy);
    /**
     * Copy changed parameters over to a context.
     *
     * @param context    the context to copy parameters to
     * @param force      the ExampleForce to copy the parameters from
     */
    void copyParametersToContext(OpenMM::ContextImpl& context, const ExampleForce& force);
private:
    int numBonds;
    bool hasInitializedKernel;
    OpenMM::CudaContext& cu;
    const OpenMM::System& system;
    OpenMM::CudaArray* params;
};

/**
 * This kernel is invoked by NonbondedForce to calculate the forces acting on the system.
 */
class CudaCalcNonbondedForceKernel : public CalcNonbondedForceKernel {
public:
    CudaCalcNonbondedForceKernel(std::string name, const OpenMM::Platform& platform, OpenMM::CudaContext& cu, const OpenMM::System& system) : CalcNonbondedForceKernel(name, platform),
            cu(cu), hasInitializedFFT(false), sort(NULL), dispersionFft(NULL), fft(NULL), pmeio(NULL), usePmeStream(false) {
    }
    ~CudaCalcNonbondedForceKernel();
    /**
     * Initialize the kernel.
     *
     * @param system     the System this kernel will be applied to
     * @param force      the NonbondedForce this kernel will be used for
     */
    void initialize(const OpenMM::System& system, const NonbondedForce& force);
    /**
     * Execute the kernel to calculate the forces and/or energy.
     *
     * @param context        the context in which to execute this kernel
     * @param includeForces  true if forces should be calculated
     * @param includeEnergy  true if the energy should be calculated
     * @param includeDirect  true if direct space interactions should be included
     * @param includeReciprocal  true if reciprocal space interactions should be included
     * @return the potential energy due to the force
     */
    double execute(OpenMM::ContextImpl& context, bool includeForces, bool includeEnergy, bool includeDirect, bool includeReciprocal);
    /**
     * Copy changed parameters over to a context.
     *
     * @param context    the context to copy parameters to
     * @param force      the NonbondedForce to copy the parameters from
     */
    void copyParametersToContext(OpenMM::ContextImpl& context, const NonbondedForce& force);
    /**
     * Get the parameters being used for PME.
     * 
     * @param alpha   the separation parameter
     * @param nx      the number of grid points along the X axis
     * @param ny      the number of grid points along the Y axis
     * @param nz      the number of grid points along the Z axis
     */
    void getPMEParameters(double& alpha, int& nx, int& ny, int& nz) const;
    /**
     * Get the dispersion parameters being used for the dispersion term in LJPME.
     * 
     * @param alpha   the separation parameter
     * @param nx      the number of grid points along the X axis
     * @param ny      the number of grid points along the Y axis
     * @param nz      the number of grid points along the Z axis
     */
    void getLJPMEParameters(double& alpha, int& nx, int& ny, int& nz) const;
private:
    class SortTrait : public OpenMM::CudaSort::SortTrait {
        int getDataSize() const {return 8;}
        int getKeySize() const {return 4;}
        const char* getDataType() const {return "int2";}
        const char* getKeyType() const {return "int";}
        const char* getMinKey() const {return "(-2147483647-1)";}
        const char* getMaxKey() const {return "2147483647";}
        const char* getMaxValue() const {return "make_int2(2147483647, 2147483647)";}
        const char* getSortKey() const {return "value.y";}
    };
    class ForceInfo;
    class PmeIO;
    class PmePreComputation;
    class PmePostComputation;
    class SyncStreamPreComputation;
    class SyncStreamPostComputation;
    OpenMM::CudaContext& cu;
    ForceInfo* info;
    bool hasInitializedFFT;
    OpenMM::CudaArray charges;
    OpenMM::CudaArray sigmaEpsilon;
    OpenMM::CudaArray exceptionParams;
    OpenMM::CudaArray exclusionAtoms;
    OpenMM::CudaArray exclusionParams;
    OpenMM::CudaArray baseParticleParams;
    OpenMM::CudaArray baseExceptionParams;
    OpenMM::CudaArray particleParamOffsets;
    OpenMM::CudaArray exceptionParamOffsets;
    OpenMM::CudaArray particleOffsetIndices;
    OpenMM::CudaArray exceptionOffsetIndices;
    OpenMM::CudaArray globalParams;
    OpenMM::CudaArray cosSinSums;
    OpenMM::CudaArray pmeGrid1;
    OpenMM::CudaArray pmeGrid2;
    OpenMM::CudaArray pmeBsplineModuliX;
    OpenMM::CudaArray pmeBsplineModuliY;
    OpenMM::CudaArray pmeBsplineModuliZ;
    OpenMM::CudaArray pmeDispersionBsplineModuliX;
    OpenMM::CudaArray pmeDispersionBsplineModuliY;
    OpenMM::CudaArray pmeDispersionBsplineModuliZ;
    OpenMM::CudaArray pmeAtomGridIndex;
    OpenMM::CudaArray pmeEnergyBuffer;
    OpenMM::CudaSort* sort;
    OpenMM::Kernel cpuPme;
    PmeIO* pmeio;
    CUstream pmeStream;
    CUevent pmeSyncEvent, paramsSyncEvent;
    OpenMM::CudaFFT3D* fft;
    cufftHandle fftForward;
    cufftHandle fftBackward;
    OpenMM::CudaFFT3D* dispersionFft;
    cufftHandle dispersionFftForward;
    cufftHandle dispersionFftBackward;
    CUfunction computeParamsKernel, computeExclusionParamsKernel;
    CUfunction ewaldSumsKernel;
    CUfunction ewaldForcesKernel;
    CUfunction pmeGridIndexKernel;
    CUfunction pmeDispersionGridIndexKernel;
    CUfunction pmeSpreadChargeKernel;
    CUfunction pmeDispersionSpreadChargeKernel;
    CUfunction pmeFinishSpreadChargeKernel;
    CUfunction pmeDispersionFinishSpreadChargeKernel;
    CUfunction pmeEvalEnergyKernel;
    CUfunction pmeEvalDispersionEnergyKernel;
    CUfunction pmeConvolutionKernel;
    CUfunction pmeDispersionConvolutionKernel;
    CUfunction pmeInterpolateForceKernel;
    CUfunction pmeInterpolateDispersionForceKernel;
    std::vector<std::pair<int, int> > exceptionAtoms;
    std::vector<std::string> paramNames;
    std::vector<double> paramValues;
    double ewaldSelfEnergy, dispersionCoefficient, alpha, dispersionAlpha;
    int interpolateForceThreads;
    int gridSizeX, gridSizeY, gridSizeZ;
    int dispersionGridSizeX, dispersionGridSizeY, dispersionGridSizeZ;
    bool hasCoulomb, hasLJ, usePmeStream, useCudaFFT, doLJPME, usePosqCharges, recomputeParams, hasOffsets;
    NonbondedMethod nonbondedMethod;
    static const int PmeOrder = 5;
};

} // namespace ExamplePlugin

#endif /*CUDA_EXAMPLE_KERNELS_H_*/
