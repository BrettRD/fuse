/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2018, Locus Robotics
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */
#include <fuse_core/serialization.h>
#include <fuse_variables/position_2d_stamped.h>
#include <fuse_variables/stamped.h>
#include <fuse_core/time.h>

#include <ceres/autodiff_cost_function.h>
#include <ceres/problem.h>
#include <ceres/solver.h>
#include <gtest/gtest.h>

#include <sstream>
#include <vector>

using fuse_variables::Position2DStamped;


TEST(Position2DStamped, Type)
{
  Position2DStamped variable(fuse_core::TimeStamp(12345678, 910111213));
  EXPECT_EQ("fuse_variables::Position2DStamped", variable.type());
}

TEST(Position2DStamped, UUID)
{
  // Verify two velocities at the same timestamp produce the same UUID
  {
    Position2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213));
    Position2DStamped variable2(fuse_core::TimeStamp(12345678, 910111213));
    EXPECT_EQ(variable1.uuid(), variable2.uuid());

    Position2DStamped variable3(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("c3po"));
    Position2DStamped variable4(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("c3po"));
    EXPECT_EQ(variable3.uuid(), variable4.uuid());
  }

  // Verify two velocities at different timestamps produce different UUIDs
  {
    Position2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213));
    Position2DStamped variable2(fuse_core::TimeStamp(12345678, 910111214));
    Position2DStamped variable3(fuse_core::TimeStamp(12345679, 910111213));
    EXPECT_NE(variable1.uuid(), variable2.uuid());
    EXPECT_NE(variable1.uuid(), variable3.uuid());
    EXPECT_NE(variable2.uuid(), variable3.uuid());
  }

  // Verify two velocities with different hardware IDs produce different UUIDs
  {
    Position2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("r2d2"));
    Position2DStamped variable2(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("bb8"));
    EXPECT_NE(variable1.uuid(), variable2.uuid());
  }
}

TEST(Position2DStamped, Stamped)
{
  fuse_core::Variable::SharedPtr base = Position2DStamped::make_shared(fuse_core::TimeStamp(12345678, 910111213),
                                                                       fuse_core::uuid::generate("mo"));
  auto derived = std::dynamic_pointer_cast<Position2DStamped>(base);
  ASSERT_TRUE(static_cast<bool>(derived));
  EXPECT_EQ(fuse_core::TimeStamp(12345678, 910111213), derived->stamp());
  EXPECT_EQ(fuse_core::uuid::generate("mo"), derived->deviceId());

  auto stamped = std::dynamic_pointer_cast<fuse_variables::Stamped>(base);
  ASSERT_TRUE(static_cast<bool>(stamped));
  EXPECT_EQ(fuse_core::TimeStamp(12345678, 910111213), stamped->stamp());
  EXPECT_EQ(fuse_core::uuid::generate("mo"), stamped->deviceId());
}

struct CostFunctor
{
  CostFunctor() {}

  template <typename T> bool operator()(const T* const x, T* residual) const
  {
    residual[0] = x[0] - T(3.0);
    residual[1] = x[1] + T(8.0);
    return true;
  }
};

TEST(Position2DStamped, Optimization)
{
  // Create a Position2DStamped
  Position2DStamped position(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("hal9000"));
  position.x() = 1.5;
  position.y() = -3.0;

  // Create a simple a constraint
  ceres::CostFunction* cost_function = new ceres::AutoDiffCostFunction<CostFunctor, 2, 2>(new CostFunctor());

  // Build the problem.
  ceres::Problem problem;
  problem.AddParameterBlock(
    position.data(),
    position.size(),
    position.localParameterization());
  std::vector<double*> parameter_blocks;
  parameter_blocks.push_back(position.data());
  problem.AddResidualBlock(
    cost_function,
    nullptr,
    parameter_blocks);

  // Run the solver
  ceres::Solver::Options options;
  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);

  // Check
  EXPECT_NEAR(3.0, position.x(), 1.0e-5);
  EXPECT_NEAR(-8.0, position.y(), 1.0e-5);
}

TEST(Position2DStamped, Serialization)
{
  // Create a Position2DStamped
  Position2DStamped expected(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("hal9000"));
  expected.x() = 1.5;
  expected.y() = -3.0;

  // Serialize the variable into an archive
  std::stringstream stream;
  {
    fuse_core::TextOutputArchive archive(stream);
    expected.serialize(archive);
  }

  // Deserialize a new variable from that same stream
  Position2DStamped actual;
  {
    fuse_core::TextInputArchive archive(stream);
    actual.deserialize(archive);
  }

  // Compare
  EXPECT_EQ(expected.deviceId(), actual.deviceId());
  EXPECT_EQ(expected.stamp(), actual.stamp());
  EXPECT_EQ(expected.x(), actual.x());
  EXPECT_EQ(expected.y(), actual.y());
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
