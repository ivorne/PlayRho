/*
 * Original work Copyright (c) 2009 Erin Catto http://www.box2d.org
 * Modified work Copyright (c) 2017 Louis Langholtz https://github.com/louis-langholtz/Box2D
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

#include <Box2D/Collision/AABB.hpp>
#include <Box2D/Collision/RayCastInput.hpp>

#include <functional>

namespace box2d {

/// A dynamic AABB tree broad-phase, inspired by Nathanael Presson's btDbvt.
///
/// @details A dynamic tree arranges data in a binary tree to accelerate
/// queries such as volume queries and ray casts. Leafs are proxies
/// with an AABB. In the tree we expand the proxy AABB by AabbMultiplier
/// so that the proxy AABB is bigger than the client object. This allows the client
/// object to move by small amounts without triggering a tree update.
///
/// Nodes are pooled and relocatable, so we use node indices rather than pointers.
///
/// @note This data structure is 24-bytes large (on at least one 64-bit platform).
///
class DynamicTree
{
public:

    using size_type = std::remove_const<decltype(MaxContacts)>::type;
    using QueryCallback = std::function<bool(size_type)>;
    using RayCastCallback = std::function<RealNum(const RayCastInput&, size_type)>;

    static constexpr auto AabbMultiplier = 2;

    /// Invalid index value.
    static constexpr size_type InvalidIndex = static_cast<size_type>(-1);

    static constexpr size_type GetDefaultInitialNodeCapacity() noexcept;

    /// Constructing the tree initializes the node pool.
    DynamicTree(const size_type nodeCapacity = GetDefaultInitialNodeCapacity());

    /// Destroys the tree, freeing the node pool.
    ~DynamicTree() noexcept;

    DynamicTree(const DynamicTree& copy) = delete;
    DynamicTree& operator=(const DynamicTree&) = delete;

    /// Creates a new proxy.
    /// @details Creates a proxy for a tight fitting AABB and a userData pointer.
    /// @note The indices of proxies that have been destroyed get reused for new proxies.
    /// @return Index of the created proxy.
    size_type CreateProxy(const AABB aabb, void* userData);

    /// Destroys a proxy.
    /// @warning Behavior is undefined if the given index is not valid.
    void DestroyProxy(const size_type index);

    /// Updates a proxy.
    /// @note If the stored AABB of the identified proxy doesn't contain the given new
    ///   AABB of the proxy, then the proxy is removed from the tree and re-inserted with
    ///   a fattened and displaced version of the given new AABB.
    /// @warning Behavior is undefined if the given index is not valid.
    /// @param index Proxy ID. Behavior is undefined if this is not a valid ID.
    /// @param aabb New axis aligned bounding box of the proxy.
    /// @param displacement Displacement of the proxy. Behavior undefined if not a valid value.
    /// @param multiplier Multiplier to displacement amount for new AABB.
    ///   This is used to predict the future position based on the current displacement.
    ///   This is a dimensionless multiplier.
    /// @param extension Extension. Amount to fatten the given AABB by if the proxy does
    ///   not contain it.
    /// @return true if the proxy was re-inserted, false otherwise.
    bool UpdateProxy(const size_type index, const AABB aabb, const Length2D displacement,
                     const RealNum multiplier = 1, const Length extension = Length{0});

    /// Gets the user data for the node identified by the given identifier.
    /// @warning Behavior is undefined if the given index is not valid.
    /// @param index Identifier of node to get the user data for.
    /// @return User data for the specified node.
    void* GetUserData(const size_type index) const noexcept;

    /// Gets the fat AABB for a proxy.
    /// @warning Behavior is undefined if the given index is not valid.
    /// @param index Proxy ID. Must be a valid ID.
    AABB GetFatAABB(const size_type index) const noexcept;

    /// Query an AABB for overlapping proxies. The callback class
    /// is called for each proxy that overlaps the supplied AABB.
    void Query(const AABB aabb, QueryCallback callback) const;

    /// Ray-cast against the proxies in the tree. This relies on the callback
    /// to perform a exact ray-cast in the case were the proxy contains a shape.
    /// The callback also performs the any collision filtering.
    /// @note Performance is roughly k * log(n), where k is the number of collisions and n is the
    /// number of proxies in the tree.
    /// @param input the ray-cast input data. The ray extends from p1 to p1 + maxFraction * (p2 - p1).
    /// @param callback a callback class that is called for each proxy that is hit by the ray.
    void RayCast(const RayCastInput& input, RayCastCallback callback) const;

    /// Validates this tree.
    /// @note Meant for testing.
    /// @return <code>true</code> if valid, <code>false</code> otherwise.
    bool Validate() const;

    /// Validates the structure of this tree from the given index.
    /// @note Meant for testing.
    /// @return <code>true</code> if valid, <code>false</code> otherwise.
    bool ValidateStructure(const size_type index) const noexcept;

    /// Validates the metrics of this tree from the given index.
    /// @note Meant for testing.
    /// @return <code>true</code> if valid, <code>false</code> otherwise.
    bool ValidateMetrics(size_type index) const noexcept;

    /// Gets the height of the binary tree.
    /// @return Height of the tree (as stored in the root node) or 0 if the root node is not valid.
    size_type GetHeight() const noexcept;

    /// Gets the maximum balance.
    /// @details This gets the maximum balance of nodes in the tree.
    /// @note The balance is the difference in height of the two children of a node.
    size_type GetMaxBalance() const;

    /// Gets the ratio of the sum of the perimeters of nodes to the root perimeter.
    /// @note Zero is returned if no proxies exist at the time of the call.
    /// @return Value of zero or more.
    RealNum GetAreaRatio() const noexcept;

    /// Builds an optimal tree.
    /// @note This operation is very expensive.
    /// @note Meant for testing.
    void RebuildBottomUp();

    /// Shifts the world origin.
    /// @note Useful for large worlds.
    /// @note The shift formula is: position -= newOrigin
    /// @param newOrigin the new origin with respect to the old origin
    void ShiftOrigin(const Length2D newOrigin);

    /// Computes the height of the tree from a given node.
    /// @warning Behavior is undefined if the given index is not valid.
    /// @param nodeId ID of node to compute height from.
    size_type ComputeHeight(const size_type nodeId) const noexcept;

    /// Computes the height of the tree from its root.
    /// @warning Behavior is undefined if the tree doesn't have a valid root.
    size_type ComputeHeight() const noexcept;

    /// Gets the current node capacity of this tree.
    size_type GetNodeCapacity() const noexcept;
    
    /// Gets the current node count.
    /// @return Count of existing proxies (count of nodes currently allocated).
    size_type GetNodeCount() const noexcept;

    /// Finds the lowest cost node.
    /// @warning Behavior is undefined if the tree doesn't have a valid root.
    size_type FindLowestCostNode(const AABB leafAABB) const noexcept;

private:

    /// A node in the dynamic tree. The client does not interact with this directly.
    struct TreeNode
    {
        /// Whether or not this node is a leaf node.
        /// @note This has constant complexity.
        /// @return <code>true</code> if this is a leaf node, <code>false</code> otherwise.
        bool IsLeaf() const noexcept
        {
            return child1 == InvalidIndex;
        }
        
        /// Enlarged AABB
        AABB aabb;
        
        void* userData;
        
        union
        {
            size_type parent;
            size_type next;
        };
        
        size_type child1; ///< Index of child 1 in DynamicTree::m_nodes or InvalidIndex.
        size_type child2; ///< Index of child 2 in DynamicTree::m_nodes or InvalidIndex.
        
        size_type height; ///< Height - for tree balancing. 0 if leaf node. InvalidIndex if free node.
    };

    /// Allocates a new node.
    size_type AllocateNode();
    
    /// Frees the specified node.
    /// @warning Behavior is undefined if the given index is not valid.
    void FreeNode(const size_type node) noexcept;

    /// Inserts the specified node.
    /// Does a leaf insertion of the node with the given index.
    /// @warning Behavior is undefined if the given index is not valid.
    void InsertLeaf(const size_type index);

    /// Removes the specified node.
    /// Does a leaf removal of the node with the given index.
    /// @warning Behavior is undefined if the given index is not valid.
    void RemoveLeaf(const size_type index);

    /// Balances the tree from the given index.
    /// @warning Behavior is undefined if the given index is not valid.
    size_type Balance(const size_type index);

    TreeNode* m_nodes; ///< Nodes. @details Initialized on construction.

    size_type m_root = InvalidIndex; ///< Index of root element in m_nodes or InvalidIndex.

    size_type m_nodeCount = 0; ///< Node count. @details Count of currently allocated nodes.
    size_type m_nodeCapacity; ///< Node capacity. @details Size of buffer allocated for nodes.

    size_type m_freeListIndex = 0; ///< Free list. @details Index to free nodes.
};

constexpr DynamicTree::size_type DynamicTree::GetDefaultInitialNodeCapacity() noexcept
{
    return size_type{16};
}

inline DynamicTree::size_type DynamicTree::GetNodeCapacity() const noexcept
{
    return m_nodeCapacity;
}

inline DynamicTree::size_type DynamicTree::GetNodeCount() const noexcept
{
    return m_nodeCount;
}

inline void* DynamicTree::GetUserData(const size_type index) const noexcept
{
    assert(index != InvalidIndex);
    assert(index < m_nodeCapacity);
    return m_nodes[index].userData;
}

inline AABB DynamicTree::GetFatAABB(const size_type index) const noexcept
{
    assert(index != InvalidIndex);
    assert(index < m_nodeCapacity);
    return m_nodes[index].aabb;
}

inline DynamicTree::size_type DynamicTree::GetHeight() const noexcept
{
    return (m_root != InvalidIndex)? m_nodes[m_root].height: 0;
}

inline DynamicTree::size_type DynamicTree::ComputeHeight() const noexcept
{
    return ComputeHeight(m_root);
}

} /* namespace box2d */

#endif