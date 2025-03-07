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
#include <fuse_core/autodiff_local_parameterization.h>
#include <fuse_core/util.h>
#include <fuse_variables/orientation_2d_stamped.h>
#include <fuse_variables/stamped.h>
#include <fuse_core/time.h>

#include <ceres/autodiff_cost_function.h>
#include <ceres/problem.h>
#include <ceres/solver.h>
#include <gtest/gtest.h>

#include <cmath>
#include <sstream>
#include <vector>


using fuse_variables::Orientation2DStamped;

TEST(Orientation2DStamped, Type)
{
  Orientation2DStamped variable(fuse_core::TimeStamp(12345678, 910111213));
  EXPECT_EQ("fuse_variables::Orientation2DStamped", variable.type());
}

TEST(Orientation2DStamped, UUID)
{
  // Verify two velocities at the same timestamp produce the same UUID
  {
    Orientation2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213));
    Orientation2DStamped variable2(fuse_core::TimeStamp(12345678, 910111213));
    EXPECT_EQ(variable1.uuid(), variable2.uuid());

    Orientation2DStamped variable3(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("c3po"));
    Orientation2DStamped variable4(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("c3po"));
    EXPECT_EQ(variable3.uuid(), variable4.uuid());
  }

  // Verify two velocities at different timestamps produce different UUIDs
  {
    Orientation2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213));
    Orientation2DStamped variable2(fuse_core::TimeStamp(12345678, 910111214));
    Orientation2DStamped variable3(fuse_core::TimeStamp(12345679, 910111213));
    EXPECT_NE(variable1.uuid(), variable2.uuid());
    EXPECT_NE(variable1.uuid(), variable3.uuid());
    EXPECT_NE(variable2.uuid(), variable3.uuid());
  }

  // Verify two velocities with different hardware IDs produce different UUIDs
  {
    Orientation2DStamped variable1(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("r2d2"));
    Orientation2DStamped variable2(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("bb8"));
    EXPECT_NE(variable1.uuid(), variable2.uuid());
  }
}

TEST(Orientation2DStamped, Stamped)
{
  fuse_core::Variable::SharedPtr base = Orientation2DStamped::make_shared(fuse_core::TimeStamp(12345678, 910111213),
                                                                          fuse_core::uuid::generate("mo"));
  auto derived = std::dynamic_pointer_cast<Orientation2DStamped>(base);
  ASSERT_TRUE(static_cast<bool>(derived));
  EXPECT_EQ(fuse_core::TimeStamp(12345678, 910111213), derived->stamp());
  EXPECT_EQ(fuse_core::uuid::generate("mo"), derived->deviceId());

  auto stamped = std::dynamic_pointer_cast<fuse_variables::Stamped>(base);
  ASSERT_TRUE(static_cast<bool>(stamped));
  EXPECT_EQ(fuse_core::TimeStamp(12345678, 910111213), stamped->stamp());
  EXPECT_EQ(fuse_core::uuid::generate("mo"), stamped->deviceId());
}

struct Orientation2DPlus
{
  template<typename T>
  bool operator()(const T* x, const T* delta, T* x_plus_delta) const
  {
    x_plus_delta[0] = fuse_core::wrapAngle2D(x[0] + delta[0]);
    return true;
  }
};

struct Orientation2DMinus
{
  template<typename T>
  bool operator()(const T* x1, const T* x2, T* delta) const
  {
    delta[0] = fuse_core::wrapAngle2D(x2[0] - x1[0]);
    return true;
  }
};

using Orientation2DLocalParameterization =
    fuse_core::AutoDiffLocalParameterization<Orientation2DPlus, Orientation2DMinus, 1, 1>;

TEST(Orientation2DStamped, Plus)
{
  auto parameterization = Orientation2DStamped(fuse_core::TimeStamp(0, 0)).localParameterization();

  // Simple test
  {
    double x[1] = {1.0};
    double delta[1] = {0.5};
    double actual[1] = {0.0};
    bool success = parameterization->Plus(x, delta, actual);

    EXPECT_TRUE(success);
    EXPECT_NEAR(1.5, actual[0], 1.0e-5);
  }

  // Check roll-over
  {
    double x[1] = {2.0};
    double delta[1] = {3.0};
    double actual[1] = {0.0};
    bool success = parameterization->Plus(x, delta, actual);

    EXPECT_TRUE(success);
    EXPECT_NEAR(5 - 2*M_PI, actual[0], 1.0e-5);
  }

  delete parameterization;
}

TEST(Orientation2DStamped, PlusJacobian)
{
  auto parameterization = Orientation2DStamped(fuse_core::TimeStamp(0, 0)).localParameterization();
  auto reference = Orientation2DLocalParameterization();

  auto test_values = std::vector<double>{-2 * M_PI, -1 * M_PI, -1.0, 0.0, 1.0, M_PI, 2 * M_PI};
  for (auto test_value : test_values)
  {
    double x[1] = {test_value};
    double actual[1] = {0.0};
    bool success = parameterization->ComputeJacobian(x, actual);

    double expected[1] = {0.0};
    reference.ComputeJacobian(x, expected);

    EXPECT_TRUE(success);
    EXPECT_NEAR(expected[0], actual[0], 1.0e-5);
  }

  delete parameterization;
}

TEST(Orientation2DStamped, Minus)
{
  auto parameterization = Orientation2DStamped(fuse_core::TimeStamp(0, 0)).localParameterization();

  // Simple test
  {
    double x1[1] = {1.0};
    double x2[1] = {1.5};
    double actual[1] = {0.0};
    bool success = parameterization->Minus(x1, x2, actual);

    EXPECT_TRUE(success);
    EXPECT_NEAR(0.5, actual[0], 1.0e-5);
  }

  // Check roll-over
  {
    double x1[1] = {2.0};
    double x2[1] = {5 - 2*M_PI};
    double actual[1] = {0.0};
    bool success = parameterization->Minus(x1, x2, actual);

    EXPECT_TRUE(success);
    EXPECT_NEAR(3.0, actual[0], 1.0e-5);
  }
}

TEST(Orientation2DStamped, MinusJacobian)
{
  auto parameterization = Orientation2DStamped(fuse_core::TimeStamp(0, 0)).localParameterization();
  auto reference = Orientation2DLocalParameterization();

  auto test_values = std::vector<double>{-2 * M_PI, -1 * M_PI, -1.0, 0.0, 1.0, M_PI, 2 * M_PI};
  for (auto test_value : test_values)
  {
    double x[1] = {test_value};
    double actual[1] = {0.0};
    bool success = parameterization->ComputeMinusJacobian(x, actual);

    double expected[1] = {0.0};
    reference.ComputeMinusJacobian(x, expected);

    EXPECT_TRUE(success);
    EXPECT_NEAR(expected[0], actual[0], 1.0e-5);
  }

  delete parameterization;
}

struct CostFunctor
{
  CostFunctor() {}

  template <typename T> bool operator()(const T* const x, T* residual) const
  {
    residual[0] = x[0] - T(3.0);
    return true;
  }
};

TEST(Orientation2DStamped, Optimization)
{
  // Create a Orientation2DStamped
  Orientation2DStamped orientation(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("hal9000"));
  orientation.yaw() = 1.5;

  // Create a simple a constraint
  ceres::CostFunction* cost_function = new ceres::AutoDiffCostFunction<CostFunctor, 1, 1>(new CostFunctor());

  // Build the problem.
  ceres::Problem problem;
  problem.AddParameterBlock(
    orientation.data(),
    orientation.size(),
    orientation.localParameterization());
  std::vector<double*> parameter_blocks;
  parameter_blocks.push_back(orientation.data());
  problem.AddResidualBlock(
    cost_function,
    nullptr,
    parameter_blocks);

  // Run the solver
  ceres::Solver::Options options;
  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);

  // Check
  EXPECT_NEAR(3.0, orientation.yaw(), 1.0e-5);
}

TEST(Orientation2DStamped, Serialization)
{
  // Create a Orientation2DStamped
  Orientation2DStamped expected(fuse_core::TimeStamp(12345678, 910111213), fuse_core::uuid::generate("hal9000"));
  expected.yaw() = 1.5;

  // Serialize the variable into an archive
  std::stringstream stream;
  {
    fuse_core::TextOutputArchive archive(stream);
    expected.serialize(archive);
  }

  // Deserialize a new variable from that same stream
  Orientation2DStamped actual;
  {
    fuse_core::TextInputArchive archive(stream);
    actual.deserialize(archive);
  }

  // Compare
  EXPECT_EQ(expected.deviceId(), actual.deviceId());
  EXPECT_EQ(expected.stamp(), actual.stamp());
  EXPECT_EQ(expected.yaw(), actual.yaw());
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
