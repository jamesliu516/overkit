// Copyright (c) 2018 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#ifndef OVK_CORE_RECV_HPP_INCLUDED
#define OVK_CORE_RECV_HPP_INCLUDED

#include <ovk/core/Array.hpp>
#include <ovk/core/Comm.hpp>
#include <ovk/core/Constants.hpp>
#include <ovk/core/Connectivity.hpp>
#include <ovk/core/DataType.hpp>
#include <ovk/core/Exchange.hpp>
#include <ovk/core/Global.hpp>
#include <ovk/core/Profiler.hpp>
#include <ovk/core/Request.hpp>

#include <mpi.h>

#include <memory>
#include <utility>

namespace ovk {
namespace core {

class recv {

public:

  recv() = default;

  template <typename T> recv(T &&Recv):
    Recv_(new model<T>(std::forward<T>(Recv)))
  {}

  recv(const recv &Other) = delete;
  recv(recv &&Other) noexcept = default;

  template <typename T> recv &operator=(T &&Recv) {
    Recv_.reset(new model<T>(std::forward<T>(Recv)));
    return *this;
  }

  recv &operator=(const recv &Other) = delete;
  recv &operator=(recv &&Other) noexcept = default;

  void Initialize(const exchange &Exchange, int Count, int Tag) {
    Recv_->Initialize(Exchange, Count, Tag);
  }

  request Recv(void **ReceiverValues) {
    return Recv_->Recv(ReceiverValues);
  }

private:

  class concept {
  public:
    virtual ~concept() {}
    virtual void Initialize(const exchange &Exchange, int Count, int Tag) = 0;
    virtual request Recv(void **ReceiverValues) = 0;
  };

  template <typename T> class model : public concept {
  public:
    using value_type = typename T::value_type;
    explicit model(T Recv):
      Recv_(std::move(Recv))
    {}
    virtual void Initialize(const exchange &Exchange, int Count, int Tag) override {
      Recv_.Initialize(Exchange, Count, Tag);
    }
    virtual request Recv(void **ReceiverValues) override {
      return Recv_.Recv(ReceiverValues);
    }
  private:
    T Recv_;
  };

  std::unique_ptr<concept> Recv_;

};

recv MakeRecv(data_type ValueType);

}}

#endif