/*
* Copyright (c) 2009 Erin Catto http://www.box2d.org
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#ifndef B2_DYNAMIC_TREE_H
#define B2_DYNAMIC_TREE_H

#include <Box2D/Collision/b2Collision.h>
#include <Box2D/Common/b2GrowableStack.h>

static constexpr auto b2_nullNode = static_cast<b2_size_t>(-1);

/// A node in the dynamic tree. The client does not interact with this directly.
struct b2TreeNode
{
	using size_type = b2_size_t;

	bool IsLeaf() const noexcept
	{
		return child1 == b2_nullNode;
	}

	/// Enlarged AABB
	b2AABB aabb;

	void* userData;

	union
	{
		size_type parent;
		size_type next;
	};

	size_type child1;
	size_type child2;

	// leaf = 0, free node = b2_nullNode
	size_type height;
};

/// A dynamic AABB tree broad-phase, inspired by Nathanael Presson's btDbvt.
/// A dynamic tree arranges data in a binary tree to accelerate
/// queries such as volume queries and ray casts. Leafs are proxies
/// with an AABB. In the tree we expand the proxy AABB by b2_fatAABBFactor
/// so that the proxy AABB is bigger than the client object. This allows the client
/// object to move by small amounts without triggering a tree update.
///
/// Nodes are pooled and relocatable, so we use node indices rather than pointers.
class b2DynamicTree
{
public:
	using size_type = b2_size_t;

	/// Constructing the tree initializes the node pool.
	b2DynamicTree();

	/// Destroy the tree, freeing the node pool.
	~b2DynamicTree();

	b2DynamicTree(const b2DynamicTree& copy) = delete;
	b2DynamicTree& operator=(const b2DynamicTree&) = delete;

	/// Create a proxy. Provide a tight fitting AABB and a userData pointer.
	size_type CreateProxy(const b2AABB& aabb, void* userData);

	/// Destroy a proxy. This asserts if the id is invalid.
	void DestroyProxy(size_type proxyId);

	/// Move a proxy with a swepted AABB. If the proxy has moved outside of its fattened AABB,
	/// then the proxy is removed from the tree and re-inserted. Otherwise
	/// the function returns immediately.
	/// @return true if the proxy was re-inserted.
	bool MoveProxy(size_type proxyId, const b2AABB& aabb1, const b2Vec2& displacement);

	/// Get proxy user data.
	/// @return the proxy user data or 0 if the id is invalid.
	void* GetUserData(size_type proxyId) const;

	/// Get the fat AABB for a proxy.
	const b2AABB& GetFatAABB(size_type proxyId) const;

	/// Query an AABB for overlapping proxies. The callback class
	/// is called for each proxy that overlaps the supplied AABB.
	template <typename T>
	void Query(T* callback, const b2AABB& aabb) const;

	/// Ray-cast against the proxies in the tree. This relies on the callback
	/// to perform a exact ray-cast in the case were the proxy contains a shape.
	/// The callback also performs the any collision filtering. This has performance
	/// roughly equal to k * log(n), where k is the number of collisions and n is the
	/// number of proxies in the tree.
	/// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
	/// @param callback a callback class that is called for each proxy that is hit by the ray.
	template <typename T>
	void RayCast(T* callback, const b2RayCastInput& input) const;

	/// Validate this tree. For testing.
	void Validate() const;

	/// Compute the height of the binary tree in O(N) time. Should not be
	/// called often.
	size_type GetHeight() const noexcept;

	/// Get the maximum balance of an node in the tree. The balance is the difference
	/// in height of the two children of a node.
	size_type GetMaxBalance() const;

	/// Get the ratio of the sum of the node areas to the root area.
	float32 GetAreaRatio() const;

	/// Build an optimal tree. Very expensive. For testing.
	void RebuildBottomUp();

	/// Shift the world origin. Useful for large worlds.
	/// The shift formula is: position -= newOrigin
	/// @param newOrigin the new origin with respect to the old origin
	void ShiftOrigin(const b2Vec2& newOrigin);

private:

	size_type AllocateNode();
	void FreeNode(size_type node);

	void InsertLeaf(size_type node);
	void RemoveLeaf(size_type node);

	size_type Balance(size_type index);

	size_type ComputeHeight() const;
	size_type ComputeHeight(size_type nodeId) const;

	void ValidateStructure(size_type index) const;
	void ValidateMetrics(size_type index) const;

	size_type m_root = b2_nullNode;

	size_type m_nodeCount = 0;
	size_type m_nodeCapacity = 16;

	size_type m_freeList = 0;

	/// This is used to incrementally traverse the tree for re-balancing.
	uint32 m_path = 0;

	int32 m_insertionCount = 0;

	/// Initialized on construction.
	b2TreeNode* m_nodes;
};

inline void* b2DynamicTree::GetUserData(size_type proxyId) const
{
	b2Assert(proxyId < m_nodeCapacity);
	return m_nodes[proxyId].userData;
}

inline const b2AABB& b2DynamicTree::GetFatAABB(size_type proxyId) const
{
	b2Assert(proxyId < m_nodeCapacity);
	return m_nodes[proxyId].aabb;
}

template <typename T>
inline void b2DynamicTree::Query(T* callback, const b2AABB& aabb) const
{
	b2GrowableStack<size_type, 256> stack;
	stack.Push(m_root);

	while (stack.GetCount() > 0)
	{
		const auto nodeId = stack.Pop();
		if (nodeId == b2_nullNode)
		{
			continue;
		}

		const b2TreeNode* node = m_nodes + nodeId;

		if (b2TestOverlap(node->aabb, aabb))
		{
			if (node->IsLeaf())
			{
				const auto proceed = callback->QueryCallback(nodeId);
				if (!proceed)
				{
					return;
				}
			}
			else
			{
				stack.Push(node->child1);
				stack.Push(node->child2);
			}
		}
	}
}

template <typename T>
inline void b2DynamicTree::RayCast(T* callback, const b2RayCastInput& input) const
{
	const auto p1 = input.p1;
	const auto p2 = input.p2;
	auto r = p2 - p1;
	b2Assert(r.LengthSquared() > 0.0f);
	r.Normalize();

	// v is perpendicular to the segment.
	const auto v = b2Cross(1.0f, r);
	const auto abs_v = b2Abs(v);

	// Separating axis for segment (Gino, p80).
	// |dot(v, p1 - c)| > dot(|v|, h)

	auto maxFraction = input.maxFraction;

	// Build a bounding box for the segment.
	b2AABB segmentAABB;
	{
		const auto t = p1 + maxFraction * (p2 - p1);
		segmentAABB.lowerBound = b2Min(p1, t);
		segmentAABB.upperBound = b2Max(p1, t);
	}

	b2GrowableStack<size_type, 256> stack;
	stack.Push(m_root);

	while (stack.GetCount() > 0)
	{
		const auto nodeId = stack.Pop();
		if (nodeId == b2_nullNode)
		{
			continue;
		}

		const auto node = m_nodes + nodeId;

		if (!b2TestOverlap(node->aabb, segmentAABB))
		{
			continue;
		}

		// Separating axis for segment (Gino, p80).
		// |dot(v, p1 - c)| > dot(|v|, h)
		const auto c = node->aabb.GetCenter();
		const auto h = node->aabb.GetExtents();
		const auto separation = b2Abs(b2Dot(v, p1 - c)) - b2Dot(abs_v, h);
		if (separation > 0.0f)
		{
			continue;
		}

		if (node->IsLeaf())
		{
			const auto subInput = b2RayCastInput{input.p1, input.p2, maxFraction};

			const auto value = callback->RayCastCallback(subInput, nodeId);

			if (value == 0.0f)
			{
				// The client has terminated the ray cast.
				return;
			}

			if (value > 0.0f)
			{
				// Update segment bounding box.
				maxFraction = value;
				const auto t = p1 + maxFraction * (p2 - p1);
				segmentAABB.lowerBound = b2Min(p1, t);
				segmentAABB.upperBound = b2Max(p1, t);
			}
		}
		else
		{
			stack.Push(node->child1);
			stack.Push(node->child2);
		}
	}
}

#endif
