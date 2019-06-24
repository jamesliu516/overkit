// Copyright (c) 2019 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#include <overkit.h>

#include "examples/Common.h"

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define min(a, b) ovk_min(a, b)
#define max(a, b) ovk_max(a, b)

#define Print(...) printf(__VA_ARGS__); fflush(stdout)

static void Interface();

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);

  Interface();

  MPI_Finalize();

  return 0;

}

typedef struct {
  MPI_Comm Comm;
  int Size[3];
  int LocalRange[6];
  long long NumLocalPoints;
  int ExtendedRange[6];
  long long NumExtendedPoints;
} grid_data;

static void CreateGridData(grid_data *Data) {

  Data->Comm = MPI_COMM_NULL;
  memset(Data->Size, 0, 3);
  Data->Size[2] = 1;
  memset(Data->LocalRange, 0, 6);
  Data->LocalRange[5] = 1;
  Data->NumLocalPoints = 0;
  memset(Data->ExtendedRange, 0, 6);
  Data->ExtendedRange[5] = 1;
  Data->NumExtendedPoints = 0;

}

static void DestroyGridData(grid_data *Data) {

  if (Data->Comm != MPI_COMM_NULL) {
    MPI_Comm_free(&Data->Comm);
  }

}

static void Interface() {

  int iDim, iCoef;
  int j;
  long long iDonor, iReceiver;

  int NumWorldProcs, WorldRank;
  MPI_Comm_size(MPI_COMM_WORLD, &NumWorldProcs);
  MPI_Comm_rank(MPI_COMM_WORLD, &WorldRank);

  ovk_context_params *ContextParams;
  ovkCreateContextParams(&ContextParams);
  ovkSetContextParamComm(ContextParams, MPI_COMM_WORLD);
  ovkSetContextParamLogLevel(ContextParams, OVK_LOG_ERRORS | OVK_LOG_WARNINGS | OVK_LOG_STATUS);

  ovk_context *Context;
  ovk_error CreateError;
  ovkCreateContext(&Context, &ContextParams, &CreateError);
  if (CreateError != OVK_ERROR_NONE) {
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  ovk_shared_context *SharedContext;
  ovkShareContext(&Context, &SharedContext);

  ovk_domain_params *DomainParams;
  ovkCreateDomainParams(&DomainParams);
  ovkSetDomainParamDimension(DomainParams, 2);
  ovkSetDomainParamComm(DomainParams, MPI_COMM_WORLD);

  ovk_domain *Domain;
  ovkCreateDomain(&Domain, SharedContext, &DomainParams);

  int Size[] = {64,64,1};

  int GridIDs[] = {1, 2};
  ovk_grid_params *MaybeGridParams[] = {NULL, NULL};

  bool Grid1IsLocal = WorldRank < max(NumWorldProcs/2, 1);
  bool Grid2IsLocal = WorldRank >= NumWorldProcs/2;

  int Grid1Size[] = {(Size[0]+2)/2, Size[1], Size[2]};
  int Grid2Size[] = {Size[0]+2-(Size[0]+2)/2, Size[1], Size[2]};

  grid_data Grid1Data, Grid2Data;
  CreateGridData(&Grid1Data);
  CreateGridData(&Grid2Data);

  if (Grid1IsLocal) {
    grid_data *Data = &Grid1Data;
    MPI_Comm TempComm;
    MPI_Comm_split(MPI_COMM_WORLD, 0, WorldRank, &TempComm);
    int NumGridProcs;
    MPI_Comm_size(TempComm, &NumGridProcs);
    int CartDims[] = {0,0,1};
    int CartPeriods[] = {0,0,0};
    MPI_Dims_create(NumGridProcs, 2, CartDims);
    MPI_Cart_create(TempComm, 2, CartDims, CartPeriods, 1, &Data->Comm);
    MPI_Comm_free(&TempComm);
    Data->Size[0] = Grid1Size[0];
    Data->Size[1] = Grid1Size[1];
    Data->Size[2] = Grid1Size[2];
    CartesianDecomp(2, Data->Size, Data->Comm, Data->LocalRange);
    Data->NumLocalPoints =
      (long long)(Data->LocalRange[3] - Data->LocalRange[0]) *
      (long long)(Data->LocalRange[4] - Data->LocalRange[1]) *
      (long long)(Data->LocalRange[5] - Data->LocalRange[2]);
    // Pretend we have a halo
    for (int iDim = 0; iDim < 3; ++iDim) {
      Data->ExtendedRange[iDim] = Data->LocalRange[iDim];
      Data->ExtendedRange[3+iDim] = Data->LocalRange[3+iDim];
      if (Data->LocalRange[iDim] > 0) --Data->ExtendedRange[iDim];
      if (Data->LocalRange[3+iDim] < Data->Size[iDim]) ++Data->ExtendedRange[3+iDim];
    }
    Data->NumExtendedPoints =
      (long long)(Data->ExtendedRange[3] - Data->ExtendedRange[0]) *
      (long long)(Data->ExtendedRange[4] - Data->ExtendedRange[1]) *
      (long long)(Data->ExtendedRange[5] - Data->ExtendedRange[2]);
  } else {
    MPI_Comm DummyComm;
    MPI_Comm_split(MPI_COMM_WORLD, MPI_UNDEFINED, WorldRank, &DummyComm);
  }

  if (Grid2IsLocal) {
    grid_data *Data = &Grid2Data;
    MPI_Comm TempComm;
    MPI_Comm_split(MPI_COMM_WORLD, 0, WorldRank, &TempComm);
    int NumGridProcs;
    MPI_Comm_size(TempComm, &NumGridProcs);
    int CartDims[] = {0,0,1};
    int CartPeriods[] = {0,0,0};
    MPI_Dims_create(NumGridProcs, 2, CartDims);
    MPI_Cart_create(TempComm, 2, CartDims, CartPeriods, 1, &Data->Comm);
    MPI_Comm_free(&TempComm);
    Data->Size[0] = Grid2Size[0];
    Data->Size[1] = Grid2Size[1];
    Data->Size[2] = Grid2Size[2];
    CartesianDecomp(2, Data->Size, Data->Comm, Data->LocalRange);
    Data->NumLocalPoints =
      (long long)(Data->LocalRange[3] - Data->LocalRange[0]) *
      (long long)(Data->LocalRange[4] - Data->LocalRange[1]) *
      (long long)(Data->LocalRange[5] - Data->LocalRange[2]);
    // Pretend we have a halo
    for (int iDim = 0; iDim < 3; ++iDim) {
      Data->ExtendedRange[iDim] = Data->LocalRange[iDim];
      Data->ExtendedRange[3+iDim] = Data->LocalRange[3+iDim];
      if (Data->LocalRange[iDim] > 0) --Data->ExtendedRange[iDim];
      if (Data->LocalRange[3+iDim] < Data->Size[iDim]) ++Data->ExtendedRange[3+iDim];
    }
    Data->NumExtendedPoints =
      (long long)(Data->ExtendedRange[3] - Data->ExtendedRange[0]) *
      (long long)(Data->ExtendedRange[4] - Data->ExtendedRange[1]) *
      (long long)(Data->ExtendedRange[5] - Data->ExtendedRange[2]);
  } else {
    MPI_Comm DummyComm;
    MPI_Comm_split(MPI_COMM_WORLD, MPI_UNDEFINED, WorldRank, &DummyComm);
  }

  if (Grid1IsLocal) {
    ovkCreateGridParams(&MaybeGridParams[0]);
    ovk_grid_params *GridParams = MaybeGridParams[0];
    ovkSetGridParamName(GridParams, "Left");
    ovkSetGridParamDimension(GridParams, 2);
    ovkSetGridParamComm(GridParams, Grid1Data.Comm);
    const int Zero[3] = {0,0,0};
    ovkSetGridParamGlobalRange(GridParams, Zero, Grid1Data.Size);
    ovkSetGridParamLocalRange(GridParams, Grid1Data.LocalRange, Grid1Data.LocalRange+3);
    ovkSetGridParamGeometryType(GridParams, OVK_GEOMETRY_TYPE_UNIFORM);
  }

  if (Grid2IsLocal) {
    ovkCreateGridParams(&MaybeGridParams[1]);
    ovk_grid_params *GridParams = MaybeGridParams[1];
    ovkSetGridParamName(GridParams, "Right");
    ovkSetGridParamDimension(GridParams, 2);
    ovkSetGridParamComm(GridParams, Grid2Data.Comm);
    const int Zero[3] = {0,0,0};
    ovkSetGridParamGlobalRange(GridParams, Zero, Grid2Data.Size);
    ovkSetGridParamLocalRange(GridParams, Grid2Data.LocalRange, Grid2Data.LocalRange+3);
    ovkSetGridParamGeometryType(GridParams, OVK_GEOMETRY_TYPE_UNIFORM);
  }

  ovkCreateGrids(Domain, 2, GridIDs, MaybeGridParams);

  const int CONNECTIVITY_ID = 1;
  ovkCreateComponent(Domain, CONNECTIVITY_ID, OVK_COMPONENT_TYPE_CONNECTIVITY, NULL);

  {

    ovk_connectivity_component *ConnectivityComponent;
    ovkEditComponent(Domain, CONNECTIVITY_ID, OVK_COMPONENT_TYPE_CONNECTIVITY,
      &ConnectivityComponent);

    int MGridIDs[] = {1, 2};
    int NGridIDs[] = {2, 1};

    ovkCreateConnectivities(ConnectivityComponent, 2, MGridIDs, NGridIDs);

    if (Grid1IsLocal) {

      const ovk_grid *Grid;
      ovkGetGrid(Domain, 1, &Grid);

      int GlobalRange[6];
      ovkGetGridGlobalRange(Grid, GlobalRange, GlobalRange+3);

      int LocalRange[6];
      ovkGetGridLocalRange(Grid, LocalRange, LocalRange+3);

      bool HasInterface = LocalRange[3] == GlobalRange[3];

      ovk_connectivity_m *ConnectivityM;
      ovkEditConnectivityM(ConnectivityComponent, 1, 2, &ConnectivityM);

      long long NumDonors = HasInterface ? (LocalRange[4]-LocalRange[1]) : 0;
      ovkResizeConnectivityM(ConnectivityM, NumDonors, 1);

      int *Extents[2][2];
      double *Coords[2];
      double *InterpCoefs[2][1];
      int *Destinations[2];
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMExtents(ConnectivityM, iDim, &Extents[0][iDim], &Extents[1][iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMCoords(ConnectivityM, iDim, &Coords[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        for (iCoef = 0; iCoef < 1; ++iCoef) {
          ovkEditConnectivityMInterpCoefs(ConnectivityM, iDim, iCoef, &InterpCoefs[iDim][iCoef]);
        }
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMDestinations(ConnectivityM, iDim, &Destinations[iDim]);
      }

      if (HasInterface) {
        iDonor = 0;
        for (j = LocalRange[1]; j < LocalRange[4]; ++j) {
          Extents[0][0][iDonor] = GlobalRange[3]-2;
          Extents[0][1][iDonor] = j;
          Extents[1][0][iDonor] = Extents[0][0][iDonor]+1;
          Extents[1][1][iDonor] = Extents[0][1][iDonor]+1;
          Coords[0][iDonor] = 0.;
          Coords[1][iDonor] = 0.;
          InterpCoefs[0][0][iDonor] = 1.;
          InterpCoefs[1][0][iDonor] = 1.;
          Destinations[0][iDonor] = 0;
          Destinations[1][iDonor] = j;
          ++iDonor;
        }
      }

      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMExtents(ConnectivityM, iDim, &Extents[0][iDim], &Extents[1][iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMCoords(ConnectivityM, iDim, &Coords[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        for (iCoef = 0; iCoef < 1; ++iCoef) {
          ovkRestoreConnectivityMInterpCoefs(ConnectivityM, iDim, iCoef,
            &InterpCoefs[iDim][iCoef]);
        }
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMDestinations(ConnectivityM, iDim, &Destinations[iDim]);
      }

      ovkRestoreConnectivityM(ConnectivityComponent, 1, 2, &ConnectivityM);

      ovk_connectivity_n *ConnectivityN;
      ovkEditConnectivityN(ConnectivityComponent, 2, 1, &ConnectivityN);

      long long NumReceivers = HasInterface ? (LocalRange[4]-LocalRange[1]) : 0;
      ovkResizeConnectivityN(ConnectivityN, NumReceivers);

      int *Points[2];
      int *Sources[2];
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityNPoints(ConnectivityN, iDim, &Points[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityNSources(ConnectivityN, iDim, &Sources[iDim]);
      }

      if (HasInterface) {
        iReceiver = 0;
        for (j = LocalRange[1]; j < LocalRange[4]; ++j) {
          Points[0][iReceiver] = GlobalRange[3]-1;
          Points[1][iReceiver] = j;
          Sources[0][iReceiver] = 1;
          Sources[1][iReceiver] = j;
          ++iReceiver;
        }
      }

      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityNPoints(ConnectivityN, iDim, &Points[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityNSources(ConnectivityN, iDim, &Sources[iDim]);
      }

      ovkRestoreConnectivityN(ConnectivityComponent, 2, 1, &ConnectivityN);

    }

    if (Grid2IsLocal) {

      const ovk_grid *Grid;
      ovkGetGrid(Domain, 2, &Grid);

      int GlobalRange[6];
      ovkGetGridGlobalRange(Grid, GlobalRange, GlobalRange+3);

      int LocalRange[6];
      ovkGetGridLocalRange(Grid, LocalRange, LocalRange+3);

      bool HasInterface = LocalRange[0] == GlobalRange[0];

      ovk_connectivity_m *ConnectivityM;
      ovkEditConnectivityM(ConnectivityComponent, 2, 1, &ConnectivityM);

      long long NumDonors = HasInterface ? (LocalRange[4]-LocalRange[1]) : 0;
      ovkResizeConnectivityM(ConnectivityM, NumDonors, 1);

      int *Extents[2][2];
      double *Coords[2];
      double *InterpCoefs[2][1];
      int *Destinations[2];
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMExtents(ConnectivityM, iDim, &Extents[0][iDim], &Extents[1][iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMCoords(ConnectivityM, iDim, &Coords[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        for (iCoef = 0; iCoef < 1; ++iCoef) {
          ovkEditConnectivityMInterpCoefs(ConnectivityM, iDim, iCoef, &InterpCoefs[iDim][iCoef]);
        }
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityMDestinations(ConnectivityM, iDim, &Destinations[iDim]);
      }

      if (HasInterface) {
        iDonor = 0;
        for (j = LocalRange[1]; j < LocalRange[4]; ++j) {
          Extents[0][0][iDonor] = 1;
          Extents[0][1][iDonor] = j;
          Extents[1][0][iDonor] = Extents[0][0][iDonor]+1;
          Extents[1][1][iDonor] = Extents[0][1][iDonor]+1;
          Coords[0][iDonor] = 0.;
          Coords[1][iDonor] = 0.;
          InterpCoefs[0][0][iDonor] = 1.;
          InterpCoefs[1][0][iDonor] = 1.;
          Destinations[0][iDonor] = Grid1Size[0]-1;
          Destinations[1][iDonor] = j;
          ++iDonor;
        }
      }

      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMExtents(ConnectivityM, iDim, &Extents[0][iDim], &Extents[1][iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMCoords(ConnectivityM, iDim, &Coords[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        for (iCoef = 0; iCoef < 1; ++iCoef) {
          ovkRestoreConnectivityMInterpCoefs(ConnectivityM, iDim, iCoef,
            &InterpCoefs[iDim][iCoef]);
        }
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityMDestinations(ConnectivityM, iDim, &Destinations[iDim]);
      }

      ovkRestoreConnectivityM(ConnectivityComponent, 2, 1, &ConnectivityM);

      ovk_connectivity_n *ConnectivityN;
      ovkEditConnectivityN(ConnectivityComponent, 1, 2, &ConnectivityN);

      long long NumReceivers = HasInterface ? (LocalRange[4]-LocalRange[1]) : 0;
      ovkResizeConnectivityN(ConnectivityN, NumReceivers);

      int *Points[2];
      int *Sources[2];
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityNPoints(ConnectivityN, iDim, &Points[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkEditConnectivityNSources(ConnectivityN, iDim, &Sources[iDim]);
      }

      if (HasInterface) {
        iReceiver = 0;
        for (j = LocalRange[1]; j < LocalRange[4]; ++j) {
          Points[0][iReceiver] = 0;
          Points[1][iReceiver] = j;
          Sources[0][iReceiver] = Grid1Size[0]-2;
          Sources[1][iReceiver] = j;
          ++iReceiver;
        }
      }

      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityNPoints(ConnectivityN, iDim, &Points[iDim]);
      }
      for (iDim = 0; iDim < 2; ++iDim) {
        ovkRestoreConnectivityNSources(ConnectivityN, iDim, &Sources[iDim]);
      }

      ovkRestoreConnectivityN(ConnectivityComponent, 1, 2, &ConnectivityN);

    }

    ovkRestoreComponent(Domain, CONNECTIVITY_ID, OVK_COMPONENT_TYPE_CONNECTIVITY,
      &ConnectivityComponent);

  }

  ovk_exchanger *Exchanger;
  ovkCreateExchanger(&Exchanger, SharedContext, NULL);

  ovk_exchanger_bindings *ExchangerBindings;
  ovkCreateExchangerBindings(&ExchangerBindings);
  ovkSetExchangerBindingsConnectivityComponentID(ExchangerBindings, CONNECTIVITY_ID);
  ovkBindExchanger(Exchanger, Domain, &ExchangerBindings);

  const ovk_connectivity_component *ConnectivityComponent;
  ovkGetComponent(Domain, CONNECTIVITY_ID, OVK_COMPONENT_TYPE_CONNECTIVITY, &ConnectivityComponent);

  double *Grid1DonorValues, *Grid1ReceiverValues;
  if (Grid1IsLocal) {
    grid_data *Data = &Grid1Data;
    const ovk_connectivity_m *ConnectivityM;
    ovkGetConnectivityM(ConnectivityComponent, 1, 2, &ConnectivityM);
    ovkCreateExchangerCollect(Exchanger, 1, 2, 1, OVK_COLLECT_INTERPOLATE,
      OVK_DOUBLE, 1, Data->ExtendedRange, Data->ExtendedRange+3, OVK_ROW_MAJOR);
    ovkCreateExchangerSend(Exchanger, 1, 2, 1, OVK_DOUBLE, 1, 1);
    long long NumDonors;
    ovkGetConnectivityMCount(ConnectivityM, &NumDonors);
    Grid1DonorValues = malloc(NumDonors*sizeof(double));
    const ovk_connectivity_n *ConnectivityN;
    ovkGetConnectivityN(ConnectivityComponent, 2, 1, &ConnectivityN);
    ovkCreateExchangerReceive(Exchanger, 2, 1, 1, OVK_DOUBLE, 1, 1);
    ovkCreateExchangerDisperse(Exchanger, 2, 1, 1, OVK_DISPERSE_OVERWRITE, OVK_DOUBLE, 1,
      Data->ExtendedRange, Data->ExtendedRange+3, OVK_ROW_MAJOR);
    long long NumReceivers;
    ovkGetConnectivityNCount(ConnectivityN, &NumReceivers);
    Grid1ReceiverValues = malloc(NumReceivers*sizeof(double));
  }

  double *Grid2DonorValues, *Grid2ReceiverValues;
  if (Grid2IsLocal) {
    grid_data *Data = &Grid2Data;
    const ovk_connectivity_m *ConnectivityM;
    ovkGetConnectivityM(ConnectivityComponent, 2, 1, &ConnectivityM);
    ovkCreateExchangerCollect(Exchanger, 2, 1, 1, OVK_COLLECT_INTERPOLATE,
      OVK_DOUBLE, 1, Data->ExtendedRange, Data->ExtendedRange+3, OVK_ROW_MAJOR);
    ovkCreateExchangerSend(Exchanger, 2, 1, 1, OVK_DOUBLE, 1, 1);
    long long NumDonors;
    ovkGetConnectivityMCount(ConnectivityM, &NumDonors);
    Grid2DonorValues = malloc(NumDonors*sizeof(double));
    const ovk_connectivity_n *ConnectivityN;
    ovkGetConnectivityN(ConnectivityComponent, 1, 2, &ConnectivityN);
    ovkCreateExchangerReceive(Exchanger, 1, 2, 1, OVK_DOUBLE, 1, 1);
    ovkCreateExchangerDisperse(Exchanger, 1, 2, 1, OVK_DISPERSE_OVERWRITE, OVK_DOUBLE, 1,
      Data->ExtendedRange, Data->ExtendedRange+3, OVK_ROW_MAJOR);
    long long NumReceivers;
    ovkGetConnectivityNCount(ConnectivityN, &NumReceivers);
    Grid2ReceiverValues = malloc(NumReceivers*sizeof(double));
  }

  double *Grid1FieldValues;
  if (Grid1IsLocal) {
    grid_data *Data = &Grid1Data;
    Grid1FieldValues = malloc(Data->NumExtendedPoints*sizeof(double));
    memset(Grid1FieldValues, -1., Data->NumExtendedPoints);
  }

  double *Grid2FieldValues;
  if (Grid2IsLocal) {
    grid_data *Data = &Grid2Data;
    Grid2FieldValues = malloc(Data->NumExtendedPoints*sizeof(double));
    memset(Grid2FieldValues, 1., Data->NumExtendedPoints);
  }

  ovk_request **Requests = malloc(4*sizeof(ovk_request *));
  int NumRequests = 0;

  if (Grid1IsLocal) {
    ovkExchangerReceive(Exchanger, 2, 1, 1, &Grid1ReceiverValues, &Requests[NumRequests]);
    ++NumRequests;
  }

  if (Grid2IsLocal) {
    ovkExchangerReceive(Exchanger, 1, 2, 1, &Grid2ReceiverValues, &Requests[NumRequests]);
    ++NumRequests;
  }

  if (Grid1IsLocal) {
    ovkExchangerCollect(Exchanger, 1, 2, 1, &Grid1FieldValues, &Grid1DonorValues);
    ovkExchangerSend(Exchanger, 1, 2, 1, &Grid1DonorValues, &Requests[NumRequests]);
    ++NumRequests;
  }

  if (Grid2IsLocal) {
    ovkExchangerCollect(Exchanger, 2, 1, 1, &Grid2FieldValues, &Grid2DonorValues);
    ovkExchangerSend(Exchanger, 2, 1, 1, &Grid2DonorValues, &Requests[NumRequests]);
    ++NumRequests;
  }

  ovkWaitAll(NumRequests, Requests);

  free(Requests);

  if (Grid1IsLocal) {
    ovkExchangerDisperse(Exchanger, 2, 1, 1, &Grid1ReceiverValues, &Grid1FieldValues);
  }

  if (Grid2IsLocal) {
    ovkExchangerDisperse(Exchanger, 1, 2, 1, &Grid2ReceiverValues, &Grid2FieldValues);
  }

  if (Grid1IsLocal) {
    free(Grid1DonorValues);
    free(Grid1ReceiverValues);
    free(Grid1FieldValues);
  }

  if (Grid2IsLocal) {
    free(Grid2DonorValues);
    free(Grid2ReceiverValues);
    free(Grid2FieldValues);
  }

  ovkDestroyExchanger(&Exchanger);
  ovkDestroyDomain(&Domain);

  ovkResetSharedContext(&SharedContext);

  DestroyGridData(&Grid1Data);
  DestroyGridData(&Grid2Data);

}