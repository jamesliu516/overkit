// Copyright (c) 2018 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#include "ovk/core/ConnectivityD.h"

#include "ovk/core/ErrorHandler.h"
#include "ovk/core/Global.h"
#include "ovk/core/Grid.h"
#include "ovk/core/Logger.h"
#include "ovk/core/Range.h"

static bool EditingProperties(const ovk_connectivity_d *Donors);
static bool EditingExtents(const ovk_connectivity_d *Donors);
static bool EditingCoords(const ovk_connectivity_d *Donors);
static bool EditingInterpCoefs(const ovk_connectivity_d *Donors);
static bool EditingDestinations(const ovk_connectivity_d *Donors);
static bool EditingDestinationRanks(const ovk_connectivity_d *Donors);

static void DefaultProperties(ovk_connectivity_d_properties *Properties);
static void DefaultEdits(t_connectivity_d_edits *Edits);

void PRIVATE(CreateConnectivityDonorSide)(ovk_connectivity_d **Donors_, const ovk_grid *Grid,
  int DestinationGridID, t_logger *Logger, t_error_handler *ErrorHandler) {

  int iDim;

  const ovk_grid_properties *GridProperties;
  ovkGetGridProperties(Grid, &GridProperties);

  int GridID;
  ovkGetGridPropertyID(GridProperties, &GridID);

  int NumDims;
  ovkGetGridPropertyDimension(GridProperties, &NumDims);

  MPI_Comm Comm;
  ovkGetGridPropertyComm(GridProperties, &Comm);

  MPI_Barrier(Comm);

  int CommSize, CommRank;
  MPI_Comm_size(Comm, &CommSize);
  MPI_Comm_rank(Comm, &CommRank);

  *Donors_ = malloc(sizeof(ovk_connectivity_d));
  ovk_connectivity_d *Donors = *Donors_;

  DefaultProperties(&Donors->properties);

  Donors->properties.grid_id = GridID;
  Donors->properties.destination_grid_id = DestinationGridID;
  Donors->properties.num_dims = NumDims;
  Donors->properties.comm = Comm;
  Donors->properties.comm_size = CommSize;
  Donors->properties.comm_rank = CommRank;

  Donors->properties_edit_ref_count = 0;

  Donors->logger = Logger;
  Donors->error_handler = ErrorHandler;

  Donors->grid = Grid;

  DefaultEdits(&Donors->edits);

  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->extents[0][iDim] = NULL;
    Donors->extents[1][iDim] = NULL;
  }
  Donors->extents_edit_ref_count = 0;

  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->coords[iDim] = NULL;
  }
  Donors->coords_edit_ref_count = 0;

  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->interp_coefs[iDim] = NULL;
  }
  Donors->interp_coefs_edit_ref_count = 0;

  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->destinations[iDim] = NULL;
  }
  Donors->destinations_edit_ref_count = 0;

  Donors->destination_ranks = NULL;
  Donors->destination_ranks_edit_ref_count = 0;

  MPI_Barrier(Donors->properties.comm);

}

void PRIVATE(DestroyConnectivityDonorSide)(ovk_connectivity_d **Donors_) {

  ovk_connectivity_d *Donors = *Donors_;

  MPI_Barrier(Donors->properties.comm);

  free(Donors->extents[0][0]);
  free(Donors->coords[0]);
  if (Donors->properties.max_donor_size > 0) {
    free(Donors->interp_coefs[0][0]);
  }
  free(Donors->interp_coefs[0]);
  free(Donors->destinations[0]);
  free(Donors->destination_ranks);

  MPI_Comm Comm = Donors->properties.comm;

  free_null(Donors_);

  MPI_Barrier(Comm);

}

void ovkGetConnectivityDonorSideProperties(const ovk_connectivity_d *Donors,
  const ovk_connectivity_d_properties **Properties) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");

  *Properties = &Donors->properties;

}

void ovkResizeDonors(ovk_connectivity_d *Donors, size_t NumDonors, int MaxDonorSize) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(NumDonors >= 0, "Invalid donor count.");
  OVK_DEBUG_ASSERT(MaxDonorSize >= 0, "Invalid max donor size.");

  MPI_Barrier(Donors->properties.comm);

  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot resize donors while editing properties.");
  OVK_DEBUG_ASSERT(!EditingExtents(Donors), "Cannot resize donors while editing extents.");
  OVK_DEBUG_ASSERT(!EditingCoords(Donors), "Cannot resize donors while editing coords.");
  OVK_DEBUG_ASSERT(!EditingInterpCoefs(Donors), "Cannot resize donors while editing interp coefs.");
  OVK_DEBUG_ASSERT(!EditingDestinations(Donors), "Cannot resize donors while editing destinations.");
  OVK_DEBUG_ASSERT(!EditingDestinationRanks(Donors), "Cannot resize donors while editing "
    "destination ranks.");

  if (OVK_DEBUG) {
    // Needed because editing interp coefs blocks on comm -- if max size was 0 on some ranks,
    // calling the edit function in a loop from 0 to max size would result in it not being called
    // on those ranks
    int GlobalMaxDonorSize;
    MPI_Allreduce(&MaxDonorSize, &GlobalMaxDonorSize, 1, MPI_INT, MPI_MAX, Donors->properties.comm);
    OVK_DEBUG_ASSERT(MaxDonorSize == GlobalMaxDonorSize, "Max donor size must be the same on all "
      "connectivity processes.");
  }

  int iDim, iPoint;
  size_t iDonor;

  int NumDims = Donors->properties.num_dims;

  int PrevMaxDonorSize = Donors->properties.max_donor_size;

  free(Donors->extents[0][0]);
  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->extents[0][iDim] = NULL;
    Donors->extents[1][iDim] = NULL;
  }
  free(Donors->coords[0]);
  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->coords[iDim] = NULL;
  }
  if (PrevMaxDonorSize > 0) {
    free(Donors->interp_coefs[0][0]);
  }
  free(Donors->interp_coefs[0]);
  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->interp_coefs[iDim] = NULL;
  }
  free(Donors->destinations[0]);
  for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
    Donors->destinations[iDim] = NULL;
  }
  free_null(&Donors->destination_ranks);

  Donors->properties.num_donors = NumDonors;
  Donors->properties.max_donor_size = MaxDonorSize;

  if (MaxDonorSize > 0) {
    Donors->interp_coefs[0] = malloc(MAX_DIMS*MaxDonorSize*sizeof(double *));
    Donors->interp_coefs[1] = Donors->interp_coefs[0] + MaxDonorSize;
    Donors->interp_coefs[2] = Donors->interp_coefs[1] + MaxDonorSize;
    for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
      for (iPoint = 0; iPoint < MaxDonorSize; ++iPoint) {
        Donors->interp_coefs[iDim][iPoint] = NULL;
      }
    }
  }

  if (NumDonors > 0) {

    Donors->extents[0][0] = malloc(2*MAX_DIMS*NumDonors*sizeof(int));
    Donors->extents[1][0] = Donors->extents[0][0] + MAX_DIMS*NumDonors;
    for (iDim = 1; iDim < MAX_DIMS; ++iDim) {
      Donors->extents[0][iDim] = Donors->extents[0][iDim-1] + NumDonors;
      Donors->extents[1][iDim] = Donors->extents[1][iDim-1] + NumDonors;
    }
    Donors->coords[0] = malloc(MAX_DIMS*NumDonors*sizeof(double));
    Donors->coords[1] = Donors->coords[0] + NumDonors;
    Donors->coords[2] = Donors->coords[1] + NumDonors;
    if (MaxDonorSize > 0) {
      Donors->interp_coefs[0][0] = malloc(MAX_DIMS*MaxDonorSize*NumDonors*sizeof(double));
      Donors->interp_coefs[1][0] = Donors->interp_coefs[0][0] + MaxDonorSize*NumDonors;
      Donors->interp_coefs[2][0] = Donors->interp_coefs[1][0] + MaxDonorSize*NumDonors;
      for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
        for (iPoint = 1; iPoint < MaxDonorSize; ++iPoint) {
          Donors->interp_coefs[iDim][iPoint] = Donors->interp_coefs[iDim][iPoint-1] + NumDonors;
        }
      }
    }
    Donors->destinations[0] = malloc(MAX_DIMS*NumDonors*sizeof(int));
    Donors->destinations[1] = Donors->destinations[0] + NumDonors;
    Donors->destinations[2] = Donors->destinations[1] + NumDonors;
    Donors->destination_ranks = malloc(NumDonors*sizeof(int));

    for (iDonor = 0; iDonor < NumDonors; ++iDonor) {
      for (iDim = 0; iDim < NumDims; ++iDim) {
        Donors->extents[0][iDim][iDonor] = 0;
        Donors->extents[1][iDim][iDonor] = 0;
      }
      for (iDim = NumDims; iDim < MAX_DIMS; ++iDim) {
        Donors->extents[0][iDim][iDonor] = 0;
        Donors->extents[1][iDim][iDonor] = 1;
      }
      for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
        Donors->coords[iDim][iDonor] = 0.;
      }
      for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
        for (iPoint = 0; iPoint < MaxDonorSize; ++iPoint) {
          Donors->interp_coefs[iDim][iPoint][iDonor] = 0.;
        }
      }
      for (iDim = 0; iDim < MAX_DIMS; ++iDim) {
        Donors->destinations[iDim][iDonor] = 0;
      }
      Donors->destination_ranks[iDonor] = -1;
    }

  }

  Donors->edits.num_donors = true;
  Donors->edits.extents = true;
  Donors->edits.coords = true;
  Donors->edits.interp_coefs = true;
  Donors->edits.destinations = true;

  MPI_Barrier(Donors->properties.comm);

}

void ovkEditDonorExtents(ovk_connectivity_d *Donors, int Dimension, int **Begins, int **Ends) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Begins, "Invalid begins pointer.");
  OVK_DEBUG_ASSERT(Ends, "Invalid ends pointer.");
  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot edit extents while editing properties.");

  bool StartEdit = Donors->extents_edit_ref_count == 0;
  ++Donors->extents_edit_ref_count;

  if (StartEdit) {
    MPI_Barrier(Donors->properties.comm);
  }

  *Begins = Donors->extents[0][Dimension];
  *Ends = Donors->extents[1][Dimension];

}

void ovkReleaseDonorExtents(ovk_connectivity_d *Donors, int Dimension, int **Begins, int **Ends) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Begins, "Invalid begins pointer.");
  OVK_DEBUG_ASSERT(Ends, "Invalid ends pointer.");
  OVK_DEBUG_ASSERT(*Begins == Donors->extents[0][Dimension], "Invalid begins pointer.");
  OVK_DEBUG_ASSERT(*Ends == Donors->extents[1][Dimension], "Invalid ends pointer.");
  OVK_DEBUG_ASSERT(EditingExtents(Donors), "Unable to release extents; not currently being edited.");

  --Donors->extents_edit_ref_count;
  bool EndEdit = Donors->extents_edit_ref_count == 0;

  *Begins = NULL;
  *Ends = NULL;

  if (EndEdit) {
    Donors->edits.extents = true;
    MPI_Barrier(Donors->properties.comm);
  }

}

void ovkEditDonorCoords(ovk_connectivity_d *Donors, int Dimension, double **Coords) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Coords, "Invalid coords pointer.");
  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot edit coords while editing properties.");

  bool StartEdit = Donors->coords_edit_ref_count == 0;
  ++Donors->coords_edit_ref_count;

  if (StartEdit) {
    MPI_Barrier(Donors->properties.comm);
  }

  *Coords = Donors->coords[Dimension];

}

void ovkReleaseDonorCoords(ovk_connectivity_d *Donors, int Dimension, double **Coords) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Coords, "Invalid coords pointer.");
  OVK_DEBUG_ASSERT(*Coords == Donors->coords[Dimension], "Invalid coords pointer.");
  OVK_DEBUG_ASSERT(EditingCoords(Donors), "Unable to release coords; not currently being edited.");

  --Donors->coords_edit_ref_count;
  bool EndEdit = Donors->coords_edit_ref_count == 0;

  *Coords = NULL;

  if (EndEdit) {
    Donors->edits.coords = true;
    MPI_Barrier(Donors->properties.comm);
  }

}

void ovkEditDonorInterpCoefs(ovk_connectivity_d *Donors, int Dimension, int Point,
  double **InterpCoefs) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Point >= 0 && Point < Donors->properties.max_donor_size, "Invalid point.");
  OVK_DEBUG_ASSERT(InterpCoefs, "Invalid interp coefs pointer.");
  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot edit interp coefs while editing properties.");

  bool StartEdit = Donors->interp_coefs_edit_ref_count == 0;
  ++Donors->interp_coefs_edit_ref_count;

  if (StartEdit) {
    MPI_Barrier(Donors->properties.comm);
  }

  *InterpCoefs = Donors->interp_coefs[Dimension][Point];

}

void ovkReleaseDonorInterpCoefs(ovk_connectivity_d *Donors, int Dimension, int Point,
  double **InterpCoefs) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Point >= 0 && Point < Donors->properties.max_donor_size, "Invalid point.");
  OVK_DEBUG_ASSERT(InterpCoefs, "Invalid donor coefs pointer.");
  OVK_DEBUG_ASSERT(*InterpCoefs == Donors->interp_coefs[Dimension][Point], "Invalid interp coefs "
    "pointer.");
  OVK_DEBUG_ASSERT(EditingInterpCoefs(Donors), "Unable to release interp coefs; not currently "
    "being edited.");

  --Donors->interp_coefs_edit_ref_count;
  bool EndEdit = Donors->interp_coefs_edit_ref_count == 0;

  *InterpCoefs = NULL;

  if (EndEdit) {
    Donors->edits.interp_coefs = true;
    MPI_Barrier(Donors->properties.comm);
  }

}

void ovkEditDonorDestinations(ovk_connectivity_d *Donors, int Dimension, int **Destinations) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Destinations, "Invalid destinations pointer.");
  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot edit destinations while editing properties.");

  bool StartEdit = Donors->destinations_edit_ref_count == 0;
  ++Donors->destinations_edit_ref_count;

  if (StartEdit) {
    MPI_Barrier(Donors->properties.comm);
  }

  *Destinations = Donors->destinations[Dimension];

}

void ovkReleaseDonorDestinations(ovk_connectivity_d *Donors, int Dimension, int **Destinations) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(Dimension >= 0 && Dimension < MAX_DIMS, "Invalid dimension.");
  OVK_DEBUG_ASSERT(Destinations, "Invalid destinations pointer.");
  OVK_DEBUG_ASSERT(*Destinations == Donors->destinations[Dimension], "Invalid destinations pointer.");
  OVK_DEBUG_ASSERT(EditingDestinations(Donors), "Unable to release destinations; not currently "
    "being edited.");

  --Donors->destinations_edit_ref_count;
  bool EndEdit = Donors->destinations_edit_ref_count == 0;

  *Destinations = NULL;

  if (EndEdit) {
    Donors->edits.destinations = true;
    MPI_Barrier(Donors->properties.comm);
  }


}

void ovkEditDonorDestinationRanks(ovk_connectivity_d *Donors, int **DestinationRanks) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(DestinationRanks, "Invalid destination ranks pointer.");
  OVK_DEBUG_ASSERT(!EditingProperties(Donors), "Cannot edit destination ranks while editing "
    "properties.");

  bool StartEdit = Donors->destination_ranks_edit_ref_count == 0;
  ++Donors->destination_ranks_edit_ref_count;

  if (StartEdit) {
    MPI_Barrier(Donors->properties.comm);
  }

  *DestinationRanks = Donors->destination_ranks;

}

void ovkReleaseDonorDestinationRanks(ovk_connectivity_d *Donors, int **DestinationRanks) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(DestinationRanks, "Invalid destination ranks pointer.");
  OVK_DEBUG_ASSERT(*DestinationRanks == Donors->destination_ranks, "Invalid destination ranks "
    "pointer.");
  OVK_DEBUG_ASSERT(EditingDestinationRanks(Donors), "Unable to release destination ranks; not "
    "currently being edited.");

  --Donors->destination_ranks_edit_ref_count;
  bool EndEdit = Donors->destination_ranks_edit_ref_count == 0;

  *DestinationRanks = NULL;

  if (EndEdit) {
    Donors->edits.destinations = true;
    MPI_Barrier(Donors->properties.comm);
  }

}

void ovkGetConnectivityDonorSideGrid(const ovk_connectivity_d *Donors, const ovk_grid **DonorGrid) {

  OVK_DEBUG_ASSERT(Donors, "Invalid donors pointer.");
  OVK_DEBUG_ASSERT(DonorGrid, "Invalid donor grid pointer.");

  *DonorGrid = Donors->grid;

}

static bool EditingProperties(const ovk_connectivity_d *Donors) {

  return Donors->properties_edit_ref_count > 0;

}

static bool EditingExtents(const ovk_connectivity_d *Donors) {

  return Donors->extents_edit_ref_count > 0;

}

static bool EditingCoords(const ovk_connectivity_d *Donors) {

  return Donors->coords_edit_ref_count > 0;

}

static bool EditingInterpCoefs(const ovk_connectivity_d *Donors) {

  return Donors->interp_coefs_edit_ref_count > 0;

}

static bool EditingDestinations(const ovk_connectivity_d *Donors) {

  return Donors->destinations_edit_ref_count > 0;

}

static bool EditingDestinationRanks(const ovk_connectivity_d *Donors) {

  return Donors->destination_ranks_edit_ref_count > 0;

}

void PRIVATE(GetConnectivityDonorSideEdits)(const ovk_connectivity_d *Donors,
  const t_connectivity_d_edits **Edits) {

  *Edits = &Donors->edits;

}

void PRIVATE(ResetConnectivityDonorSideEdits)(ovk_connectivity_d *Donors) {

  MPI_Barrier(Donors->properties.comm);

  DefaultEdits(&Donors->edits);

  MPI_Barrier(Donors->properties.comm);

}

static void DefaultProperties(ovk_connectivity_d_properties *Properties) {

  Properties->grid_id = -1;
  Properties->destination_grid_id = -1;
  Properties->num_dims = 2;
  Properties->comm = MPI_COMM_NULL;
  Properties->comm_size = 0;
  Properties->comm_rank = 0;
  Properties->num_donors = 0;
  Properties->max_donor_size = 0;

}

void ovkGetConnectivityDonorSidePropertyGridID(const ovk_connectivity_d_properties *Properties,
  int *GridID) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(GridID, "Invalid grid ID pointer.");

  *GridID = Properties->grid_id;

}

void ovkGetConnectivityDonorSidePropertyDestinationGridID(const ovk_connectivity_d_properties
  *Properties, int *DestinationGridID) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(DestinationGridID, "Invalid destination grid ID pointer.");

  *DestinationGridID = Properties->destination_grid_id;

}

void ovkGetConnectivityDonorSidePropertyDimension(const ovk_connectivity_d_properties *Properties,
  int *NumDims) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(NumDims, "Invalid num dims pointer.");

  *NumDims = Properties->num_dims;

}

void ovkGetConnectivityDonorSidePropertyComm(const ovk_connectivity_d_properties *Properties,
  MPI_Comm *Comm) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(Comm, "Invalid comm pointer.");

  *Comm = Properties->comm;

}

void ovkGetConnectivityDonorSidePropertyCommSize(const ovk_connectivity_d_properties *Properties,
  int *CommSize) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(CommSize, "Invalid comm size pointer.");

  *CommSize = Properties->comm_size;

}

void ovkGetConnectivityDonorSidePropertyCommRank(const ovk_connectivity_d_properties *Properties,
  int *CommRank) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(CommRank, "Invalid comm rank pointer.");

  *CommRank = Properties->comm_rank;

}

void ovkGetConnectivityDonorSidePropertyDonorCount(const ovk_connectivity_d_properties *Properties,
  size_t *NumDonors) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(NumDonors, "Invalid num donors pointer.");

  *NumDonors = Properties->num_donors;

}

void ovkGetConnectivityDonorSidePropertyMaxDonorSize(const ovk_connectivity_d_properties *Properties,
  int *MaxDonorSize) {

  OVK_DEBUG_ASSERT(Properties, "Invalid properties pointer.");
  OVK_DEBUG_ASSERT(MaxDonorSize, "Invalid max donor size pointer.");

  *MaxDonorSize = Properties->max_donor_size;

}

static void DefaultEdits(t_connectivity_d_edits *Edits) {

  Edits->num_donors = false;
  Edits->extents = false;
  Edits->coords = false;
  Edits->interp_coefs = false;
  Edits->destinations = false;

}