// Copyright (c) 2018 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_COLLECT_ALL_HPP_INCLUDED
#define OVK_CORE_COLLECT_ALL_HPP_INCLUDED

#include "ovk/core/Array.hpp"
#include "ovk/core/ArrayView.hpp"
#include "ovk/core/CollectBase.hpp"
#include "ovk/core/Comm.hpp"
#include "ovk/core/ConnectivityD.hpp"
#include "ovk/core/Constants.hpp"
#include "ovk/core/Elem.hpp"
#include "ovk/core/Exchange.hpp"
#include "ovk/core/Global.hpp"
#include "ovk/core/Profiler.hpp"
#include "ovk/core/Range.hpp"

#include <mpi.h>

namespace ovk {
namespace core {
namespace collect_internal {

template <typename T, array_layout Layout> class collect_all : public collect_base_for_type<T,
  Layout> {

protected:

  using parent_type = collect_base_for_type<T, Layout>;

  using typename parent_type::donor_indexer;
  using parent_type::Profiler_;
  using parent_type::Count_;
  using parent_type::NumDonors_;
  using parent_type::MaxPointsInCell_;
  using parent_type::GridValues_;
  using parent_type::DonorValues_;

public:

  using typename parent_type::value_type;

  collect_all() = default;
  collect_all(const collect_all &Other) = delete;
  collect_all(collect_all &&Other) noexcept = default;

  collect_all &operator=(const collect_all &Other) = delete;
  collect_all &operator=(collect_all &&Other) noexcept = default;

  void Initialize(const exchange &Exchange, int Count, const range &GridValuesRange) {

    parent_type::Initialize(Exchange, Count, GridValuesRange);

    int MemAllocTime = core::GetProfilerTimerID(*Profiler_, "Collect::MemAlloc");
    core::StartProfile(*Profiler_, MemAllocTime);

    parent_type::AllocateRemoteDonorValues(RemoteDonorValues_);

    DonorPointValues_.Resize({{Count_,MaxPointsInCell_}});

    core::EndProfile(*Profiler_, MemAllocTime);

  }

  void Collect(const void * const *GridValuesVoid, void **DonorValuesVoid) {

    parent_type::SetBufferViews(GridValuesVoid, DonorValuesVoid);
    parent_type::RetrieveRemoteDonorValues(GridValues_, RemoteDonorValues_);

    int ReduceTime = core::GetProfilerTimerID(*Profiler_, "Collect::Reduce");
    core::StartProfile(*Profiler_, ReduceTime);

    for (long long iDonor = 0; iDonor < NumDonors_; ++iDonor) {

      elem<int,MAX_DIMS> DonorSize;
      parent_type::AssembleDonorPointValues(GridValues_, RemoteDonorValues_, iDonor, DonorSize,
        DonorPointValues_);
      int NumDonorPoints = DonorSize[0]*DonorSize[1]*DonorSize[2];

      for (int iCount = 0; iCount < Count_; ++iCount) {
        DonorValues_(iCount)(iDonor) = value_type(true);
        for (int iPointInCell = 0; iPointInCell < NumDonorPoints; ++iPointInCell) {
          DonorValues_(iCount)(iDonor) = DonorValues_(iCount)(iDonor) && DonorPointValues_(iCount,
            iPointInCell);
        }
      }

    }

    core::EndProfile(*Profiler_, ReduceTime);

  }

private:

  array<array<value_type,2>> RemoteDonorValues_;
  array<value_type,2> DonorPointValues_;

};

}}}

#endif