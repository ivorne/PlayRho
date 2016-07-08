//
//  Distance.cpp
//  Box2D
//
//  Created by Louis D. Langholtz on 7/8/16.
//
//

#include "gtest/gtest.h"
#include <Box2D/Collision/Distance.h>

using namespace box2d;

TEST(Distance, MatchingCircles)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{2, 2};
	const auto pos2 = Vec2{2, 2};
	DistanceProxy dp1{1, pos1};
	DistanceProxy dp2{1, pos2};

	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a, pos1);
	EXPECT_EQ(output.witnessPoints.b, pos1);
	EXPECT_EQ(decltype(output.iterations){0}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){1});
	
	const auto ip = cache.GetIndexPair(0);
	EXPECT_EQ(ip.a, IndexPair::size_type{0});
	EXPECT_EQ(ip.b, IndexPair::size_type{0});

	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{0});
}

TEST(Distance, OpposingCircles)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{2, 2};
	const auto pos2 = Vec2{-2, -2};
	DistanceProxy dp1{2, pos1};
	DistanceProxy dp2{2, pos2};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos1.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos1.y);

	EXPECT_EQ(output.witnessPoints.b.x, pos2.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos2.y);

	EXPECT_EQ(decltype(output.iterations){1}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){1});
	
	const auto ip = cache.GetIndexPair(0);
	EXPECT_EQ(ip.a, IndexPair::size_type{0});
	EXPECT_EQ(ip.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{0});
}

TEST(Distance, OverlappingCirclesPN)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{1, 1};
	const auto pos2 = Vec2{-1, -1};
	DistanceProxy dp1{2, pos1};
	DistanceProxy dp2{2, pos2};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos1.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos1.y);
	
	EXPECT_EQ(output.witnessPoints.b.x, pos2.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos2.y);
	
	EXPECT_EQ(decltype(output.iterations){1}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){1});
	
	const auto ip = cache.GetIndexPair(0);
	EXPECT_EQ(ip.a, IndexPair::size_type{0});
	EXPECT_EQ(ip.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{0});
}

TEST(Distance, OverlappingCirclesNP)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{-1, -1};
	const auto pos2 = Vec2{1, 1};
	DistanceProxy dp1{2, pos1};
	DistanceProxy dp2{2, pos2};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos1.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos1.y);
	
	EXPECT_EQ(output.witnessPoints.b.x, pos2.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos2.y);
	
	EXPECT_EQ(decltype(output.iterations){1}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){1});
	
	const auto ip = cache.GetIndexPair(0);
	EXPECT_EQ(ip.a, IndexPair::size_type{0});
	EXPECT_EQ(ip.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{0});
}


TEST(Distance, SeparatedCircles)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{2, 2};
	const auto pos2 = Vec2{-2, -2};
	DistanceProxy dp1{1, pos1};
	DistanceProxy dp2{1, pos2};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos1.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos1.y);
	
	EXPECT_EQ(output.witnessPoints.b.x, pos2.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos2.y);
	
	EXPECT_EQ(decltype(output.iterations){1}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){1});
	
	const auto ip = cache.GetIndexPair(0);
	EXPECT_EQ(ip.a, IndexPair::size_type{0});
	EXPECT_EQ(ip.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{0});
}

TEST(Distance, EdgeCircleOverlapping)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{0, 2};
	const auto pos2 = Vec2{4, 2};
	const auto pos3 = Vec2{2, 2};
	DistanceProxy dp1{float_t(0.1), pos1, pos2};
	DistanceProxy dp2{1, pos3};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos3.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos3.y);

	EXPECT_EQ(output.witnessPoints.b.x, pos3.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos3.y);
	
	EXPECT_EQ(decltype(output.iterations){2}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){2});
	
	const auto ip0 = cache.GetIndexPair(0);
	EXPECT_EQ(ip0.a, IndexPair::size_type{0});
	EXPECT_EQ(ip0.b, IndexPair::size_type{0});

	const auto ip1 = cache.GetIndexPair(1);
	EXPECT_EQ(ip1.a, IndexPair::size_type{1});
	EXPECT_EQ(ip1.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{4});
}

TEST(Distance, EdgeCircleOverlapping2)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{-3, 2};
	const auto pos2 = Vec2{7, 2};
	const auto pos3 = Vec2{2, 2};
	DistanceProxy dp1{float_t(0.1), pos1, pos2};
	DistanceProxy dp2{1, pos3};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, pos3.x);
	EXPECT_EQ(output.witnessPoints.a.y, pos3.y);
	
	EXPECT_EQ(output.witnessPoints.b.x, pos3.x);
	EXPECT_EQ(output.witnessPoints.b.y, pos3.y);
	
	EXPECT_EQ(decltype(output.iterations){2}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){2});
	
	const auto ip0 = cache.GetIndexPair(0);
	EXPECT_EQ(ip0.a, IndexPair::size_type{0});
	EXPECT_EQ(ip0.b, IndexPair::size_type{0});
	
	const auto ip1 = cache.GetIndexPair(1);
	EXPECT_EQ(ip1.a, IndexPair::size_type{1});
	EXPECT_EQ(ip1.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{10});
}

TEST(Distance, EdgeCircleTouching)
{
	SimplexCache cache;
	Transformation xf1 = Transform_identity;
	Transformation xf2 = Transform_identity;
	const auto pos1 = Vec2{0, 3};
	const auto pos2 = Vec2{4, 3};
	const auto pos3 = Vec2{2, 1};
	DistanceProxy dp1{float_t(1), pos1, pos2};
	DistanceProxy dp2{1, pos3};
	
	const auto output = Distance(cache, dp1, xf1, dp2, xf2);
	
	EXPECT_EQ(output.witnessPoints.a.x, float_t{2});
	EXPECT_EQ(output.witnessPoints.a.y, float_t{3});
	
	EXPECT_EQ(output.witnessPoints.b.x, float_t{2});
	EXPECT_EQ(output.witnessPoints.b.y, float_t{1});
	
	EXPECT_EQ(decltype(output.iterations){2}, output.iterations);
	
	EXPECT_EQ(cache.GetCount(), decltype(cache.GetCount()){2});
	
	const auto ip0 = cache.GetIndexPair(0);
	EXPECT_EQ(ip0.a, IndexPair::size_type{0});
	EXPECT_EQ(ip0.b, IndexPair::size_type{0});
	
	const auto ip1 = cache.GetIndexPair(1);
	EXPECT_EQ(ip1.a, IndexPair::size_type{1});
	EXPECT_EQ(ip1.b, IndexPair::size_type{0});
	
	EXPECT_EQ(true, cache.IsMetricSet());
	EXPECT_EQ(cache.GetMetric(), float_t{4});
}