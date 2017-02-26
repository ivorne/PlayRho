/*
 * Original work Copyright (c) 2006-2011 Erin Catto http://www.box2d.org
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

#include <Box2D/Dynamics/World.hpp>
#include <Box2D/Dynamics/Body.hpp>
#include <Box2D/Dynamics/StepConf.hpp>
#include <Box2D/Dynamics/Fixture.hpp>
#include <Box2D/Dynamics/FixtureProxy.hpp>
#include <Box2D/Dynamics/Island.hpp>
#include <Box2D/Dynamics/Joints/PulleyJoint.hpp>
#include <Box2D/Dynamics/Contacts/Contact.hpp>
#include <Box2D/Collision/BroadPhase.hpp>
#include <Box2D/Collision/WorldManifold.hpp>
#include <Box2D/Collision/TimeOfImpact.hpp>
#include <Box2D/Collision/RayCastOutput.hpp>
#include <Box2D/Common/Timer.hpp>
#include <Box2D/Common/AllocatedArray.hpp>

#include <Box2D/Dynamics/Contacts/ContactSolver.hpp>
#include <Box2D/Dynamics/Contacts/VelocityConstraint.hpp>
#include <Box2D/Dynamics/Contacts/PositionConstraint.hpp>

#include <new>
#include <functional>
#include <type_traits>
#include <memory>
#include <set>
#include <vector>

#define BOX2D_MAGIC(x) (x)

namespace box2d
{

using BodyConstraints = std::vector<BodyConstraint>;
using PositionConstraintsContainer = std::vector<PositionConstraint>;
using VelocityConstraintsContainer = std::vector<VelocityConstraint>;
	
struct MovementConf
{
	RealNum maxTranslation;
	Angle maxRotation;
};

template <typename T>
class FlagGuard
{
public:
	FlagGuard(T& flag, T value) : m_flag(flag), m_value(value)
	{
		static_assert(std::is_unsigned<T>::value, "Unsigned integer required");
		m_flag |= m_value;
	}

	~FlagGuard() noexcept
	{
		m_flag &= ~m_value;
	}

	FlagGuard() = delete;

private:
	T& m_flag;
	T m_value;
};

template <class T>
class RaiiWrapper
{
public:
	RaiiWrapper() = delete;
	RaiiWrapper(std::function<void(T&)> on_destruction): m_on_destruction(on_destruction) {}
	~RaiiWrapper() { m_on_destruction(m_wrapped); }
	T m_wrapped;

private:
	std::function<void(T&)> m_on_destruction;
};

namespace {
	
	struct PositionAndVelocity
	{
		Position position;
		Velocity velocity;
	};

	/// Calculates movement.
	/// @detail Calculate the positional displacement based on the given velocity
	///    that's possibly clamped to the maximum translation and rotation.
	inline PositionAndVelocity CalculateMovement(const BodyConstraint& body, RealNum h, MovementConf conf)
	{
		assert(IsValid(h));
		
		auto velocity = body.GetVelocity();
		auto translation = h * velocity.linear;
		if (GetLengthSquared(translation) > Square(conf.maxTranslation))
		{
			const auto ratio = conf.maxTranslation / Sqrt(GetLengthSquared(translation));
			velocity.linear *= ratio;
			translation = h * velocity.linear;
		}
		
		auto rotation = h * velocity.angular;
		if (Abs(rotation) > conf.maxRotation)
		{
			const auto ratio = conf.maxRotation / Abs(rotation);
			velocity.angular *= ratio;
			rotation = h * velocity.angular;
		}
		
		return PositionAndVelocity{body.GetPosition() + Position{translation, rotation}, velocity};
	}
	
	inline void IntegratePositions(BodyConstraints& bodies,
								   RealNum h, MovementConf conf)
	{
		for (auto&& body: bodies)
		{
			const auto newPosAndVel = CalculateMovement(body, h, conf);
			body.SetPosition(newPosAndVel.position);
			body.SetVelocity(newPosAndVel.velocity);
		}
	}
	
	inline ContactImpulsesList GetContactImpulses(const VelocityConstraint& vc)
	{
		ContactImpulsesList impulse;
		const auto count = vc.GetPointCount();
		for (auto j = decltype(count){0}; j < count; ++j)
		{
			impulse.AddEntry(GetNormalImpulseAtPoint(vc, j), GetTangentImpulseAtPoint(vc, j));
		}
		return impulse;
	}
	
	/// Reports the given constraints to the listener.
	/// @detail
	/// This calls the listener's PostSolve method for all contacts.size() elements of
	/// the given array of constraints.
	/// @param listener Listener to call.
	/// @param constraints Array of m_contactCount contact velocity constraint elements.
	inline void Report(ContactListener& listener,
					   Span<Contact*> contacts,
					   const VelocityConstraintsContainer& constraints,
					   StepConf::iteration_type solved)
	{
		const auto size = contacts.size();
		for (auto i = decltype(size){0}; i < size; ++i)
		{
			listener.PostSolve(*contacts[i], GetContactImpulses(constraints[i]), solved);
		}
	}
	
	PositionConstraintsContainer GetPositionConstraints(const Island::ContactContainer& contacts, BodyConstraints& bodies)
	{
		auto constraints = PositionConstraintsContainer{};
		constraints.reserve(contacts.size());
		for (auto&& contact: contacts)
		{
			const auto& manifold = contact->GetManifold();
			const auto& fixtureA = *(contact->GetFixtureA());
			const auto& fixtureB = *(contact->GetFixtureB());
			
			auto& bodyA = bodies[fixtureA.GetBody()->GetIslandIndex()];
			const auto radiusA = GetVertexRadius(*fixtureA.GetShape());
			
			auto& bodyB = bodies[fixtureB.GetBody()->GetIslandIndex()];
			const auto radiusB = GetVertexRadius(*fixtureB.GetShape());
			
			constraints.emplace_back(manifold, bodyA, radiusA, bodyB, radiusB);
		}
		return constraints;
	}
	
	inline void AssignImpulses(Manifold& var, const VelocityConstraint& vc)
	{
		assert(var.GetPointCount() >= vc.GetPointCount());
		
		const auto count = vc.GetPointCount();
		for (auto i = decltype(count){0}; i < count; ++i)
		{
			var.SetPointImpulses(i, GetNormalImpulseAtPoint(vc, i), GetTangentImpulseAtPoint(vc, i));
		}
	}
	
	/// Stores impulses.
	/// @detail Saves the normal and tangent impulses of all the velocity constraint points back to their
	///   associated contacts' manifold points.
	inline void StoreImpulses(const VelocityConstraintsContainer& velocityConstraints, Span<Contact*> contacts)
	{
		for (auto&& vc: velocityConstraints)
		{
			auto& manifold = contacts[vc.GetContactIndex()]->GetManifold();
			AssignImpulses(manifold, vc);
		}
	}
	
	struct VelocityPair
	{
		Velocity a;
		Velocity b;
	};
	
	inline VelocityPair CalcWarmStartVelocityDeltas(const VelocityConstraint& vc)
	{
		VelocityPair vp{Velocity{Vec2_zero, 0_rad}, Velocity{Vec2_zero, 0_rad}};
		
		const auto normal = GetNormal(vc);
		const auto tangent = GetTangent(vc);
		if (IsValid(normal) && IsValid(tangent))
		{
			const auto pointCount = vc.GetPointCount();
			for (auto j = decltype(pointCount){0}; j < pointCount; ++j)
			{
				const auto P = GetNormalImpulseAtPoint(vc, j) * normal + GetTangentImpulseAtPoint(vc, j) * tangent;
				vp.a -= Velocity{
					vc.bodyA.GetInvMass() * P,
					1_rad * vc.bodyA.GetInvRotI() * Cross(GetPointRelPosA(vc, j), P)
				};
				vp.b += Velocity{
					vc.bodyB.GetInvMass() * P,
					1_rad * vc.bodyB.GetInvRotI() * Cross(GetPointRelPosB(vc, j), P)
				};
			}
		}
		return vp;
	}
	
	inline void WarmStartVelocities(const VelocityConstraintsContainer& velocityConstraints)
	{
		for (auto&& vc: velocityConstraints)
		{
			const auto vp = CalcWarmStartVelocityDeltas(vc);
			vc.bodyA.SetVelocity(vc.bodyA.GetVelocity() + vp.a);
			vc.bodyB.SetVelocity(vc.bodyB.GetVelocity() + vp.b);
		}
	}

	/// Gets the velocity constraints for the given inputs.
	/// @detail
	/// Inializes the velocity constraints with the position dependent portions of the current position constraints.
	/// @post Velocity constraints will have their "normal" field set to the world manifold normal for them.
	/// @post Velocity constraints will have their constraint points set.
	/// @sa SolveVelocityConstraints.
	VelocityConstraintsContainer GetVelocityConstraints(const Island::ContactContainer& contacts,
														BodyConstraints& bodies,
														const VelocityConstraint::Conf conf)
	{
		auto velocityConstraints = VelocityConstraintsContainer{};
		const auto numContacts = contacts.size();
		velocityConstraints.reserve(numContacts);

		//auto i = VelocityConstraint::index_type{0};
		for (auto i = decltype(numContacts){0}; i < numContacts; ++i)
		{
			const auto& contact = *contacts[i];

			const auto& manifold = contact.GetManifold();
			const auto fixtureA = contact.GetFixtureA();
			const auto fixtureB = contact.GetFixtureB();
			const auto friction = contact.GetFriction();
			const auto restitution = contact.GetRestitution();
			const auto tangentSpeed = contact.GetTangentSpeed();
			
			const auto radiusA = fixtureA->GetShape()->GetVertexRadius();
			auto& bodyA = bodies[fixtureA->GetBody()->GetIslandIndex()];
			
			const auto radiusB = fixtureB->GetShape()->GetVertexRadius();
			auto& bodyB = bodies[fixtureB->GetBody()->GetIslandIndex()];
			
			velocityConstraints.emplace_back(i, friction, restitution, tangentSpeed,
											 manifold, bodyA, radiusA, bodyB, radiusB,
											 conf);

		}
		return velocityConstraints;
	}

	/// "Solves" the velocity constraints.
	/// @detail Updates the velocities and velocity constraint points' normal and tangent impulses.
	/// @pre <code>UpdateVelocityConstraints</code> has been called on the velocity constraints.
	inline RealNum SolveVelocityConstraints(VelocityConstraintsContainer& velocityConstraints)
	{
		auto maxIncImpulse = RealNum{0};
		for (auto&& vc: velocityConstraints)
		{
			maxIncImpulse = std::max(maxIncImpulse, SolveVelocityConstraint(vc));
		}
		return maxIncImpulse;
	}

	inline RealNum UpdateSleepTimes(Island::BodyContainer& bodies, const StepConf& step)
	{
		auto minSleepTime = std::numeric_limits<RealNum>::infinity();
		for (auto&& b: bodies)
		{
			if (b->IsSpeedable())
			{
				const auto sleepTime = b->UpdateSleepTime(step.get_dt(),
														  step.linearSleepTolerance,
														  step.angularSleepTolerance);
				minSleepTime = Min(minSleepTime, sleepTime);
			}
		}
		return minSleepTime;
	}
	
	inline size_t Sleepem(Island::BodyContainer& bodies)
	{
		auto unawoken = size_t{0};
		for (auto&& b: bodies)
		{
			if (b->UnsetAwake())
			{
				++unawoken;
			}
		}
		return unawoken;
	}
	
	inline bool IsAllFlagsSet(uint16 value, uint16 flags)
	{
		return (value & flags) == flags;
	}

} // anonymous namespace

const BodyDef& World::GetDefaultBodyDef()
{
	static const BodyDef def = BodyDef{};
	return def;
}

World::World(const Def& def):
	m_gravity(def.gravity),
	m_aabbExtension(def.aabbExtension),
	m_minVertexRadius(def.minVertexRadius),
	m_maxVertexRadius(def.maxVertexRadius)
{
	assert(IsValid(def.gravity));
	assert(def.aabbExtension > 0);
	assert(def.minVertexRadius > 0);
	assert(def.minVertexRadius < def.maxVertexRadius);
}

World::~World()
{
	while (!m_contactMgr.GetContacts().empty())
	{
		const auto c = &m_contactMgr.GetContacts().front();
		m_contactMgr.Remove(c);
		Contact::Destroy(c, m_contactMgr.m_allocator);
	}

	// Delete the created joints.
	while (!m_joints.empty())
	{
		InternalDestroy(&m_joints.front());
	}

	// Some shapes allocate using alloc.
	while (!m_bodies.empty())
	{
		auto&& b = m_bodies.front();
		m_bodies.erase(BodyIterator{&b});
		b.~Body();
		m_blockAllocator.Free(&b, sizeof(b));
	}
}

void World::SetDestructionListener(DestructionListener* listener) noexcept
{
	m_destructionListener = listener;
}

void World::SetContactFilter(ContactFilter* filter) noexcept
{
	m_contactMgr.m_contactFilter = filter;
}

void World::SetContactListener(ContactListener* listener) noexcept
{
	m_contactMgr.m_contactListener = listener;
}

void World::SetGravity(const Vec2 gravity) noexcept
{
	if (m_gravity != gravity)
	{
		const auto diff = gravity - m_gravity;
		for (auto&& body: m_bodies)
		{
			ApplyLinearAcceleration(body, diff);
		}
		m_gravity = gravity;
	}
}

Body* World::CreateBody(const BodyDef& def)
{
	assert(!IsLocked());
	if (IsLocked())
	{
		return nullptr;
	}

	void* mem = m_blockAllocator.Allocate(sizeof(Body));
	auto b = new (mem) Body(def, this);
	if (b)
	{
		if (!Add(*b))
		{
			b->~Body();
			m_blockAllocator.Free(b, sizeof(Body));
			return nullptr;
		}		
	}

	b->SetAcceleration(m_gravity, 0_rad);
	return b;
}

bool World::Add(Body& b)
{
	assert(!b.m_prev);
	assert(!b.m_next);

	if (m_bodies.size() >= MaxBodies)
	{
		return false;
	}
	
	// Add to world doubly linked list.
	m_bodies.push_front(&b);
	return true;
}

bool World::Remove(Body& b)
{
	assert(!m_bodies.empty());
	if (m_bodies.empty())
	{
		return false;
	}

	m_bodies.erase(BodyIterator{&b});
	return true;
}

void World::Destroy(Body* b)
{
	assert(b->m_world == this);
	
	assert(!IsLocked());
	if (IsLocked())
	{
		return;
	}
	
	Remove(*b);
	
	b->~Body();
	m_blockAllocator.Free(b, sizeof(*b));
}

Joint* World::CreateJoint(const JointDef& def)
{
	if (m_joints.size() >= m_joints.max_size())
	{
		return nullptr;
	}

	assert(!IsLocked());
	if (IsLocked())
	{
		return nullptr;
	}

	// Note: creating a joint doesn't wake the bodies.
	auto j = Joint::Create(def, m_blockAllocator);

	// Connect to the bodies' doubly linked lists.
	j->m_edgeA.joint = j;
	j->m_edgeA.other = j->m_bodyB;
	j->m_edgeA.prev = nullptr;
	j->m_edgeA.next = j->m_bodyA->m_joints.p;
	if (j->m_bodyA->m_joints.p)
	{
		j->m_bodyA->m_joints.p->prev = &j->m_edgeA;
	}
	j->m_bodyA->m_joints.p = &j->m_edgeA;

	j->m_edgeB.joint = j;
	j->m_edgeB.other = j->m_bodyA;
	j->m_edgeB.prev = nullptr;
	j->m_edgeB.next = j->m_bodyB->m_joints.p;
	if (j->m_bodyB->m_joints.p)
	{
		j->m_bodyB->m_joints.p->prev = &j->m_edgeB;
	}
	j->m_bodyB->m_joints.p = &j->m_edgeB;

	auto bodyA = def.bodyA;
	auto bodyB = def.bodyB;

	// If the joint prevents collisions, then flag any contacts for filtering.
	if (!def.collideConnected)
	{
		for (auto&& edge: bodyB->GetContactEdges())
		{
			if (edge.other == bodyA)
			{
				// Flag the contact for filtering at the next time step (where either
				// body is awake).
				edge.contact->FlagForFiltering();
			}
		}
	}

	Add(*j);
	
	return j;
}

bool World::Add(Joint& j)
{
	m_joints.push_front(&j);
	return true;
}

bool World::Remove(Joint& j)
{
	const auto it = JointIterator{&j};
	return m_joints.erase(it) != it;
}

void World::Destroy(Joint* j)
{
	assert(!IsLocked());
	if (IsLocked())
	{
		return;
	}
	InternalDestroy(j);
}
	
void World::InternalDestroy(Joint* j)
{
	if (!Remove(*j))
	{
		return;
	}

	const auto collideConnected = j->m_collideConnected;
	
	// Disconnect from island graph.
	auto bodyA = j->m_bodyA;
	auto bodyB = j->m_bodyB;

	// Wake up connected bodies.
	bodyA->SetAwake();
	bodyB->SetAwake();

	// Remove from body 1.
	if (j->m_edgeA.prev)
	{
		j->m_edgeA.prev->next = j->m_edgeA.next;
	}

	if (j->m_edgeA.next)
	{
		j->m_edgeA.next->prev = j->m_edgeA.prev;
	}

	if (&j->m_edgeA == bodyA->m_joints.p)
	{
		bodyA->m_joints.p = j->m_edgeA.next;
	}

	j->m_edgeA.prev = nullptr;
	j->m_edgeA.next = nullptr;

	// Remove from body 2
	if (j->m_edgeB.prev)
	{
		j->m_edgeB.prev->next = j->m_edgeB.next;
	}

	if (j->m_edgeB.next)
	{
		j->m_edgeB.next->prev = j->m_edgeB.prev;
	}

	if (&j->m_edgeB == bodyB->m_joints.p)
	{
		bodyB->m_joints.p = j->m_edgeB.next;
	}

	j->m_edgeB.prev = nullptr;
	j->m_edgeB.next = nullptr;

	Joint::Destroy(j, m_blockAllocator);

	// If the joint prevents collisions, then flag any contacts for filtering.
	if (!collideConnected)
	{
		for (auto&& edge: bodyB->GetContactEdges())
		{
			if (edge.other == bodyA)
			{
				// Flag the contact for filtering at the next time step (where either
				// body is awake).
				edge.contact->FlagForFiltering();
			}
		}
	}
}

body_count_t World::AddToIsland(Island& island, Body& body)
{
	const auto index = static_cast<body_count_t>(island.m_bodies.size());
	body.m_islandIndex = index;
	island.m_bodies.push_back(&body);
	return index;
}

Island World::BuildIsland(Body& seed,
				  BodyList::size_type& remNumBodies,
				  contact_count_t& remNumContacts,
				  JointList::size_type& remNumJoints)
{
	assert(remNumBodies != 0);

	// Size the island for the remaining un-evaluated bodies, contacts, and joints.
	Island island(remNumBodies, remNumContacts, remNumJoints);

	// Perform a depth first search (DFS) on the constraint graph.
	auto stack = std::vector<Body*>();
	stack.reserve(remNumBodies);
	stack.push_back(&seed);
	seed.SetInIsland();
	while (!stack.empty())
	{
		// Grab the next body off the stack and add it to the island.
		const auto b = stack.back();
		stack.pop_back();
		
		assert(b->IsActive());
		AddToIsland(island, *b);
		--remNumBodies;
		
		// Make sure the body is awake.
		b->SetAwake();
		
		// To keep islands smaller, don't propagate islands across bodies that can't have a velocity (static bodies).
		if (!b->IsSpeedable())
		{
			continue;
		}
		
		const auto numContacts = island.m_contacts.size();
		// Adds appropriate contacts of current body and appropriate 'other' bodies of those contacts.
		for (auto&& ce: b->GetContactEdges())
		{
			const auto contact = ce.contact;
			if (!contact->IsInIsland() && contact->IsEnabled() && contact->IsTouching() && !HasSensor(*contact))
			{
				island.m_contacts.push_back(contact);
				contact->SetInIsland();
				const auto other = ce.other;
				if (!other->IsInIsland())
				{				
					stack.push_back(other);
					other->SetInIsland();
				}
			}			
		}
		remNumContacts -= island.m_contacts.size() - numContacts;
		
		const auto numJoints = island.m_joints.size();
		// Adds appropriate joints of current body and appropriate 'other' bodies of those joint.
		for (auto&& je: b->m_joints)
		{
			const auto joint = je.joint;
			const auto other = je.other;
			if (!joint->IsInIsland() && other->IsActive())
			{
				island.m_joints.push_back(joint);
				joint->SetInIsland(true);
				if (!other->IsInIsland())
				{					
					stack.push_back(other);
					other->SetInIsland();
				}
			}
		}
		remNumJoints -= island.m_joints.size() - numJoints;
	}
	
	return island;
}
	
RegStepStats World::SolveReg(const StepConf& step)
{
	auto stats = RegStepStats{};

	// Clear all the island flags.
	for (auto&& b: m_bodies)
	{
		b.UnsetInIsland();
	}
	for (auto&& c: m_contactMgr.GetContacts())
	{
		c.UnsetInIsland();
	}
	for (auto&& j: m_joints)
	{
		j.SetInIsland(false);
	}

	{
		auto remNumBodies = m_bodies.size(); ///< Remaining number of bodies.
		auto remNumContacts = m_contactMgr.GetContacts().size(); ///< Remaining number of contacts.
		auto remNumJoints = m_joints.size(); ///< Remaining number of joints.
		
		// Build and simulate all awake islands.
		for (auto&& body: m_bodies)
		{
			if (!body.IsInIsland() && body.IsSpeedable() && body.IsAwake() && body.IsActive())
			{
				++stats.islandsFound;

				auto island = BuildIsland(body, remNumBodies, remNumContacts, remNumJoints);
				
				// Updates bodies' sweep.pos0 to current sweep.pos1 and bodies' sweep.pos1 to new positions
				const auto solverResults = SolveReg(step, island);
				stats.maxIncImpulse = Max(stats.maxIncImpulse, solverResults.maxIncImpulse);
				stats.minSeparation = Min(stats.minSeparation, solverResults.minSeparation);
				if (solverResults.solved)
				{
					++stats.islandsSolved;
				}
				stats.sumPosIters += solverResults.positionIterations;
				stats.sumVelIters += solverResults.velocityIterations;

				if (IsValid(step.minStillTimeToSleep))
				{
					const auto minSleepTime = UpdateSleepTimes(island.m_bodies, step);
					if ((minSleepTime >= step.minStillTimeToSleep) && solverResults.solved)
					{
						stats.bodiesSlept += Sleepem(island.m_bodies);
					}
				}
				
				for (auto&& b: island.m_bodies)
				{
					// Allow static bodies to participate in other islands.
					if (!b->IsSpeedable())
					{
						b->UnsetInIsland();
						++remNumBodies;
					}
				}
			}		
		}
	}

	for (auto&& b: m_bodies)
	{
		// A non-static body that was in an island may have moved.
		if ((b.m_flags & (Body::e_velocityFlag|Body::e_islandFlag)) == (Body::e_velocityFlag|Body::e_islandFlag))
		{
			// Update fixtures (for broad-phase).
			b.SynchronizeFixtures();
		}
	}

	// Look for new contacts.
	stats.contactsAdded = m_contactMgr.FindNewContacts();
	
	return stats;
}

World::IslandSolverResults World::SolveReg(const StepConf& step, Island& island)
{
	auto finMinSeparation = std::numeric_limits<RealNum>::infinity();
	auto solved = false;
	auto positionIterations = step.regPositionIterations;

	auto bodyConstraints = BodyConstraints{};
	bodyConstraints.reserve(island.m_bodies.size());

	const auto h = step.get_dt(); ///< Time step (in seconds).
	
	// Update bodies' pos0 values then copy their pos1 and velocity data into local arrays.
	for (auto&& body: island.m_bodies)
	{
		body->m_sweep.pos0 = body->m_sweep.pos1; // like Advance0(1) on the sweep.
		const auto new_velocity = GetVelocity(*body, h);
		assert(IsValid(new_velocity));
		bodyConstraints.emplace_back(body->GetIslandIndex(),
									 body->GetInverseMass(),
									 body->GetInverseInertia(),
									 body->GetLocalCenter(),
									 body->m_sweep.pos1,
									 new_velocity);
	}
	auto positionConstraints = GetPositionConstraints(island.m_contacts, bodyConstraints);

	auto velocityConstraints = GetVelocityConstraints(island.m_contacts, bodyConstraints,
													  VelocityConstraint::Conf{step.doWarmStart? step.dtRatio: 0, step.velocityThreshold, true});
	
	if (step.doWarmStart)
	{
		WarmStartVelocities(velocityConstraints);
	}

	const auto psConf = ConstraintSolverConf{}
		.UseResolutionRate(step.regResolutionRate)
		.UseLinearSlop(step.linearSlop)
		.UseAngularSlop(step.angularSlop)
		.UseMaxLinearCorrection(step.maxLinearCorrection)
		.UseMaxAngularCorrection(step.maxAngularCorrection);

	for (auto&& joint: island.m_joints)
	{
		joint->InitVelocityConstraints(bodyConstraints, step, psConf);
	}
	
	auto velocityIterations = step.regVelocityIterations;
	auto maxIncImpulse = RealNum{0};
	for (auto i = decltype(step.regVelocityIterations){0}; i < step.regVelocityIterations; ++i)
	{
		for (auto&& joint: island.m_joints)
		{
			joint->SolveVelocityConstraints(bodyConstraints, step);
		}

		const auto newIncImpulse = SolveVelocityConstraints(velocityConstraints);
		maxIncImpulse = std::max(maxIncImpulse, newIncImpulse);
	}
	
	// updates array of tentative new body positions per the velocities as if there were no obstacles...
	IntegratePositions(bodyConstraints, h, MovementConf{step.maxTranslation, step.maxRotation});
	
	// Solve position constraints
	for (auto i = decltype(step.regPositionIterations){0}; i < step.regPositionIterations; ++i)
	{
		const auto minSeparation = SolvePositionConstraints(positionConstraints, psConf);
		finMinSeparation = Min(finMinSeparation, minSeparation);
		const auto contactsOkay = (minSeparation >= step.regMinSeparation);

		const auto jointsOkay = [&]()
		{
			auto allOkay = true;
			for (auto&& joint: island.m_joints)
			{
				if (!joint->SolvePositionConstraints(bodyConstraints, psConf))
				{
					allOkay = false;
				}
			}
			return allOkay;
		}();
		
		if (contactsOkay && jointsOkay)
		{
			// Reached tolerance, early out...
			positionIterations = i + 1;
			solved = true;
			break;
		}
	}
	
	// Update normal and tangent impulses of contacts' manifold points
	StoreImpulses(velocityConstraints, island.m_contacts);
	
	UpdateBodies(island.m_bodies, Span<const BodyConstraint>(bodyConstraints.data(), bodyConstraints.size()));

	if (m_contactMgr.m_contactListener)
	{
		Report(*m_contactMgr.m_contactListener, island.m_contacts, velocityConstraints,
			   solved? positionIterations - 1: StepConf::InvalidIteration);
	}
	return IslandSolverResults{finMinSeparation, maxIncImpulse, solved, positionIterations, velocityIterations};
}

void World::ResetBodiesForSolveTOI()
{
	for (auto&& b: m_bodies)
	{
		b.UnsetInIsland();
		b.m_sweep.ResetAlpha0();
	}
}

void World::ResetContactsForSolveTOI()
{
	for (auto&& c: m_contactMgr.GetContacts())
	{
		// Invalidate TOI
		c.UnsetInIsland();
		c.UnsetToi();
		c.ResetToiCount();
	}	
}

World::UpdateContactsData World::UpdateContactTOIs(const StepConf& step)
{
	contact_count_t numAtMaxSubSteps = 0;
	contact_count_t numUpdated = 0;
	UpdateContactsData::dist_iter_type maxDistIters = 0;
	UpdateContactsData::toi_iter_type maxToiIters = 0;
	UpdateContactsData::root_iter_type maxRootIters = 0;

	const auto toiConf = ToiConf{}
		.UseTimeMax(1)
		.UseTargetDepth(step.targetDepth)
		.UseTolerance(step.tolerance)
		.UseMaxRootIters(step.maxToiRootIters)
		.UseMaxToiIters(step.maxToiIters)
		.UseMaxDistIters(step.maxDistanceIters);
	
	for (auto&& c: m_contactMgr.GetContacts())
	{
		if (c.HasValidToi())
		{
			continue;
		}
		if (!c.IsEnabled() || HasSensor(c) || !IsActive(c) || !IsImpenetrable(c))
		{
			continue;
		}
		if (c.GetToiCount() >= step.maxSubSteps)
		{
			// What are the pros/cons of this?
			// Larger m_maxSubSteps slows down the simulation.
			// m_maxSubSteps of 44 and higher seems to decrease the occurrance of tunneling of multiple
			// bullet body collisions with static objects.
			++numAtMaxSubSteps;
			continue;
		}
		const auto output = c.UpdateForCCD(toiConf);
		maxDistIters = Max(maxDistIters, output.maxDistIters);
		maxToiIters = Max(maxToiIters, output.toiIters);
		maxRootIters = Max(maxRootIters, output.maxRootIters);
		++numUpdated;
	}
	
	return UpdateContactsData{numAtMaxSubSteps, numUpdated, maxDistIters, maxToiIters, maxRootIters};
}
	
World::ContactToiData World::GetSoonestContact()
{
	auto minToi = std::numeric_limits<RealNum>::infinity();
	auto minContact = static_cast<Contact*>(nullptr);
	auto count = contact_count_t{0};
	for (auto&& c: m_contactMgr.GetContacts())
	{
		if (c.HasValidToi())
		{
			const auto toi = c.GetToi();
			if (minToi > toi)
			{
				minToi = toi;
				minContact = &c;
				count = contact_count_t{1};
			}
			else if (minToi == toi)
			{
				// Have multiple contacts at the current minimum time of impact.
				++count;

				// Should the first one found be dealt with first?
				// Does ordering of these contacts even matter?
				//
				//   Presumably if these contacts are independent then ordering won't matter
				//   since they'd be dealt with in separate islands anyway.
				//   OTOH, if these contacts are dependent, then the contact returned will be
				//   first to have its two bodies positions handled for impact before other
				//   bodies which seems like it will matter.
				//
				//   Prioritizing contact with non-accelerable body doesn't prevent
				//   tunneling of bullet objects through static objects in the multi body
				//   collision case however.
#if 1
				if (!c.GetFixtureB()->GetBody()->IsAccelerable() ||
					!c.GetFixtureB()->GetBody()->IsAccelerable())
				{
					minContact = &c;
				}
#endif
			}
		}
	}
	return ContactToiData{count, minContact, minToi};
}

// Find TOI contacts and solve them.
ToiStepStats World::SolveTOI(const StepConf& step)
{
	auto stats = ToiStepStats{};

	if (IsStepComplete())
	{
		ResetBodiesForSolveTOI();
		ResetContactsForSolveTOI();
	}

	// Find TOI events and solve them.
	for (;;)
	{
		const auto updateData = UpdateContactTOIs(step);
		stats.contactsAtMaxSubSteps += updateData.numAtMaxSubSteps;
		stats.contactsUpdatedToi += updateData.numUpdatedTOI;
		stats.maxDistIters = Max(stats.maxDistIters, updateData.maxDistIters);
		stats.maxRootIters = Max(stats.maxRootIters, updateData.maxRootIters);
		stats.maxToiIters = Max(stats.maxToiIters, updateData.maxToiIters);
		
		const auto next = GetSoonestContact();
		if ((!next.contact) || (next.toi >= 1))
		{
			// No more TOI events to handle within the current time step. Done!
			SetStepComplete(true);
			break;
		}

		++stats.contactsFound;
		const auto solverResults = SolveTOI(step, *next.contact);
		stats.minSeparation = Min(stats.minSeparation, solverResults.minSeparation);
		stats.maxIncImpulse = Max(stats.maxIncImpulse, solverResults.maxIncImpulse);
		if (solverResults.solved)
		{
			++stats.islandsSolved;
		}
		if ((solverResults.positionIterations > 0) || (solverResults.velocityIterations > 0))
		{
			++stats.islandsFound;
			stats.sumPosIters += solverResults.positionIterations;
			stats.sumVelIters += solverResults.velocityIterations;
		}
		
		// Commit fixture proxy movements to the broad-phase so that new contacts are created.
		// Also, some contacts can be destroyed.
		stats.contactsAdded += m_contactMgr.FindNewContacts();

		if (GetSubStepping())
		{
			SetStepComplete(false);
			break;
		}
	}
	return stats;
}

World::IslandSolverResults World::SolveTOI(const StepConf& step, Contact& contact)
{
	const auto toi = contact.GetToi();
	auto bA = contact.GetFixtureA()->GetBody();
	auto bB = contact.GetFixtureB()->GetBody();

	{
		const auto backupA = bA->m_sweep;
		const auto backupB = bB->m_sweep;

		// Advance the bodies to the TOI.
		bA->Advance(toi);
		bB->Advance(toi);

		// The TOI contact likely has some new contact points.
		contact.SetEnabled();	
		contact.Update(m_contactMgr.m_contactListener);
		contact.UnsetToi();

		++contact.m_toiCount;

		// Is contact disabled or separated?
		if (!contact.IsEnabled() || !contact.IsTouching())
		{
			// Restore the sweeps by undoing the body "advance" calls (and anything else done movement-wise)
			contact.UnsetEnabled();
			bA->m_sweep = backupA;
			bA->m_xf = GetTransform1(bA->m_sweep);
			bB->m_sweep = backupB;
			bB->m_xf = GetTransform1(bB->m_sweep);
			return IslandSolverResults{};
		}
	}

	bA->SetAwake();
	bB->SetAwake();

	// Build the island
	Island island(m_bodies.size(), m_contactMgr.GetContacts().size(), 0);

	AddToIsland(island, *bA);
	bA->SetInIsland();
	AddToIsland(island, *bB);
	bB->SetInIsland();
	island.m_contacts.push_back(&contact);
	contact.SetInIsland();

	// Process the contacts of the two bodies, adding appropriate ones to the island,
	// adding appropriate other bodies of added contacts, and advancing those other
	// bodies sweeps and transforms to the minimum contact's TOI.
	if (bA->IsAccelerable())
	{
		ProcessContactsForTOI(island, *bA, toi, m_contactMgr.m_contactListener);
	}
	if (bB->IsAccelerable())
	{
		ProcessContactsForTOI(island, *bB, toi, m_contactMgr.m_contactListener);
	}

	// Now solve for remainder of time step
	const auto results = SolveTOI(StepConf{step}.set_dt((1 - toi) * step.get_dt()), island);

	// Reset island flags and synchronize broad-phase proxies.
	for (auto&& body: island.m_bodies)
	{
		body->UnsetInIsland();

		if (body->IsAccelerable())
		{
			body->SynchronizeFixtures();
			ResetContactsForSolveTOI(*body);
		}
	}
	
	return results;
}

void World::UpdateBodies(Span<Body*> bodies,
						 Span<const BodyConstraint> constraints)
{
	auto i = size_t{0};
	for (auto&& b: bodies)
	{
		b->m_velocity = constraints[i].GetVelocity(); // sets what Body::GetVelocity returns
		b->m_sweep.pos1 = constraints[i].GetPosition(); // sets what Body::GetWorldCenter returns
		b->m_xf = GetTransformation(b->m_sweep.pos1, b->m_sweep.GetLocalCenter()); // sets what Body::GetLocation returns
		++i;
	}
}

World::IslandSolverResults World::SolveTOI(const StepConf& step, Island& island)
{
	auto bodyConstraints = BodyConstraints{};
	bodyConstraints.reserve(island.m_bodies.size());

	// Initialize the body state.
	for (auto&& body: island.m_bodies)
	{
		bodyConstraints.emplace_back(body->GetIslandIndex(),
									 body->GetInverseMass(),
									 body->GetInverseInertia(),
									 body->GetLocalCenter(),
									 body->m_sweep.pos1,
									 body->GetVelocity());
	}
	
	auto positionConstraints = GetPositionConstraints(island.m_contacts, bodyConstraints);
	
	// Solve TOI-based position constraints.
	auto finMinSeparation = std::numeric_limits<RealNum>::infinity();
	auto solved = false;
	auto positionIterations = step.toiPositionIterations;
	
	{
		const auto psConf = ConstraintSolverConf{}
			.UseResolutionRate(step.toiResolutionRate)
			.UseLinearSlop(step.linearSlop)
			.UseAngularSlop(step.angularSlop)
			.UseMaxLinearCorrection(step.maxLinearCorrection)
			.UseMaxAngularCorrection(step.maxAngularCorrection);

		for (auto i = decltype(step.toiPositionIterations){0}; i < step.toiPositionIterations; ++i)
		{
			//
			// Note: There are two flavors of the SolvePositionConstraints function.
			//   One takes an extra two arguments that are the indexes of two bodies that are okay to
			//   move. The other one does not.
			//   Calling the selective solver (that takes the two additional arguments) appears to
			//   result in phsyics simulations that are more prone to tunneling. Meanwhile, using the
			//   non-selective solver would presumably be slower (since it appears to have more that
			//   it will do). Assuming that slower is preferable to tunnelling, then the non-selective
			//   function is the one to be calling here.
			//
			const auto minSeparation = SolvePositionConstraints(positionConstraints, psConf);
			finMinSeparation = Min(finMinSeparation, minSeparation);
			if (minSeparation >= step.toiMinSeparation)
			{
				// Reached tolerance, early out...
				positionIterations = i + 1;
				solved = true;
				break;
			}
		}
	}
	
	// Leap of faith to new safe state.
	// Not doing this results in slower simulations.
	// Originally this update was only done to island.m_bodies 0 and 1.
	// Unclear whether rest of bodies should also be updated. No difference noticed.
	for (auto i = decltype(bodyConstraints.size()){0}; i < bodyConstraints.size(); ++i)
	{
		island.m_bodies[i]->m_sweep.pos0 = bodyConstraints[i].GetPosition();
	}
	
	auto velocityConstraints = GetVelocityConstraints(island.m_contacts, bodyConstraints,
													  VelocityConstraint::Conf{0, step.velocityThreshold, true});

	// No warm starting is needed for TOI events because warm
	// starting impulses were applied in the discrete solver.

	// Solve velocity constraints.
	auto maxIncImpulse = RealNum{0};
	auto velocityIterations = step.toiVelocityIterations;
	for (auto i = decltype(step.toiVelocityIterations){0}; i < step.toiVelocityIterations; ++i)
	{
		const auto newIncImpulse = SolveVelocityConstraints(velocityConstraints);
		maxIncImpulse = std::max(maxIncImpulse, newIncImpulse);
	}
	
	// Don't store TOI contact forces for warm starting because they can be quite large.
	
	IntegratePositions(bodyConstraints, step.get_dt(), MovementConf{step.maxTranslation, step.maxRotation});
	
	UpdateBodies(island.m_bodies, Span<const BodyConstraint>(bodyConstraints.data(), bodyConstraints.size()));

	if (m_contactMgr.m_contactListener)
	{
		Report(*m_contactMgr.m_contactListener, island.m_contacts, velocityConstraints,
			   positionIterations);
	}
	
	return IslandSolverResults{finMinSeparation, maxIncImpulse, solved, positionIterations, velocityIterations};
}
	
void World::ResetContactsForSolveTOI(Body& body)
{
	// Invalidate all contact TOIs on this displaced body.
	for (auto&& ce: body.GetContactEdges())
	{
		ce.contact->UnsetInIsland();
		ce.contact->UnsetToi();
	}
}

void World::ProcessContactsForTOI(Island& island, Body& body, RealNum toi, ContactListener* listener)
{
	assert(body.IsAccelerable());

	for (auto&& ce: body.GetContactEdges())
	{
		auto contact = ce.contact;
		auto other = ce.other;

		if (!contact->IsInIsland() && !HasSensor(*contact) && (other->IsImpenetrable() || body.IsImpenetrable()))
		{
			// Tentatively advance the body to the TOI.
			const auto backup = other->m_sweep;
			if (!other->IsInIsland())
			{
				other->Advance(toi);
			}
			
			// Update the contact points
			contact->SetEnabled();
			contact->Update(listener);
			
			// Revert and skip if contact disabled by user or no contact points anymore.
			if (!contact->IsEnabled() || !contact->IsTouching())
			{
				other->m_sweep = backup;
				other->m_xf = GetTransform1(other->m_sweep);
				continue;
			}
			
			island.m_contacts.push_back(contact);
			contact->SetInIsland();
			
			if (!other->IsInIsland())
			{
				other->SetInIsland();			
				if (other->IsSpeedable())
				{
					other->SetAwake();
				}
				AddToIsland(island, *other);
			}		
		}		
	}
}

StepStats World::Step(const StepConf& conf)
{
	assert((m_maxVertexRadius * 2) + (conf.linearSlop / 4) > (m_maxVertexRadius * 2));

	auto stepStats = StepStats{};

	if (HasNewFixtures())
	{
		UnsetNewFixtures();
		
		// New fixtures were added: need to find and create the new contacts.
		stepStats.pre.added = m_contactMgr.FindNewContacts();
	}

	assert(!IsLocked());
	FlagGuard<decltype(m_flags)> flagGaurd(m_flags, e_locked);

	// Update and destroy contacts. No new contacts are created though.
	const auto collideStats = m_contactMgr.Collide();
	stepStats.pre.ignored = collideStats.ignored;
	stepStats.pre.destroyed = collideStats.destroyed;
	stepStats.pre.updated = collideStats.updated;

	if (conf.get_dt() > 0)
	{
		m_inv_dt0 = conf.get_inv_dt();

		// Integrate velocities, solve velocity constraints, and integrate positions.
		if (IsStepComplete())
		{
			stepStats.reg = SolveReg(conf);
		}

		// Handle TOI events.
		if (conf.doToi)
		{
			stepStats.toi = SolveTOI(conf);
		}
	}
	return stepStats;
}

struct WorldQueryWrapper
{
	using size_type = BroadPhase::size_type;

	bool QueryCallback(size_type proxyId)
	{
		const auto proxy = static_cast<FixtureProxy*>(broadPhase->GetUserData(proxyId));
		return callback->ReportFixture(proxy->fixture);
	}

	const BroadPhase* broadPhase;
	QueryFixtureReporter* callback;
};

void World::QueryAABB(QueryFixtureReporter* callback, const AABB& aabb) const
{
	WorldQueryWrapper wrapper;
	wrapper.broadPhase = &m_contactMgr.m_broadPhase;
	wrapper.callback = callback;
	m_contactMgr.m_broadPhase.Query(&wrapper, aabb);
}

struct WorldRayCastWrapper
{
	using size_type = BroadPhase::size_type;

	RealNum RayCastCallback(const RayCastInput& input, size_type proxyId)
	{
		auto userData = broadPhase->GetUserData(proxyId);
		const auto proxy = static_cast<FixtureProxy*>(userData);
		auto fixture = proxy->fixture;
		const auto index = proxy->childIndex;
		const auto output = RayCast(*fixture, input, index);

		if (output.hit)
		{
			const auto fraction = output.fraction;
			assert(fraction >= 0 && fraction <= 1);
			const auto point = (RealNum{1} - fraction) * input.p1 + fraction * input.p2;
			return callback->ReportFixture(fixture, point, output.normal, fraction);
		}

		return input.maxFraction;
	}

	WorldRayCastWrapper() = delete;

	constexpr WorldRayCastWrapper(const BroadPhase* bp, RayCastFixtureReporter* cb): broadPhase(bp), callback(cb) {}

	const BroadPhase* const broadPhase;
	RayCastFixtureReporter* const callback;
};

void World::RayCast(RayCastFixtureReporter* callback, const Vec2& point1, const Vec2& point2) const
{
	WorldRayCastWrapper wrapper(&m_contactMgr.m_broadPhase, callback);
	const auto input = RayCastInput{point1, point2, RealNum{1}};
	m_contactMgr.m_broadPhase.RayCast(&wrapper, input);
}

World::size_type World::GetProxyCount() const noexcept
{
	return m_contactMgr.m_broadPhase.GetProxyCount();
}

World::size_type World::GetTreeHeight() const noexcept
{
	return m_contactMgr.m_broadPhase.GetTreeHeight();
}

World::size_type World::GetTreeBalance() const
{
	return m_contactMgr.m_broadPhase.GetTreeBalance();
}

RealNum World::GetTreeQuality() const
{
	return m_contactMgr.m_broadPhase.GetTreeQuality();
}

void World::ShiftOrigin(const Vec2 newOrigin)
{
	assert(!IsLocked());
	if (IsLocked())
	{
		return;
	}

	for (auto&& b: m_bodies)
	{
		b.m_xf.p -= newOrigin;
		b.m_sweep.pos0.linear -= newOrigin;
		b.m_sweep.pos1.linear -= newOrigin;
	}

	for (auto&& j: m_joints)
	{
		j.ShiftOrigin(newOrigin);
	}

	m_contactMgr.m_broadPhase.ShiftOrigin(newOrigin);
}
	
bool World::IsActive(const Contact& contact) noexcept
{
	const auto bA = contact.GetFixtureA()->GetBody();
	const auto bB = contact.GetFixtureB()->GetBody();
	
	const auto activeA = IsAllFlagsSet(bA->m_flags, Body::e_awakeFlag|Body::e_velocityFlag);
	const auto activeB = IsAllFlagsSet(bB->m_flags, Body::e_awakeFlag|Body::e_velocityFlag);
	
	// Is at least one body active (awake and dynamic or kinematic)?
	return activeA || activeB;
}

StepStats Step(World& world, RealNum dt, World::ts_iters_type velocityIterations, World::ts_iters_type positionIterations)
{
	StepConf step;
	step.set_dt(dt);
	step.regVelocityIterations = velocityIterations;
	step.regPositionIterations = positionIterations;
	step.toiVelocityIterations = velocityIterations;
	if (positionIterations == 0)
	{
		step.toiPositionIterations = 0;
	}
	step.dtRatio = dt * world.GetInvDeltaTime();
	return world.Step(step);
}

size_t GetFixtureCount(const World& world) noexcept
{
	size_t sum = 0;
	for (auto&& b: world.GetBodies())
	{
		sum += GetFixtureCount(b);
	}
	return sum;
}

size_t GetShapeCount(const World& world) noexcept
{
	auto shapes = std::set<const Shape*>();
	for (auto&& b: world.GetBodies())
	{
		for (auto&& f: b.GetFixtures())
		{
			shapes.insert(f.GetShape());
		}
	}
	return shapes.size();
}

size_t GetAwakeCount(const World& world) noexcept
{
	auto count = size_t(0);
	for (auto&& body: world.GetBodies())
	{
		if (body.IsAwake())
		{
			++count;
		}
	}
	return count;
}
	
size_t Awaken(World& world)
{
	auto awoken = size_t{0};
	for (auto&& b: world.GetBodies())
	{
		if (b.SetAwake())
		{
			++awoken;
		}
	}
	return awoken;
}

void ClearForces(World& world) noexcept
{
	const auto g = world.GetGravity();
	for (auto&& body: world.GetBodies())
	{
		body.SetAcceleration(g, 0_rad);
	}
}

} // namespace box2d
