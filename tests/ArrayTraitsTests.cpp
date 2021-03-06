// Copyright (c) 2020 Matthew J. Smith and Overkit contributors
// License: MIT (http://opensource.org/licenses/MIT)

#include <ovk/core/ArrayTraits.hpp>

#include "tests/MPITest.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <ovk/core/Comm.hpp>
#include <ovk/core/Elem.hpp>

#include <mpi.h>

#include <array>
#include <string>
#include <vector>

using testing::ElementsAre;
using testing::ElementsAreArray;

class ArrayTraitsTests : public tests::mpi_test {};

TEST_F(ArrayTraitsTests, CArrayTraits) {

  if (TestComm().Rank() != 0) return;

  using array = int[1][2][3];

  EXPECT_TRUE(ovk::core::IsArray<array>());
  EXPECT_TRUE((std::is_same<ovk::core::array_value_type<array>, int>::value));
  EXPECT_EQ(ovk::core::ArrayRank<array>(), 3);
  EXPECT_EQ(ovk::core::ArrayLayout<array>(), ovk::array_layout::ROW_MAJOR);
  EXPECT_TRUE(ovk::core::ArrayHasStaticExtents<array>());
  EXPECT_TRUE((ovk::core::StaticArrayHasExtentsBegin<array,0,0,0>()));
  EXPECT_TRUE((ovk::core::StaticArrayHasExtentsEnd<array,1,2,3>()));

  array Array = {{{0,1,2},{3,4,5}}};
  EXPECT_THAT(ovk::core::ArrayExtents(Array).Begin(), ElementsAre(0,0,0));
  EXPECT_THAT(ovk::core::ArrayExtents(Array).End(), ElementsAre(1,2,3));
  EXPECT_EQ(ovk::core::ArrayData(Array), &Array[0][0][0]);

}

TEST_F(ArrayTraitsTests, StdArrayTraits) {

  if (TestComm().Rank() != 0) return;

  using array = std::array<int,5>;

  EXPECT_TRUE(ovk::core::IsArray<array>());
  EXPECT_TRUE((std::is_same<ovk::core::array_value_type<array>, int>::value));
  EXPECT_EQ(ovk::core::ArrayRank<array>(), 1);
  EXPECT_EQ(ovk::core::ArrayLayout<array>(), ovk::array_layout::ROW_MAJOR);
  EXPECT_TRUE(ovk::core::ArrayHasStaticExtents<array>());
  EXPECT_TRUE((ovk::core::StaticArrayHasExtentsBegin<array,0>()));
  EXPECT_TRUE((ovk::core::StaticArrayHasExtentsEnd<array,5>()));

  array Array = {{0,1,2,3,4}};
  EXPECT_THAT(ovk::core::ArrayExtents(Array).Begin(), ElementsAre(0));
  EXPECT_THAT(ovk::core::ArrayExtents(Array).End(), ElementsAre(5));
  EXPECT_EQ(ovk::core::ArrayData(Array), Array.data());

}

TEST_F(ArrayTraitsTests, StdVectorTraits) {

  if (TestComm().Rank() != 0) return;

  using vector = std::vector<int>;

  EXPECT_TRUE(ovk::core::IsArray<vector>());
  EXPECT_TRUE((std::is_same<ovk::core::array_value_type<vector>, int>::value));
  EXPECT_EQ(ovk::core::ArrayRank<vector>(), 1);
  EXPECT_EQ(ovk::core::ArrayLayout<vector>(), ovk::array_layout::ROW_MAJOR);
  EXPECT_TRUE(ovk::core::ArrayHasRuntimeExtents<vector>());

  vector Vector = {0,1,2,3,4};
  EXPECT_THAT(ovk::core::ArrayExtents(Vector).Begin(), ElementsAre(0));
  EXPECT_THAT(ovk::core::ArrayExtents(Vector).End(), ElementsAre(5));
  EXPECT_EQ(ovk::core::ArrayData(Vector), Vector.data());

}

TEST_F(ArrayTraitsTests, StdStringTraits) {

  if (TestComm().Rank() != 0) return;

  EXPECT_TRUE(ovk::core::IsArray<std::string>());
  EXPECT_TRUE((std::is_same<ovk::core::array_value_type<std::string>, char>::value));
  EXPECT_EQ(ovk::core::ArrayRank<std::string>(), 1);
  EXPECT_EQ(ovk::core::ArrayLayout<std::string>(), ovk::array_layout::ROW_MAJOR);
  EXPECT_TRUE(ovk::core::ArrayHasRuntimeExtents<std::string>());

  std::string String = "Hello, world!";
  EXPECT_THAT(ovk::core::ArrayExtents(String).Begin(), ElementsAre(0));
  EXPECT_THAT(ovk::core::ArrayExtents(String).End(), ElementsAre(13));
  EXPECT_EQ(ovk::core::ArrayData(String), String.data());

}
