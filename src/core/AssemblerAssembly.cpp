// Copyright (c) 2019 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#include "ovk/core/Assembler.hpp"

#include "ovk/core/Array.hpp"
#include "ovk/core/ArrayOps.hpp"
#include "ovk/core/ArrayView.hpp"
#include "ovk/core/Comm.hpp"
#include "ovk/core/ConnectivityComponent.hpp"
#include "ovk/core/ConnectivityM.hpp"
#include "ovk/core/ConnectivityN.hpp"
#include "ovk/core/Context.hpp"
#include "ovk/core/Debug.hpp"
#include "ovk/core/DistributedField.hpp"
#include "ovk/core/DistributedFieldOps.hpp"
#include "ovk/core/DistributedRegionHash.hpp"
#include "ovk/core/Domain.hpp"
#include "ovk/core/Elem.hpp"
#include "ovk/core/ElemMap.hpp"
#include "ovk/core/ElemSet.hpp"
#include "ovk/core/Event.hpp"
#include "ovk/core/Field.hpp"
#include "ovk/core/FieldOps.hpp"
#include "ovk/core/FloatingRef.hpp"
#include "ovk/core/Geometry.hpp"
#include "ovk/core/GeometryComponent.hpp"
#include "ovk/core/GeometryOps.hpp"
#include "ovk/core/Global.hpp"
#include "ovk/core/Grid.hpp"
#include "ovk/core/Indexer.hpp"
#include "ovk/core/Logger.hpp"
#include "ovk/core/Map.hpp"
#include "ovk/core/Misc.hpp"
#include "ovk/core/OverlapComponent.hpp"
#include "ovk/core/OverlapM.hpp"
#include "ovk/core/OverlapN.hpp"
#include "ovk/core/Partition.hpp"
#include "ovk/core/Range.hpp"
#include "ovk/core/Set.hpp"
#include "ovk/core/State.hpp"
#include "ovk/core/StateComponent.hpp"
#include "ovk/core/TextProcessing.hpp"
#include "ovk/core/Tuple.hpp"

#include <mpi.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace ovk {

namespace {

constexpr long long NO_CELL = std::numeric_limits<long long>::min();

void GenerateActiveMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &ActiveMask);
void GenerateCellActiveMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &CellActiveMask);
void GenerateDomainBoundaryMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &DomainBoundaryMask);
void GenerateInternalBoundaryMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &InternalBoundaryMask);

}

void assembler::Assemble() {

  const domain &Domain = *Domain_;
  core::logger &Logger = Context_->core_Logger();

  MPI_Barrier(Domain.Comm());

  Logger.LogStatus(Domain.Comm().Rank() == 0, 0, "Assembling domain %s using assembler %s...",
    Domain.Name(), *Name_);

  InitializeAssembly_();
  DetectOverlap_();
  InferBoundaries_();

  AssemblyManifest_.DetectOverlap.Clear();
  AssemblyManifest_.InferBoundaries.Clear();
  AssemblyManifest_.CutBoundaryHoles.Clear();
  AssemblyManifest_.ComputeOcclusion.Clear();
  AssemblyManifest_.ApplyPadding.Clear();
  AssemblyManifest_.ApplySmoothing.Clear();
  AssemblyManifest_.MinimizeOverlap.Clear();
  AssemblyManifest_.GenerateConnectivity.Clear();

  MPI_Barrier(Domain.Comm());

  Logger.LogStatus(Domain.Comm().Rank() == 0, 0, "Done assembling domain %s using assembler %s.",
    Domain.Name(), *Name_);

}

assembler::assembly_data::assembly_data(int NumDims, comm_view Comm):
  BoundingBoxHash(NumDims, Comm)
{}

void assembler::InitializeAssembly_() {

  const domain &Domain = *Domain_;
  int NumDims = Domain.Dimension();
  auto &GeometryComponent = Domain.Component<geometry_component>(GeometryComponentID_);
  auto &StateComponent = Domain.Component<state_component>(StateComponentID_);
  assembly_data &AssemblyData = *AssemblyData_;

  for (int GridID : Domain.GridIDs()) {
    OVK_DEBUG_ASSERT(GeometryComponent.GeometryExists(GridID), "No geometry data for grid %s.",
      Domain.GridInfo(GridID).Name());
    OVK_DEBUG_ASSERT(StateComponent.StateExists(GridID), "No state data for grid %s.",
      Domain.GridInfo(GridID).Name());
  }

  if (OVK_DEBUG) {
    ValidateOptions_();
  }

  range VertexOffsetRange = MakeEmptyRange(NumDims);
  for (int iDim = 0; iDim < NumDims; ++iDim) {
    VertexOffsetRange.Begin(iDim) = 0;
    VertexOffsetRange.End(iDim) = 2;
  }

  for (int GridID : Domain.LocalGridIDs()) {
    const grid &Grid = Domain.Grid(GridID);
    auto &Flags = StateComponent.State(GridID).Flags();
    core::partition_pool PartitionPool(Context_, Grid.Comm(), Grid.Partition().NeighborRanks());
    PartitionPool.Insert(Grid.SharedPartition());
    PartitionPool.Insert(Grid.SharedCellPartition());
    local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData.Insert(GridID,
      std::move(PartitionPool));
    GenerateActiveMask(Grid, Flags, GridAuxData.ActiveMask);
    GenerateCellActiveMask(Grid, Flags, GridAuxData.CellActiveMask);
    GenerateDomainBoundaryMask(Grid, Flags, GridAuxData.DomainBoundaryMask);
    GenerateInternalBoundaryMask(Grid, Flags, GridAuxData.InternalBoundaryMask);
  }

}

void assembler::ValidateOptions_() {

  domain &Domain = *Domain_;

  for (int MGridID : Domain.GridIDs()) {
    for (int NGridID : Domain.GridIDs()) {
      const std::string &MGridName = Domain.GridInfo(MGridID).Name();
      const std::string &NGridName = Domain.GridInfo(NGridID).Name();
      if (Options_.CutBoundaryHoles({MGridID,NGridID})) {
        OVK_DEBUG_ASSERT(Options_.Overlappable({MGridID,NGridID}), "Grid %s being boundary-hole-"
          "cut by grid %s requires %s to be overlappable by %s.", NGridName, MGridName, NGridName,
          MGridName);
        OVK_DEBUG_ASSERT(Options_.Overlappable({NGridID,MGridID}), "Grid %s being boundary-hole-"
          "cut by grid %s requires %s to be overlappable by %s.", NGridName, MGridName, MGridName,
          NGridName);
      }
    }
  }

  // This is incomplete; add rest

}

void assembler::DetectOverlap_() {

  domain &Domain = *Domain_;
  core::logger &Logger = Context_->core_Logger();

  MPI_Barrier(Domain.Comm());

  Logger.LogDebug(Domain.Comm().Rank() == 0, 1, "Detecting overlap between grids...");

  int NumDims = Domain.Dimension();
  auto &GeometryComponent = Domain.Component<geometry_component>(GeometryComponentID_);
  assembly_data &AssemblyData = *AssemblyData_;

  Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Generating distributed bounding box hash...");

  range VertexOffsetRange = MakeEmptyRange(NumDims);
  for (int iDim = 0; iDim < NumDims; ++iDim) {
    VertexOffsetRange.Begin(iDim) = 0;
    VertexOffsetRange.End(iDim) = 2;
  }

  // Range consisting of all cells having vertices in local range
  auto MakeCellCoverRange = [](const cart &CellCart, const range &CellLocalRange) -> range {
    range CellCoverRange = MakeEmptyRange(CellCart.Dimension());
    for (int iDim = 0; iDim < CellCart.Dimension(); ++iDim) {
      if (CellLocalRange.Begin(iDim) > CellCart.Range().Begin(iDim) || (CellCart.Periodic(iDim) &&
        CellLocalRange.End(iDim) != CellCart.Range().End(iDim))) {
        CellCoverRange.Begin(iDim) = CellLocalRange.Begin(iDim)-1;
      } else {
        CellCoverRange.Begin(iDim) = CellLocalRange.Begin(iDim);
      }
      CellCoverRange.End(iDim) = CellLocalRange.End(iDim);
    }
    return CellCoverRange;
  };

  array<box> LocalGridBounds;
  LocalGridBounds.Reserve(Domain.LocalGridCount());

  for (int GridID : Domain.LocalGridIDs()) {
    const grid &Grid = Domain.Grid(GridID);
    range CellCoverRange = MakeCellCoverRange(Grid.CellCart(), Grid.CellLocalRange());
    const local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(GridID);
    const distributed_field<bool> &CellActiveMask = GridAuxData.CellActiveMask;
    const geometry &Geometry = GeometryComponent.Geometry(GridID);
    auto &Coords = Geometry.Coords();
    box &Bounds = LocalGridBounds.Append();
    Bounds = MakeEmptyBox(NumDims);
    for (int k = CellCoverRange.Begin(2); k < CellCoverRange.End(2); ++k) {
      for (int j = CellCoverRange.Begin(1); j < CellCoverRange.End(1); ++j) {
        for (int i = CellCoverRange.Begin(0); i < CellCoverRange.End(0); ++i) {
          tuple<int> Cell = {i,j,k};
          if (!CellActiveMask(Cell)) continue;
          for (int o = VertexOffsetRange.Begin(2); o < VertexOffsetRange.End(2); ++o) {
            for (int n = VertexOffsetRange.Begin(1); n < VertexOffsetRange.End(1); ++n) {
              for (int m = VertexOffsetRange.Begin(0); m < VertexOffsetRange.End(0); ++m) {
                tuple<int> Vertex = {Cell(0)+m,Cell(1)+n,Cell(2)+o};
                tuple<double> VertexCoords = {
                  Coords(0)(Vertex),
                  Coords(1)(Vertex),
                  Coords(2)(Vertex)
                };
                Bounds = ExtendBox(Bounds, VertexCoords);
              }
            }
          }
        }
      }
    }
  }

  array<int> LocalGridIDs(Domain.LocalGridIDs());

  bounding_box_hash &BoundingBoxHash = AssemblyData.BoundingBoxHash;
  BoundingBoxHash = bounding_box_hash(NumDims, Domain.Comm(), Domain.LocalGridCount(),
    LocalGridBounds, LocalGridIDs);

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done generating distributed bounding box hash.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Mapping local coordinates into hash bins...");
  }

  map<int,field<int>> LocalPointOverlappingBinIndices;
  set<int> UniqueOverlappingBinIndices;

  for (int GridID : Domain.LocalGridIDs()) {
    const grid &Grid = Domain.Grid(GridID);
    const range &LocalRange = Grid.LocalRange();
    const local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(GridID);
    const distributed_field<bool> &ActiveMask = GridAuxData.ActiveMask;
    const geometry &Geometry = GeometryComponent.Geometry(GridID);
    auto &Coords = Geometry.Coords();
    field<int> &BinIndices = LocalPointOverlappingBinIndices.Insert(GridID);
    BinIndices.Resize(LocalRange, -1);
    for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
      for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
        for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
          tuple<int> Point = {i,j,k};
          if (!ActiveMask(Point)) continue;
          tuple<double> PointCoords = {
            Coords(0)(Point),
            Coords(1)(Point),
            Coords(2)(Point)
          };
          tuple<int> BinLoc = BoundingBoxHash.MapPointToBin(PointCoords);
          int BinIndex = BoundingBoxHash.BinIndexer().ToIndex(BinLoc);
          BinIndices(Point) = BinIndex;
          UniqueOverlappingBinIndices.Insert(BinIndex);
        }
      }
    }
  }

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done mapping local coordinates into hash bins.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Retrieving remote hash bins...");
  }

  map<int,bounding_box_hash_bin> Bins;

  for (int BinIndex : UniqueOverlappingBinIndices) {
    Bins.Insert(BinIndex);
  }

  BoundingBoxHash.RetrieveBins(Bins);

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done retrieving remote hash bins.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Establishing communication between potentially-"
      "overlapping ranks...");
  }

  map<int,map<int,set<int>>> OverlappingMGridIDsAndRanksForLocalNGrid;

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    const geometry &Geometry = GeometryComponent.Geometry(NGridID);
    auto &Coords = Geometry.Coords();
    field<int> &BinIndices = LocalPointOverlappingBinIndices(NGridID);
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid.Insert(NGridID);
    for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
      for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
        for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
          tuple<int> Point = {i,j,k};
          int BinIndex = BinIndices(Point);
          if (BinIndex < 0) continue;
          bounding_box_hash_bin &Bin = Bins(BinIndex);
          tuple<double> PointCoords = {
            Coords(0)(Point),
            Coords(1)(Point),
            Coords(2)(Point)
          };
          for (auto &Region : Bin.Regions()) {
            int MGridID = Region.Tag;
            if (Options_.Overlappable({MGridID,NGridID}) && Region.Extents.Contains(PointCoords)) {
              MGridIDsAndRanks.Fetch(MGridID).Insert(Region.Rank);
            }
          }
        }
      }
    }
  }

  set<int> RemoteMRanksSet;

  for (auto &NEntry : OverlappingMGridIDsAndRanksForLocalNGrid) {
    auto &MGridIDsAndRanks = NEntry.Value();
    for (auto &MEntry : MGridIDsAndRanks) {
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank != Domain.Comm().Rank()) {
          RemoteMRanksSet.Insert(Rank);
        }
      }
    }
  }

  array<int> RemoteMRanks(RemoteMRanksSet);

  array<int> RemoteNRanks = core::DynamicHandshake(Domain.Comm(), RemoteMRanks);

  array<MPI_Request> MPIRequests;
  MPIRequests.Reserve(RemoteMRanks.Count() + RemoteNRanks.Count());

  map<int,int> NumGridIDPairsFromRank;
  NumGridIDPairsFromRank.Reserve(RemoteNRanks.Count());

  for (int Rank : RemoteNRanks) {
    int &NumPairs = NumGridIDPairsFromRank.Insert(Rank, 0);
    MPI_Irecv(&NumPairs, 1, MPI_INT, Rank, 0, Domain.Comm(), &MPIRequests.Append());
  }

  map<int,int> NumGridIDPairsToRank;
  NumGridIDPairsToRank.Reserve(RemoteMRanks.Count());

  for (auto &NEntry : OverlappingMGridIDsAndRanksForLocalNGrid) {
    auto &MGridIDsAndRanks = NEntry.Value();
    for (auto &MEntry : MGridIDsAndRanks) {
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        ++NumGridIDPairsToRank.Fetch(Rank, 0);
      }
    }
  }

  for (int Rank : RemoteMRanks) {
    int &NumPairs = NumGridIDPairsToRank(Rank);
    MPI_Isend(&NumPairs, 1, MPI_INT, Rank, 0, Domain.Comm(), &MPIRequests.Append());
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  map<int,array<int>> GridIDPairRecvData;
  GridIDPairRecvData.Reserve(RemoteNRanks.Count());

  for (int Rank : RemoteNRanks) {
    int NumPairs = NumGridIDPairsFromRank(Rank);
    array<int> &Pairs = GridIDPairRecvData.Insert(Rank);
    Pairs.Resize({2*NumPairs});
    MPI_Irecv(Pairs.Data(), 2*NumPairs, MPI_INT, Rank, 0, Domain.Comm(), &MPIRequests.Append());
  }

  map<int,array<int>> GridIDPairSendData;
  GridIDPairSendData.Reserve(RemoteMRanks.Count());

  for (int Rank : RemoteMRanks) {
    int NumPairs = NumGridIDPairsToRank(Rank);
    array<int> &Pairs = GridIDPairSendData.Insert(Rank);
    Pairs.Reserve(2*NumPairs);
  }

  for (auto &NEntry : OverlappingMGridIDsAndRanksForLocalNGrid) {
    int NGridID = NEntry.Key();
    auto &MGridIDsAndRanks = NEntry.Value();
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank != Domain.Comm().Rank()) {
          array<int> &Pairs = GridIDPairSendData(Rank);
          Pairs.Append(MGridID);
          Pairs.Append(NGridID);
        }
      }
    }
  }

  for (int Rank : RemoteMRanks) {
    int NumPairs = NumGridIDPairsToRank(Rank);
    array<int> &Pairs = GridIDPairSendData(Rank);
    MPI_Isend(Pairs.Data(), 2*NumPairs, MPI_INT, Rank, 0, Domain.Comm(), &MPIRequests.Append());
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  map<int,map<int,set<int>>> OverlappingNGridIDsAndRanksForLocalMGrid;

  for (int MGridID : Domain.LocalGridIDs()) {
    OverlappingNGridIDsAndRanksForLocalMGrid.Insert(MGridID);
  }

  for (auto &NEntry : OverlappingMGridIDsAndRanksForLocalNGrid) {
    int NGridID = NEntry.Key();
    auto &MGridIDsAndRanks = NEntry.Value();
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank == Domain.Comm().Rank()) {
          OverlappingNGridIDsAndRanksForLocalMGrid(MGridID).Fetch(NGridID).Insert(Rank);
        }
      }
    }
  }

  for (int Rank : RemoteNRanks) {
    int NumPairs = NumGridIDPairsFromRank(Rank);
    array<int> &Pairs = GridIDPairRecvData(Rank);
    for (int iPair = 0; iPair < NumPairs; ++iPair) {
      int MGridID = Pairs(2*iPair);
      int NGridID = Pairs(2*iPair+1);
      OverlappingNGridIDsAndRanksForLocalMGrid(MGridID).Fetch(NGridID).Insert(Rank);
    }
  }

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done establishing communication between "
      "potentially-overlapping ranks.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Transferring coordinate data...");
  }

  elem_set<int,2> MGridDataSends;

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      const set<int> &NGridRanks = NEntry.Value();
      for (int Rank : NGridRanks) {
        if (Rank != Domain.Comm().Rank()) {
          MGridDataSends.Insert({MGridID,Rank});
        }
      }
    }
  }

  elem_set<int,2> MGridDataRecvs;

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank != Domain.Comm().Rank()) {
          MGridDataRecvs.Insert({MGridID,Rank});
        }
      }
    }
  }

  struct partition_data {
    range ExtendedRange;
    range CellLocalRange;
    range CellExtendedRange;
    range CellCoverRange;
  };

  elem_map<int,2,partition_data> MGridPartitionData;

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        MGridPartitionData.Insert({MGridID,Rank});
      }
    }
  }

  MPIRequests.Reserve(6*(MGridDataSends.Count() + MGridDataRecvs.Count()));

  for (auto &MGridIDAndRankPair : MGridDataRecvs) {
    int Rank = MGridIDAndRankPair(1);
    partition_data &Data = MGridPartitionData(MGridIDAndRankPair);
    MPI_Irecv(Data.ExtendedRange.Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(Data.ExtendedRange.End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(Data.CellLocalRange.Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(Data.CellLocalRange.End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(Data.CellExtendedRange.Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(Data.CellExtendedRange.End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
  }

  for (auto &MGridIDAndSendToRankPair : MGridDataSends) {
    int MGridID = MGridIDAndSendToRankPair(0);
    int Rank = MGridIDAndSendToRankPair(1);
    const grid &MGrid = Domain.Grid(MGridID);
    MPI_Isend(MGrid.ExtendedRange().Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(MGrid.ExtendedRange().End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(MGrid.CellLocalRange().Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(MGrid.CellLocalRange().End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(MGrid.CellExtendedRange().Begin().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(MGrid.CellExtendedRange().End().Data(), MAX_DIMS, MPI_INT, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank == Domain.Comm().Rank()) {
          const grid &MGrid = Domain.Grid(MGridID);
          partition_data &Data = MGridPartitionData({MGridID,Rank});
          Data.ExtendedRange = MGrid.ExtendedRange();
          Data.CellLocalRange = MGrid.CellLocalRange();
          Data.CellExtendedRange = MGrid.CellExtendedRange();
        }
      }
    }
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      const grid_info &MGridInfo = Domain.GridInfo(MGridID);
      const cart &CellCart = MGridInfo.CellCart();
      for (int Rank : MGridRanks) {
        partition_data &Data = MGridPartitionData({MGridID,Rank});
        Data.CellCoverRange = MakeCellCoverRange(CellCart, Data.CellLocalRange);
      }
    }
  }

  struct cell_coord_data {
    array<field<double>> Coords;
    geometry_type GeometryType;
    field<bool> CellActiveMask;
  };

  elem_map<int,2,cell_coord_data> CellCoordData;

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        const partition_data &PartitionData = MGridPartitionData({MGridID,Rank});
        cell_coord_data &CoordData = CellCoordData.Insert({MGridID,Rank});
        CoordData.Coords.Resize({MAX_DIMS});
        CoordData.Coords(0).Resize(PartitionData.ExtendedRange);
        CoordData.Coords(1).Resize(PartitionData.ExtendedRange);
        CoordData.Coords(2).Resize(PartitionData.ExtendedRange);
        CoordData.CellActiveMask.Resize(PartitionData.CellExtendedRange);
      }
    }
  }

  MPIRequests.Reserve(5*(MGridDataSends.Count() + MGridDataRecvs.Count()));

  for (auto &MGridIDAndRankPair : MGridDataRecvs) {
    int Rank = MGridIDAndRankPair(1);
    const partition_data &PartitionData = MGridPartitionData(MGridIDAndRankPair);
    cell_coord_data &CoordData = CellCoordData(MGridIDAndRankPair);
    long long NumExtended = PartitionData.ExtendedRange.Count();
    long long NumCellExtended = PartitionData.CellExtendedRange.Count();
    MPI_Irecv(CoordData.Coords(0).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(CoordData.Coords(1).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(CoordData.Coords(2).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Irecv(&CoordData.GeometryType, 1, core::GetMPIDataType<geometry_type>(), Rank, 0,
      Domain.Comm(), &MPIRequests.Append());
    MPI_Irecv(CoordData.CellActiveMask.Data(), NumCellExtended, MPI_C_BOOL, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
  }

  // Need to store geometry type values somewhere for Isend buffer
  map<int,geometry_type> GeometryTypeStorage;

  for (int MGridID : Domain.LocalGridIDs()) {
    const geometry &Geometry = GeometryComponent.Geometry(MGridID);
    GeometryTypeStorage.Insert(MGridID, Geometry.Type());
  }

  for (auto &MGridIDAndSendToRankPair : MGridDataSends) {
    int MGridID = MGridIDAndSendToRankPair(0);
    int Rank = MGridIDAndSendToRankPair(1);
    const grid &MGrid = Domain.Grid(MGridID);
    const geometry &Geometry = GeometryComponent.Geometry(MGridID);
    const array<distributed_field<double>> &Coords = Geometry.Coords();
    const local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(MGridID);
    const distributed_field<bool> &CellActiveMask = GridAuxData.CellActiveMask;
    long long NumExtended = MGrid.ExtendedRange().Count();
    long long NumCellExtended = MGrid.CellExtendedRange().Count();
    MPI_Isend(Coords(0).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(Coords(1).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(Coords(2).Data(), NumExtended, MPI_DOUBLE, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
    MPI_Isend(&GeometryTypeStorage(MGridID), 1, core::GetMPIDataType<geometry_type>(), Rank, 0,
      Domain.Comm(), &MPIRequests.Append());
    MPI_Isend(CellActiveMask.Data(), NumCellExtended, MPI_C_BOOL, Rank, 0, Domain.Comm(),
      &MPIRequests.Append());
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank == Domain.Comm().Rank()) {
          const geometry &Geometry = GeometryComponent.Geometry(MGridID);
          const array<distributed_field<double>> &Coords = Geometry.Coords();
          const local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(MGridID);
          const distributed_field<bool> &CellActiveMask = GridAuxData.CellActiveMask;
          cell_coord_data &Data = CellCoordData({MGridID,Rank});
          Data.GeometryType = Geometry.Type();
          Data.Coords(0).Fill(Coords(0));
          Data.Coords(1).Fill(Coords(1));
          Data.Coords(2).Fill(Coords(2));
          Data.CellActiveMask.Fill(CellActiveMask);
        }
      }
    }
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done transferring coordinate data.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Searching for overlapping cells...");
  }

  // Brute force for now
  auto FindOverlappingCell = [NumDims](const range &CellRange, const array<field<double>> &Coords,
    geometry_type GeometryType, const field<bool> &CellActiveMask, double Tolerance, const
    tuple<double> &PointCoords) -> optional<tuple<int>> {
    for (int k = CellRange.Begin(2); k < CellRange.End(2); ++k) {
      for (int j = CellRange.Begin(1); j < CellRange.End(1); ++j) {
        for (int i = CellRange.Begin(0); i < CellRange.End(0); ++i) {
          tuple<int> Cell = {i,j,k};
          if (!CellActiveMask(Cell)) continue;
          if (core::OverlapsCell(NumDims, Coords, GeometryType, Tolerance, Cell, PointCoords)) {
            return Cell;
          }
        }
      }
    }
    return {};
  };

  struct overlapping_cell_data {
    bool Allocated = false;
    field_indexer Indexer;
    field<long long> Cells;
    overlapping_cell_data() = default;
    overlapping_cell_data(const range &MGridCellGlobalRange, const range &LocalRange):
      Allocated(true),
      Indexer(MGridCellGlobalRange),
      Cells(LocalRange, NO_CELL)
    {}
  };

  elem_map<int,2,overlapping_cell_data> OverlappingCellData;

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    const local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(NGridID);
    const distributed_field<bool> &ActiveMask = GridAuxData.ActiveMask;
    const geometry &Geometry = GeometryComponent.Geometry(NGridID);
    auto &Coords = Geometry.Coords();
    field<int> &BinIndices = LocalPointOverlappingBinIndices(NGridID);
    for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
      for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
        for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
          tuple<int> Point = {i,j,k};
          if (!ActiveMask(Point)) continue;
          int BinIndex = BinIndices(Point);
          if (BinIndex < 0) continue;
          bounding_box_hash_bin &Bin = Bins(BinIndex);
          tuple<double> PointCoords = {
            Coords(0)(Point),
            Coords(1)(Point),
            Coords(2)(Point)
          };
          for (auto &Region : Bin.Regions()) {
            int MGridID = Region.Tag;
            if (!Region.Extents.Contains(PointCoords) || !Options_.Overlappable({MGridID,NGridID}))
              continue;
            elem<int,2> IDPair = {MGridID,NGridID};
            const partition_data &PartitionData = MGridPartitionData({MGridID,Region.Rank});
            const cell_coord_data &CoordData = CellCoordData({MGridID,Region.Rank});
            auto MaybeCell = FindOverlappingCell(PartitionData.CellLocalRange, CoordData.Coords,
              CoordData.GeometryType, CoordData.CellActiveMask, Options_.OverlapTolerance(IDPair),
              PointCoords);
            if (MaybeCell) {
              const tuple<int> &Cell = *MaybeCell;
              overlapping_cell_data &CellData = OverlappingCellData.Fetch(IDPair);
              if (!CellData.Allocated) {
                const range &MGridCellGlobalRange = Domain.GridInfo(MGridID).CellGlobalRange();
                CellData = overlapping_cell_data(MGridCellGlobalRange, LocalRange);
              }
              CellData.Cells(Point) = CellData.Indexer.ToIndex(Cell);
            }
          }
        }
      }
    }
  }

  map<int,elem_map<int,2,long long>> NumOverlappingFromMGridAndRankForLocalNGrid;
  map<int,elem_map<int,2,long long>> NumOverlappingFromNGridAndRankForLocalMGrid;

  int NumSends = 0;
  for (int NGridID : Domain.LocalGridIDs()) {
    auto &NumFromMGridAndRank = NumOverlappingFromMGridAndRankForLocalNGrid.Insert(NGridID);
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        NumFromMGridAndRank.Insert({MGridID,Rank}, 0);
        ++NumSends;
      }
    }
  }

  int NumRecvs = 0;
  for (int MGridID : Domain.LocalGridIDs()) {
    auto &NumFromNGridAndRank = NumOverlappingFromNGridAndRankForLocalMGrid.Insert(MGridID);
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      const set<int> &NGridRanks = NEntry.Value();
      for (int Rank : NGridRanks) {
        NumFromNGridAndRank.Insert({NGridID,Rank}, 0);
        ++NumRecvs;
      }
    }
  }

  MPIRequests.Reserve(NumSends + NumRecvs);

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    auto &NumFromNGridAndRank = NumOverlappingFromNGridAndRankForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      set<int> &NGridRanks = NEntry.Value();
      for (int Rank : NGridRanks) {
        long long &NumOverlapping = NumFromNGridAndRank({NGridID,Rank});
        MPI_Irecv(&NumOverlapping, 1, MPI_LONG_LONG, Rank, MGridID, Domain.Comm(),
          &MPIRequests.Append());
      }
    }
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    auto &NumFromMGridAndRank = NumOverlappingFromMGridAndRankForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const grid_info &MGridInfo = Domain.GridInfo(MGridID);
      const cart &MGridCart = MGridInfo.Cart();
      set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        long long &NumOverlapping = NumFromMGridAndRank({MGridID,Rank});
        const partition_data &PartitionData = MGridPartitionData({MGridID,Rank});
        auto Iter = OverlappingCellData.Find({MGridID,NGridID});
        if (Iter != OverlappingCellData.End()) {
          const overlapping_cell_data &CellData = Iter->Value();
          for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
            for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
              for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
                tuple<int> Point = {i,j,k};
                if (CellData.Cells(Point) != NO_CELL) {
                  tuple<int> Cell = CellData.Indexer.ToTuple(CellData.Cells(Point));
                  if (MGridCart.MapToRange(PartitionData.CellCoverRange, Cell).Present()) {
                    ++NumOverlapping;
                  }
                }
              }
            }
          }
        }
        MPI_Isend(&NumOverlapping, 1, MPI_LONG_LONG, Rank, MGridID, Domain.Comm(),
          &MPIRequests.Append());
      }
    }
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    auto &NumFromMGridAndRank = NumOverlappingFromMGridAndRankForLocalNGrid(NGridID);
    NumFromMGridAndRank.EraseIf([](const typename elem_map<int,2,long long>::entry &Entry) -> bool {
      return Entry.Value() == 0;
    });
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      set<int> &MGridRanks = MEntry.Value();
      MGridRanks.EraseIf([&](int Rank) -> bool {
        return !NumFromMGridAndRank.Contains({MGridID,Rank});
      });
    }
    MGridIDsAndRanks.EraseIf([](const typename map<int,set<int>>::entry &MEntry) -> bool {
      return MEntry.Value().Empty();
    });
  }

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    auto &NumFromNGridAndRank = NumOverlappingFromNGridAndRankForLocalMGrid(MGridID);
    NumFromNGridAndRank.EraseIf([](const typename elem_map<int,2,long long>::entry &Entry) -> bool {
      return Entry.Value() == 0;
    });
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      set<int> &NGridRanks = NEntry.Value();
      NGridRanks.EraseIf([&](int Rank) -> bool {
        return !NumFromNGridAndRank.Contains({NGridID,Rank});
      });
    }
    NGridIDsAndRanks.EraseIf([](const typename map<int,set<int>>::entry &NEntry) -> bool {
      return NEntry.Value().Empty();
    });
  }

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    elem_map<int,2,long long> NumOverlappedByMGridForLocalNGrid;
    for (int NGridID : Domain.LocalGridIDs()) {
      const grid &NGrid = Domain.Grid(NGridID);
      auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
      for (int MGridID : Domain.GridIDs()) {
        elem<int,2> OverlapID = {MGridID,NGridID};
        if (!Options_.Overlappable(OverlapID)) continue;
        long long &NumOverlapped = NumOverlappedByMGridForLocalNGrid.Insert(OverlapID, 0);
        if (MGridIDsAndRanks.Contains(MGridID)) {
          const overlapping_cell_data &CellData = OverlappingCellData(OverlapID);
          for (long long l = 0; l < CellData.Cells.Count(); ++l) {
            if (CellData.Cells[l] != NO_CELL) {
              ++NumOverlapped;
            }
          }
        }
        MPI_Allreduce(MPI_IN_PLACE, &NumOverlapped, 1, MPI_LONG_LONG, MPI_SUM, NGrid.Comm());
      }
    }
    for (int MGridID : Domain.GridIDs()) {
      for (int NGridID : Domain.GridIDs()) {
        elem<int,2> OverlapID = {MGridID,NGridID};
        if (Options_.Overlappable(OverlapID) && Domain.GridIsLocal(NGridID)) {
          const grid &NGrid = Domain.Grid(NGridID);
          long long NumOverlapped = NumOverlappedByMGridForLocalNGrid(OverlapID);
          if (NumOverlapped > 0) {
            const grid_info &MGridInfo = Domain.GridInfo(MGridID);
            std::string NumOverlappedString = core::FormatNumber(NumOverlapped, "points", "point");
            Logger.LogDebug(NGrid.Comm().Rank() == 0, 3, "Detected %s overlapped by grid %s on grid "
              "%s.", NumOverlappedString, MGridInfo.Name(), NGrid.Name());
          }
        }
        MPI_Barrier(Domain.Comm());
      }
    }
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done searching for overlapping cells.");
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Creating and filling overlap data "
      "structures...");
  }

  elem_set<int,2> OverlappingGridIDs;

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      OverlappingGridIDs.Insert({MGridID,NGridID});
    }
    for (int MGridID : Domain.GridIDs()) {
      elem<int,2> OverlapID = {MGridID,NGridID};
      if (Options_.Overlappable(OverlapID)) {
        int Overlaps = OverlappingGridIDs.Contains(OverlapID);
        if (NGrid.Comm().Rank() > 0) {
          MPI_Reduce(&Overlaps, nullptr, 1, MPI_INT, MPI_MAX, 0, NGrid.Comm());
        } else {
          MPI_Reduce(MPI_IN_PLACE, &Overlaps, 1, MPI_INT, MPI_MAX, 0, NGrid.Comm());
          if (Overlaps) {
            OverlappingGridIDs.Insert(OverlapID);
          }
        }
      }
    }
  }

  for (int NGridID : Domain.GridIDs()) {
    bool IsNGridRoot = false;
    int NGridRootRank;
    if (Domain.GridIsLocal(NGridID)) {
      const grid &NGrid = Domain.Grid(NGridID);
      IsNGridRoot = NGrid.Comm().Rank() == 0;
      if (IsNGridRoot) {
        NGridRootRank = Domain.Comm().Rank();
      }
    }
    core::BroadcastAnySource(&NGridRootRank, 1, MPI_INT, IsNGridRoot, Domain.Comm());
    for (int MGridID : Domain.GridIDs()) {
      elem<int,2> OverlapID = {MGridID,NGridID};
      if (Options_.Overlappable(OverlapID)) {
        int Overlaps;
        if (IsNGridRoot) Overlaps = OverlappingGridIDs.Contains(OverlapID);
        MPI_Bcast(&Overlaps, 1, MPI_INT, NGridRootRank, Domain.Comm());
        if (Overlaps) {
          OverlappingGridIDs.Insert(OverlapID);
        }
      }
    }
  }

  auto OverlapComponentEditHandle = Domain.EditComponent<overlap_component>(OverlapComponentID_);
  overlap_component &OverlapComponent = *OverlapComponentEditHandle;

  OverlapComponent.ClearOverlaps();
  OverlapComponent.CreateOverlaps(OverlappingGridIDs);

  struct overlap_m_data {
    long long NumOverlapping;
    array<int,2> Cells;
    array<double,2> Coords;
    array<int,2> Destinations;
    explicit overlap_m_data(long long NumOverlapping_):
      NumOverlapping(NumOverlapping_),
      Cells({{MAX_DIMS,NumOverlapping_}}),
      Coords({{MAX_DIMS,NumOverlapping_}}),
      Destinations({{MAX_DIMS,NumOverlapping_}})
    {}
  };

  map<int,elem_map<int,2,overlap_m_data>> OverlapMSendDataForLocalNGrid;
  map<int,elem_map<int,2,overlap_m_data>> OverlapMRecvDataForLocalMGrid;
  elem_map<int,2,overlap_m_data> OverlapMLocalToLocalData;

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &OverlapMSendData = OverlapMSendDataForLocalNGrid.Insert(NGridID);
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    auto &NumFromMGridAndRank = NumOverlappingFromMGridAndRankForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        long long NumOverlapping = NumFromMGridAndRank({MGridID,Rank});
        if (Rank != Domain.Comm().Rank()) {
          OverlapMSendData.Insert({MGridID,Rank}, NumOverlapping);
        } else {
          OverlapMLocalToLocalData.Insert({MGridID,NGridID}, NumOverlapping);
        }
      }
    }
  }

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &OverlapMRecvData = OverlapMRecvDataForLocalMGrid.Insert(MGridID);
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    auto &NumFromNGridAndRank = NumOverlappingFromNGridAndRankForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      const set<int> &NGridRanks = NEntry.Value();
      for (int Rank : NGridRanks) {
        if (Rank != Domain.Comm().Rank()) {
          long long NumOverlapping = NumFromNGridAndRank({NGridID,Rank});
          OverlapMRecvData.Insert({NGridID,Rank}, NumOverlapping);
        }
      }
    }
  }

  NumSends = 0;
  for (int NGridID : Domain.LocalGridIDs()) {
    NumSends += OverlapMSendDataForLocalNGrid(NGridID).Count();
  }

  NumRecvs = 0;
  for (int MGridID : Domain.LocalGridIDs()) {
    NumRecvs += OverlapMRecvDataForLocalMGrid(MGridID).Count();
  }

  MPIRequests.Reserve(3*(NumSends + NumRecvs));

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &OverlapMRecvData = OverlapMRecvDataForLocalMGrid(MGridID);
    for (auto &Entry : OverlapMRecvData) {
      int Rank = Entry.Key()(1);
      overlap_m_data &OverlapMData = Entry.Value();
      long long NumOverlapping = OverlapMData.NumOverlapping;
      MPI_Irecv(OverlapMData.Cells.Data(), MAX_DIMS*NumOverlapping, MPI_INT, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
      MPI_Irecv(OverlapMData.Coords.Data(), MAX_DIMS*NumOverlapping, MPI_DOUBLE, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
      MPI_Irecv(OverlapMData.Destinations.Data(), MAX_DIMS*NumOverlapping, MPI_INT, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
    }
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    const geometry &Geometry = GeometryComponent.Geometry(NGridID);
    auto &Coords = Geometry.Coords();
    auto &OverlapMSendData = OverlapMSendDataForLocalNGrid(NGridID);
    for (auto &Entry : OverlapMSendData) {
      int MGridID = Entry.Key()(0);
      int Rank = Entry.Key()(1);
      const grid_info &MGridInfo = Domain.GridInfo(MGridID);
      const cart &MGridCart = MGridInfo.Cart();
      overlap_m_data &OverlapMData = Entry.Value();
      const partition_data &PartitionData = MGridPartitionData({MGridID,Rank});
      const cell_coord_data &CoordData = CellCoordData({MGridID,Rank});
      const overlapping_cell_data &CellData = OverlappingCellData({MGridID,NGridID});
      long long iOverlapping = 0;
      for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
        for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
          for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
            tuple<int> Point = {i,j,k};
            if (CellData.Cells(Point) != NO_CELL) {
              tuple<int> Cell = CellData.Indexer.ToTuple(CellData.Cells(Point));
              auto MaybeCoverCell = MGridCart.MapToRange(PartitionData.CellCoverRange, Cell);
              if (MaybeCoverCell) {
                tuple<int> MappedCell;
                auto MaybeOwnCell = MGridCart.MapToRange(PartitionData.CellLocalRange, Cell);
                if (MaybeOwnCell) {
                  MappedCell = *MaybeOwnCell;
                } else {
                  MappedCell = *MaybeCoverCell;
                }
                tuple<double> PointCoords = {
                  Coords(0)(Point),
                  Coords(1)(Point),
                  Coords(2)(Point)
                };
                OverlapMData.Cells(0,iOverlapping) = MappedCell(0);
                OverlapMData.Cells(1,iOverlapping) = MappedCell(1);
                OverlapMData.Cells(2,iOverlapping) = MappedCell(2);
                auto MaybeLocalCoords = core::CoordsInCell(NumDims, CoordData.Coords,
                  CoordData.GeometryType, MappedCell, PointCoords);
                if (MaybeLocalCoords) {
                  const tuple<double> &LocalCoords = *MaybeLocalCoords;
                  OverlapMData.Coords(0,iOverlapping) = LocalCoords(0);
                  OverlapMData.Coords(1,iOverlapping) = LocalCoords(1);
                  OverlapMData.Coords(2,iOverlapping) = LocalCoords(2);
                } else {
                  Logger.LogWarning(true, "Failed to compute local coordinates of point "
                    "(%i,%i,%i) of grid %s inside cell (%i,%i,%i) of grid %s.", Point(0),
                    Point(1), Point(2), NGrid.Name(), MappedCell(0), MappedCell(1),
                    MappedCell(2), Domain.GridInfo(MGridID).Name());
                }
                OverlapMData.Destinations(0,iOverlapping) = Point(0);
                OverlapMData.Destinations(1,iOverlapping) = Point(1);
                OverlapMData.Destinations(2,iOverlapping) = Point(2);
                ++iOverlapping;
              }
            }
          }
        }
      }
      long long NumOverlapping = OverlapMData.NumOverlapping;
      MPI_Isend(OverlapMData.Cells.Data(), MAX_DIMS*NumOverlapping, MPI_INT, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
      MPI_Isend(OverlapMData.Coords.Data(), MAX_DIMS*NumOverlapping, MPI_DOUBLE, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
      MPI_Isend(OverlapMData.Destinations.Data(), MAX_DIMS*NumOverlapping, MPI_INT, Rank, MGridID,
        Domain.Comm(), &MPIRequests.Append());
    }
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    const geometry &Geometry = GeometryComponent.Geometry(NGridID);
    auto &Coords = Geometry.Coords();
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const grid_info &MGridInfo = Domain.GridInfo(MGridID);
      const cart &MGridCart = MGridInfo.Cart();
      const set<int> &MGridRanks = MEntry.Value();
      for (int Rank : MGridRanks) {
        if (Rank != Domain.Comm().Rank()) continue;
        const partition_data &PartitionData = MGridPartitionData({MGridID,Rank});
        const cell_coord_data &CoordData = CellCoordData({MGridID,Rank});
        const overlapping_cell_data &CellData = OverlappingCellData({MGridID,NGridID});
        overlap_m_data &OverlapMData = OverlapMLocalToLocalData({MGridID,NGridID});
        long long iOverlapping = 0;
        for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
          for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
            for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
              tuple<int> Point = {i,j,k};
              if (CellData.Cells(Point) != NO_CELL) {
                tuple<int> Cell = CellData.Indexer.ToTuple(CellData.Cells(Point));
                auto MaybeCoverCell = MGridCart.MapToRange(PartitionData.CellCoverRange, Cell);
                if (MaybeCoverCell) {
                  tuple<int> MappedCell;
                  auto MaybeOwnCell = MGridCart.MapToRange(PartitionData.CellLocalRange, Cell);
                  if (MaybeOwnCell) {
                    MappedCell = *MaybeOwnCell;
                  } else {
                    MappedCell = *MaybeCoverCell;
                  }
                  tuple<double> PointCoords = {
                    Coords(0)(Point),
                    Coords(1)(Point),
                    Coords(2)(Point)
                  };
                  OverlapMData.Cells(0,iOverlapping) = MappedCell(0);
                  OverlapMData.Cells(1,iOverlapping) = MappedCell(1);
                  OverlapMData.Cells(2,iOverlapping) = MappedCell(2);
                  auto MaybeLocalCoords = core::CoordsInCell(NumDims, CoordData.Coords,
                    CoordData.GeometryType, MappedCell, PointCoords);
                  if (MaybeLocalCoords) {
                    const tuple<double> &LocalCoords = *MaybeLocalCoords;
                    OverlapMData.Coords(0,iOverlapping) = LocalCoords(0);
                    OverlapMData.Coords(1,iOverlapping) = LocalCoords(1);
                    OverlapMData.Coords(2,iOverlapping) = LocalCoords(2);
                  } else {
                    Logger.LogWarning(true, "Failed to compute local coordinates of point "
                      "(%i,%i,%i) of grid %s inside cell (%i,%i,%i) of grid %s.", Point(0),
                      Point(1), Point(2), NGrid.Name(), MappedCell(0), MappedCell(1),
                      MappedCell(2), Domain.GridInfo(MGridID).Name());
                  }
                  OverlapMData.Destinations(0,iOverlapping) = Point(0);
                  OverlapMData.Destinations(1,iOverlapping) = Point(1);
                  OverlapMData.Destinations(2,iOverlapping) = Point(2);
                  ++iOverlapping;
                }
              }
            }
          }
        }
      }
    }
  }

  MPI_Waitall(MPIRequests.Count(), MPIRequests.Data(), MPI_STATUSES_IGNORE);

  MPIRequests.Clear();

  struct overlap_m_edit {
    edit_handle<overlap_m> Overlap;
    long long NumOverlapping = 0;
    edit_handle<array<int,2>> Cells;
    edit_handle<array<double,2>> Coords;
    edit_handle<array<int,2>> Destinations;
    edit_handle<array<int>> DestinationRanks;
  };

  struct overlap_n_edit {
    edit_handle<overlap_n> Overlap;
    long long NumOverlapping = 0;
    edit_handle<array<int,2>> Points;
    edit_handle<array<int,2>> Sources;
    edit_handle<array<int>> SourceRanks;
  };

  elem_map<int,2,overlap_m_edit> OverlapMEdits;
  elem_map<int,2,overlap_n_edit> OverlapNEdits;

  for (auto &OverlapID : OverlapComponent.LocalOverlapMIDs()) {
    overlap_m_edit &Edit = OverlapMEdits.Insert(OverlapID);
    Edit.Overlap = OverlapComponent.EditOverlapM(OverlapID);
  }

  for (auto &OverlapID : OverlapComponent.LocalOverlapNIDs()) {
    overlap_n_edit &Edit = OverlapNEdits.Insert(OverlapID);
    Edit.Overlap = OverlapComponent.EditOverlapN(OverlapID);
  }

  for (int MGridID : Domain.LocalGridIDs()) {
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    auto &NumFromNGridAndRank = NumOverlappingFromNGridAndRankForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      const set<int> &NGridRanks = NEntry.Value();
      for (int Rank : NGridRanks) {
        long long NumOverlapping = NumFromNGridAndRank({NGridID,Rank});
        OverlapMEdits({MGridID,NGridID}).NumOverlapping += NumOverlapping;
      }
    }
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const overlapping_cell_data &CellData = OverlappingCellData({MGridID,NGridID});
      long long NumOverlapping = 0;
      for (long long l = 0; l < CellData.Cells.Count(); ++l) {
        if (CellData.Cells[l] != NO_CELL) {
          ++NumOverlapping;
        }
      }
      OverlapNEdits({MGridID,NGridID}).NumOverlapping = NumOverlapping;
    }
  }

  for (auto &Entry : OverlapMEdits) {
    overlap_m_edit &Edit = Entry.Value();
    Edit.Overlap->Resize(Edit.NumOverlapping);
    Edit.Cells = Edit.Overlap->EditCells();
    Edit.Coords = Edit.Overlap->EditCoords();
    Edit.Destinations = Edit.Overlap->EditDestinations();
    Edit.DestinationRanks = Edit.Overlap->EditDestinationRanks();
  }

  for (auto &Entry : OverlapNEdits) {
    overlap_n_edit &Edit = Entry.Value();
    Edit.Overlap->Resize(Edit.NumOverlapping);
    Edit.Points = Edit.Overlap->EditPoints();
    Edit.Sources = Edit.Overlap->EditSources();
    Edit.SourceRanks = Edit.Overlap->EditSourceRanks();
  }

  for (int MGridID : Domain.LocalGridIDs()) {
    const grid &MGrid = Domain.Grid(MGridID);
    const range &CellLocalRange = MGrid.CellLocalRange();
    auto &NGridIDsAndRanks = OverlappingNGridIDsAndRanksForLocalMGrid(MGridID);
    auto &OverlapMRecvData = OverlapMRecvDataForLocalMGrid(MGridID);
    for (auto &NEntry : NGridIDsAndRanks) {
      int NGridID = NEntry.Key();
      const set<int> &NGridRanks = NEntry.Value();
      field_indexer NGridGlobalIndexer(Domain.GridInfo(NGridID).GlobalRange());
      overlap_m_edit &Edit = OverlapMEdits({MGridID,NGridID});
      long long NumOverlapping = Edit.Overlap->Count();
      // Want to have the same order as overlap N data
      array<long long> DestinationPointIndices;
      DestinationPointIndices.Reserve(NumOverlapping);
      for (int Rank : NGridRanks) {
        overlap_m_data *OverlapMData;
        if (Rank != Domain.Comm().Rank()) {
          OverlapMData = &OverlapMRecvData({NGridID,Rank});
        } else {
          OverlapMData = &OverlapMLocalToLocalData({MGridID,NGridID});
        }
        for (long long iFromRank = 0; iFromRank < OverlapMData->NumOverlapping; ++iFromRank) {
          tuple<int> DestinationPoint = {
            OverlapMData->Destinations(0,iFromRank),
            OverlapMData->Destinations(1,iFromRank),
            OverlapMData->Destinations(2,iFromRank)
          };
          DestinationPointIndices.Append(NGridGlobalIndexer.ToIndex(DestinationPoint));
        }
      }
      array<long long> ROrder = ArrayOrder(DestinationPointIndices);
      // Need to use order on LHS since RHS is not one contiguous array
      array<long long> LOrder({NumOverlapping});
      for (long long iOverlapping = 0; iOverlapping < NumOverlapping; ++iOverlapping) {
        LOrder(ROrder(iOverlapping)) = iOverlapping;
      }
      long long iOverlapping = 0;
      for (int Rank : NGridRanks) {
        overlap_m_data *OverlapMData;
        if (Rank != Domain.Comm().Rank()) {
          OverlapMData = &OverlapMRecvData({NGridID,Rank});
        } else {
          OverlapMData = &OverlapMLocalToLocalData({MGridID,NGridID});
        }
        for (long long iFromRank = 0; iFromRank < OverlapMData->NumOverlapping; ++iFromRank) {
          long long iOrder = LOrder(iOverlapping);
          tuple<int> Cell = {
            OverlapMData->Cells(0,iFromRank),
            OverlapMData->Cells(1,iFromRank),
            OverlapMData->Cells(2,iFromRank)
          };
          (*Edit.Cells)(0,iOrder) = Cell(0);
          (*Edit.Cells)(1,iOrder) = Cell(1);
          (*Edit.Cells)(2,iOrder) = Cell(2);
          (*Edit.Coords)(0,iOrder) = OverlapMData->Coords(0,iFromRank);
          (*Edit.Coords)(1,iOrder) = OverlapMData->Coords(1,iFromRank);
          (*Edit.Coords)(2,iOrder) = OverlapMData->Coords(2,iFromRank);
          (*Edit.Destinations)(0,iOrder) = OverlapMData->Destinations(0,iFromRank);
          (*Edit.Destinations)(1,iOrder) = OverlapMData->Destinations(1,iFromRank);
          (*Edit.Destinations)(2,iOrder) = OverlapMData->Destinations(2,iFromRank);
          (*Edit.DestinationRanks)(iOrder) = CellLocalRange.Contains(Cell) ? Rank : -1;
          ++iOverlapping;
        }
      }
    }
  }

  for (int NGridID : Domain.LocalGridIDs()) {
    const grid &NGrid = Domain.Grid(NGridID);
    const range &LocalRange = NGrid.LocalRange();
    auto &MGridIDsAndRanks = OverlappingMGridIDsAndRanksForLocalNGrid(NGridID);
    for (auto &MEntry : MGridIDsAndRanks) {
      int MGridID = MEntry.Key();
      const set<int> &MGridRanks = MEntry.Value();
      overlap_n_edit &Edit = OverlapNEdits({MGridID,NGridID});
      const overlapping_cell_data &CellData = OverlappingCellData({MGridID,NGridID});
      long long iOverlapping = 0;
      for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
        for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
          for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
            tuple<int> Point = {i,j,k};
            if (CellData.Cells(Point) != NO_CELL) {
              tuple<int> Cell = CellData.Indexer.ToTuple(CellData.Cells(Point));
              int SourceRank = -1;
              for (int Rank : MGridRanks) {
                const partition_data &PartitionData = MGridPartitionData({MGridID,Rank});
                if (PartitionData.CellCoverRange.Contains(Cell)) {
                  SourceRank = Rank;
                  break;
                }
              }
              (*Edit.Points)(0,iOverlapping) = Point(0);
              (*Edit.Points)(1,iOverlapping) = Point(1);
              (*Edit.Points)(2,iOverlapping) = Point(2);
              (*Edit.Sources)(0,iOverlapping) = Cell(0);
              (*Edit.Sources)(1,iOverlapping) = Cell(1);
              (*Edit.Sources)(2,iOverlapping) = Cell(2);
              (*Edit.SourceRanks)(iOverlapping) = SourceRank;
              ++iOverlapping;
            }
          }
        }
      }
    }
  }

  OverlapMEdits.Clear();
  OverlapNEdits.Clear();

  for (auto &OverlapID : OverlapComponent.LocalOverlapMIDs()) {
    AssemblyData.LocalOverlapMAuxData.Insert(OverlapID);
  }

  for (auto &OverlapID : OverlapComponent.LocalOverlapNIDs()) {
    AssemblyData.LocalOverlapNAuxData.Insert(OverlapID);
  }

  for (auto &OverlapID : OverlapComponent.LocalOverlapNIDs()) {
    int NGridID = OverlapID(1);
    const grid &NGrid = Domain.Grid(NGridID);
    local_overlap_n_aux_data &OverlapNAuxData = AssemblyData.LocalOverlapNAuxData(OverlapID);
    distributed_field<bool> &OverlapMask = OverlapNAuxData.OverlapMask;
    const overlap_n &OverlapN = OverlapComponent.OverlapN(OverlapID);
    const array<int,2> &Points = OverlapN.Points();
    OverlapMask.Assign(NGrid.SharedPartition(), false);
    for (long long iOverlapping = 0; iOverlapping < OverlapN.Count(); ++iOverlapping) {
      tuple<int> Point = {
        Points(0,iOverlapping),
        Points(1,iOverlapping),
        Points(2,iOverlapping)
      };
      OverlapMask(Point) = true;
    }
    OverlapMask.Exchange();
  }

  if (Logger.LoggingDebug()) {
    MPI_Barrier(Domain.Comm());
    Logger.LogDebug(Domain.Comm().Rank() == 0, 2, "Done creating and filling overlap data "
      "structures.");
  }

  MPI_Barrier(Domain.Comm());

  Logger.LogDebug(Domain.Comm().Rank() == 0, 1, "Done detecting overlap between grids.");

}

void assembler::InferBoundaries_() {

  domain &Domain = *Domain_;
  core::logger &Logger = Context_->core_Logger();

  MPI_Barrier(Domain.Comm());

  Logger.LogDebug(Domain.Comm().Rank() == 0, 1, "Inferring non-overlapping boundaries...");

  auto &OverlapComponent = Domain.Component<overlap_component>(OverlapComponentID_);
  assembly_data &AssemblyData = *AssemblyData_;

  auto StateComponentEditHandle = Domain.EditComponent<state_component>(StateComponentID_);
  state_component &StateComponent = *StateComponentEditHandle;

  map<int,long long> NumInferredForGrid;
  if (Logger.LoggingDebug()) {
    for (int GridID : Domain.LocalGridIDs()) {
      NumInferredForGrid.Insert(GridID, 0);
    }
  }

  for (int GridID : Domain.LocalGridIDs()) {
    if (!Options_.InferBoundaries(GridID)) continue;
    const grid &Grid = Domain.Grid(GridID);
    const range &LocalRange = Grid.LocalRange();
    long long NumExtended = Grid.ExtendedRange().Count();
    local_grid_aux_data &GridAuxData = AssemblyData.LocalGridAuxData(GridID);
    const distributed_field<bool> &ActiveMask = GridAuxData.ActiveMask;
    distributed_field<bool> &DomainBoundaryMask = GridAuxData.DomainBoundaryMask;
    auto StateEditHandle = StateComponent.EditState(GridID);
    auto FlagsEditHandle = StateEditHandle->EditFlags();
    distributed_field<state_flags> &Flags = *FlagsEditHandle;
    distributed_field<bool> InferredBoundaryMask(Grid.SharedPartition());
    core::DetectEdge(ActiveMask, core::edge_type::INNER, core::mask_bc::FALSE, false,
      InferredBoundaryMask);
    for (long long l = 0; l < NumExtended; ++l) {
      InferredBoundaryMask[l] = InferredBoundaryMask[l] && !DomainBoundaryMask[l];
    }
    for (auto &OverlapID : OverlapComponent.LocalOverlapNIDs()) {
      if (OverlapID(1) == GridID) {
        const field<bool> &OverlapMask = OverlapComponent.OverlapN(OverlapID).Mask();
        for (int k = LocalRange.Begin(2); k < LocalRange.End(2); ++k) {
          for (int j = LocalRange.Begin(1); j < LocalRange.End(1); ++j) {
            for (int i = LocalRange.Begin(0); i < LocalRange.End(0); ++i) {
              InferredBoundaryMask(i,j,k) = InferredBoundaryMask(i,j,k) && !OverlapMask(i,j,k);
            }
          }
        }
      }
    }
    InferredBoundaryMask.Exchange();
    for (long long l = 0; l < NumExtended; ++l) {
      if (InferredBoundaryMask[l]) {
        Flags[l] |= state_flags::DOMAIN_BOUNDARY | state_flags::INFERRED_DOMAIN_BOUNDARY;
      }
    }
    for (long long l = 0; l < NumExtended; ++l) {
      DomainBoundaryMask[l] = DomainBoundaryMask[l] || InferredBoundaryMask[l];
    }
    if (Logger.LoggingDebug()) {
      NumInferredForGrid.Insert(GridID, core::CountDistributedMask(InferredBoundaryMask));
    }
  }

  if (Logger.LoggingDebug()) {
    for (int GridID : Domain.GridIDs()) {
      if (Domain.GridIsLocal(GridID)) {
        const grid &Grid = Domain.Grid(GridID);
        long long NumInferred = NumInferredForGrid(GridID);
        if (NumInferred > 0) {
          std::string NumInferredString = core::FormatNumber(NumInferred, "points", "point");
          Logger.LogDebug(Grid.Comm().Rank() == 0, 2, "%s marked as boundaries on grid %s.",
            NumInferredString, Grid.Name());
        }
      }
      MPI_Barrier(Domain.Comm());
    }
  }

  MPI_Barrier(Domain.Comm());

  Logger.LogDebug(Domain.Comm().Rank() == 0, 1, "Done inferring non-overlapping boundaries.");

}

namespace {

void GenerateActiveMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &ActiveMask) {

  long long NumExtended = Grid.ExtendedRange().Count();

  ActiveMask.Assign(Grid.SharedPartition());

  for (long long l = 0; l < NumExtended; ++l) {
    ActiveMask[l] = (Flags[l] & state_flags::ACTIVE) != state_flags::NONE;
  }

}

void GenerateCellActiveMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &CellActiveMask) {

  int NumDims = Grid.Dimension();
  const range &CellLocalRange = Grid.CellLocalRange();

  CellActiveMask.Assign(Grid.SharedCellPartition());

  for (int k = CellLocalRange.Begin(2); k < CellLocalRange.End(2); ++k) {
    for (int j = CellLocalRange.Begin(1); j < CellLocalRange.End(1); ++j) {
      for (int i = CellLocalRange.Begin(0); i < CellLocalRange.End(0); ++i) {
        tuple<int> Cell = {i,j,k};
        CellActiveMask(Cell) = true;
        range NeighborRange;
        for (int iDim = 0; iDim < NumDims; ++iDim) {
          NeighborRange.Begin(iDim) = Cell(iDim);
          NeighborRange.End(iDim) = Cell(iDim)+2;
        }
        for (int iDim = NumDims; iDim < MAX_DIMS; ++iDim) {
          NeighborRange.Begin(iDim) = 0;
          NeighborRange.End(iDim) = 1;
        }
        for (int o = NeighborRange.Begin(2); o < NeighborRange.End(2); ++o) {
          for (int n = NeighborRange.Begin(1); n < NeighborRange.End(1); ++n) {
            for (int m = NeighborRange.Begin(0); m < NeighborRange.End(0); ++m) {
              tuple<int> Point = {m,n,o};
              CellActiveMask(Cell) = CellActiveMask(Cell) && (Flags(Point) & state_flags::ACTIVE)
                != state_flags::NONE;
            }
          }
        }
      }
    }
  }

  CellActiveMask.Exchange();

}

void GenerateDomainBoundaryMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &DomainBoundaryMask) {

  long long NumExtended = Grid.ExtendedRange().Count();

  DomainBoundaryMask.Assign(Grid.SharedPartition());

  auto MatchesAll = [](state_flags Flags, state_flags Mask) -> bool {
    return (Flags & Mask) == Mask;
  };

  for (long long l = 0; l < NumExtended; ++l) {
    DomainBoundaryMask[l] = MatchesAll(Flags[l], state_flags::ACTIVE |
      state_flags::DOMAIN_BOUNDARY);
  }

}

void GenerateInternalBoundaryMask(const grid &Grid, const distributed_field<state_flags> &Flags,
  distributed_field<bool> &InternalBoundaryMask) {

  long long NumExtended = Grid.ExtendedRange().Count();

  InternalBoundaryMask.Assign(Grid.SharedPartition());

  auto MatchesAll = [](state_flags Flags, state_flags Mask) -> bool {
    return (Flags & Mask) == Mask;
  };

  for (long long l = 0; l < NumExtended; ++l) {
    InternalBoundaryMask[l] = MatchesAll(Flags[l], state_flags::ACTIVE |
      state_flags::INTERNAL_BOUNDARY);
  }

}

}

}
