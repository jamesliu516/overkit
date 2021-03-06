// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_SUPPORT_DECOMP_H_LOADED
#define OVK_SUPPORT_DECOMP_H_LOADED

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

void support_CreateCartesianDecompDims(int Size, int NumDims, int *Dims);

void support_DecomposeDomain(int NumGrids, const long long *NumPointsPerGrid, int NumProcs, int
  *GridProcRanges);

void support_CartesianDecomp(int NumDims, const int *GlobalBegin, const int *GlobalEnd, MPI_Comm
  CartComm, int *LocalBegin, int *LocalEnd);
void support_TriangularDecomp(int NumDims, const int *GlobalBegin, const int *GlobalEnd, MPI_Comm
  Comm, int *LocalBegin, int *LocalEnd);

#ifdef __cplusplus
}
#endif

#endif
