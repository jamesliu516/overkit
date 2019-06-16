// Copyright (c) 2019 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_SEND_MAP_HPP_INCLUDED
#define OVK_CORE_SEND_MAP_HPP_INCLUDED

#include <ovk/core/Array.hpp>
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

  send_map();
  send_map(long long NumValues, array<long long> SendOrder, const array<int> &DestinationRanks);

  floating_ref<const send_map> GetFloatingRef() const { return FloatingRefGenerator_.Generate(); }
  floating_ref<send_map> GetFloatingRef() { return FloatingRefGenerator_.Generate(); }

  long long Count() const { return SendOrder_.Count(); }

  const array<send> &Sends() const { return Sends_; }

  const array<long long> &SendOrder() const { return SendOrder_; }
  const array<int> &SendIndices() const { return SendIndices_; }

private:

  floating_ref_generator<send_map> FloatingRefGenerator_;

  array<send> Sends_;
  array<long long> SendOrder_;
  array<int> SendIndices_;

};

}}

#endif