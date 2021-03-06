// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_SEND_MAP_HPP_INCLUDED
#define OVK_CORE_SEND_MAP_HPP_INCLUDED

#include <ovk/core/Array.hpp>
#include <ovk/core/ArrayView.hpp>
#include <ovk/core/FloatingRef.hpp>
#include <ovk/core/Global.hpp>

namespace ovk {
namespace core {

class send_map {

public:

  struct send {
    int Rank;
    long long NumValues;
  };

  send_map() = default;
  send_map(array_view<const int> DestinationRanks);
  send_map(array_view<const int> DestinationRanks, array<long long> SendOrder);

  floating_ref<const send_map> GetFloatingRef() const {
    return FloatingRefGenerator_.Generate(*this);
  }
  floating_ref<send_map> GetFloatingRef() { return FloatingRefGenerator_.Generate(*this); }

  long long Count() const { return SendOrder_.Count(); }

  const array<send> &Sends() const { return Sends_; }

  const array<long long> &SendOrder() const { return SendOrder_; }
  const array<int> &SendIndices() const { return SendIndices_; }

private:

  floating_ref_generator FloatingRefGenerator_;

  array<send> Sends_;
  array<long long> SendOrder_;
  array<int> SendIndices_;

  static array<long long> MakeDefaultSendOrder_(long long NumValues);

};

}}

#endif
