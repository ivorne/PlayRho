/*
 * Original work Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
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

#include <Box2D/Dynamics/Contacts/Contact.hpp>

#include <Box2D/Collision/Collision.hpp>
#include <Box2D/Collision/TimeOfImpact.hpp>
#include <Box2D/Collision/CollideShapes.hpp>
#include <Box2D/Collision/Shapes/Shape.hpp>
#include <Box2D/Collision/Shapes/ChainShape.hpp>
#include <Box2D/Collision/Shapes/CircleShape.hpp>
#include <Box2D/Collision/Shapes/PolygonShape.hpp>
#include <Box2D/Collision/Shapes/EdgeShape.hpp>
#include <Box2D/Dynamics/Body.hpp>
#include <Box2D/Dynamics/Fixture.hpp>
#include <Box2D/Dynamics/World.hpp>

using namespace box2d;

using ContactCreateFcn = Contact* (Fixture* fixtureA, child_count_t indexA,
									   Fixture* fixtureB, child_count_t indexB);
using ContactDestroyFcn = void (Contact* contact);

struct ContactRegister
{
	Contact::ManifoldCalcFunc calcfunc;
	const bool primary;
};

static Manifold GetChainCircleManifold(const Fixture* fixtureA, child_count_t indexA,
									   const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	const auto edge = (static_cast<const ChainShape*>(fixtureA->GetShape()))->GetChildEdge(indexA);
	return CollideShapes(edge, xfA, *static_cast<const CircleShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetChainPolygonManifold(const Fixture* fixtureA, child_count_t indexA,
										const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	const auto edge = static_cast<const ChainShape*>(fixtureA->GetShape())->GetChildEdge(indexA);
	return CollideShapes(edge, xfA, *static_cast<const PolygonShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetCircleCircleManifold(const Fixture* fixtureA, child_count_t,
										const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const CircleShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const CircleShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetEdgeCircleManifold(const Fixture* fixtureA, child_count_t,
									  const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const EdgeShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const CircleShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetEdgeEdgeManifold(const Fixture* fixtureA, child_count_t,
									const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const EdgeShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const EdgeShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetEdgePolygonManifold(const Fixture* fixtureA, child_count_t,
									   const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const EdgeShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const PolygonShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetPolygonCircleManifold(const Fixture* fixtureA, child_count_t,
										 const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const PolygonShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const CircleShape*>(fixtureB->GetShape()), xfB);
}

static Manifold GetPolygonPolygonManifold(const Fixture* fixtureA, child_count_t,
										  const Fixture* fixtureB, child_count_t)
{
	const auto xfA = fixtureA->GetBody()->GetTransformation();
	const auto xfB = fixtureB->GetBody()->GetTransformation();
	return CollideShapes(*static_cast<const PolygonShape*>(fixtureA->GetShape()), xfA,
						 *static_cast<const PolygonShape*>(fixtureB->GetShape()), xfB);
}

// Order dependent on Shape::Type enumeration values
static constexpr ContactRegister s_registers[Shape::e_typeCount][Shape::e_typeCount] =
{
	// circle-* contacts
	{
		{GetCircleCircleManifold, true}, // circle
		{GetEdgeCircleManifold, false}, // edge
		{GetPolygonCircleManifold, false}, // polygon
		{GetChainCircleManifold, false}, // chain
	},
	// edge-* contacts
	{
		{GetEdgeCircleManifold, true}, // circle
		{GetEdgeEdgeManifold, false}, // edge
		{GetEdgePolygonManifold, true}, // polygon
		{nullptr, false}, // chain
	},
	// polygon-* contacts
	{
		{GetPolygonCircleManifold, true}, // circle
		{GetEdgePolygonManifold, false}, // edge
		{GetPolygonPolygonManifold, true}, // polygon
		{GetChainPolygonManifold, false}, // chain
	},
	// chain-* contacts
	{
		{GetChainCircleManifold, true}, // circle
		{nullptr, false}, // edge
		{GetChainPolygonManifold, true}, // polygon
		{nullptr, false}, // chain
	},
};

Contact* Contact::Create(Fixture& fixtureA, child_count_t indexA,
						 Fixture& fixtureB, child_count_t indexB)
{
	const auto type1 = GetType(fixtureA);
	const auto type2 = GetType(fixtureB);

	assert(0 <= type1 && type1 < Shape::e_typeCount);
	assert(0 <= type2 && type2 < Shape::e_typeCount);
	
	const auto calcfunc = s_registers[type1][type2].calcfunc;
	if (calcfunc)
	{
		return (s_registers[type1][type2].primary)?
			new Contact{&fixtureA, indexA, &fixtureB, indexB, calcfunc}:
			new Contact{&fixtureB, indexB, &fixtureA, indexA, calcfunc};
	}
	return nullptr;
}

void Contact::Destroy(Contact* contact)
{
	const auto fixtureA = contact->GetFixtureA();
	const auto fixtureB = contact->GetFixtureB();

	if ((contact->m_manifold.GetPointCount() > 0) &&
		!fixtureA->IsSensor() && !fixtureB->IsSensor())
	{
		// Contact may have been keeping accelerable bodies of fixture A or B from moving.
		// Need to awaken those bodies now in case they are again movable.
		SetAwake(*fixtureA);
		SetAwake(*fixtureB);
	}
	
	delete contact;
}

Contact::Contact(Fixture* fA, child_count_t indexA, Fixture* fB, child_count_t indexB,
				 ManifoldCalcFunc mcf):
	m_manifoldCalcFunc{mcf},
	m_fixtureA{fA}, m_fixtureB{fB}, m_indexA{indexA}, m_indexB{indexB},
	m_friction{MixFriction(fA->GetFriction(), fB->GetFriction())},
	m_restitution{MixRestitution(fA->GetRestitution(), fB->GetRestitution())}
{
	assert(fA->GetShape());
	assert(fB->GetShape());
}

void Contact::Update(ContactListener* listener)
{
	const auto oldManifold = m_manifold;

	// Note: do not assume the fixture AABBs are overlapping or are valid.
	const auto oldTouching = IsTouching();
	auto newTouching = false;

	const auto fixtureA = GetFixtureA();
	const auto fixtureB = GetFixtureB();

	const auto bodyA = fixtureA->GetBody();
	const auto bodyB = fixtureB->GetBody();
	
	assert(bodyA);
	assert(bodyB);

	const auto sensor = fixtureA->IsSensor() || fixtureB->IsSensor();
	if (sensor)
	{
		const auto shapeA = fixtureA->GetShape();
		const auto shapeB = fixtureB->GetShape();
		const auto xfA = bodyA->GetTransformation();
		const auto xfB = bodyB->GetTransformation();
		
		newTouching = TestOverlap(*shapeA, GetChildIndexA(), xfA, *shapeB, GetChildIndexB(), xfB);

		// Sensors don't generate manifolds.
		m_manifold = Manifold{};
	}
	else
	{
		auto newManifold = Evaluate();
		
		const auto old_point_count = oldManifold.GetPointCount();
		const auto new_point_count = newManifold.GetPointCount();

		newTouching = new_point_count > 0;

		// Match old contact ids to new contact ids and copy the
		// stored impulses to warm start the solver.
		for (auto i = decltype(new_point_count){0}; i < new_point_count; ++i)
		{
			const auto new_cf = newManifold.GetContactFeature(i);
			for (auto j = decltype(old_point_count){0}; j < old_point_count; ++j)
			{
				if (new_cf == oldManifold.GetContactFeature(j))
				{
					newManifold.SetContactImpulses(i, oldManifold.GetContactImpulses(j));
					break;
				}
			}
		}

		m_manifold = newManifold;
		
#ifdef MAKE_CONTACT_PROCESSING_ORDER_DEPENDENT
		/*
		 * The following code creates an ordering dependency in terms of update processing
		 * over a container of contacts. It also puts this method into the situation of
		 * modifying bodies which adds race potential in a multi-threaded mode of operation.
		 * Lastly, without this code, the step-statistics show a world getting to sleep in
		 * less TOI position iterations.
		 */
		if (newTouching != oldTouching)
		{
			bodyA->SetAwake();
			bodyB->SetAwake();
		}
#endif
	}

	if (!oldTouching && newTouching)
	{
		SetTouching();
		if (listener)
		{
			listener->BeginContact(*this);
		}
	}
	else if (oldTouching && !newTouching)
	{
		UnsetTouching();
		if (listener)
		{
			listener->EndContact(*this);
		}
	}
	
	if (!sensor && newTouching)
	{
		if (listener)
		{
			listener->PreSolve(*this, oldManifold);
		}
	}
}

bool box2d::HasSensor(const Contact& contact) noexcept
{
	return contact.GetFixtureA()->IsSensor() || contact.GetFixtureB()->IsSensor();
}

bool box2d::IsImpenetrable(const Contact& contact) noexcept
{
	const auto bA = contact.GetFixtureA()->GetBody();
	const auto bB = contact.GetFixtureB()->GetBody();
	return bA->IsImpenetrable() || bB->IsImpenetrable();
}

void box2d::SetAwake(Contact& c) noexcept
{
	SetAwake(*c.GetFixtureA());
	SetAwake(*c.GetFixtureB());
}

/// Resets the friction mixture to the default value.
void box2d::ResetFriction(Contact& contact)
{
	contact.SetFriction(MixFriction(contact.GetFixtureA()->GetFriction(), contact.GetFixtureB()->GetFriction()));
}

/// Reset the restitution to the default value.
void box2d::ResetRestitution(Contact& contact) noexcept
{
	contact.SetRestitution(MixRestitution(contact.GetFixtureA()->GetRestitution(), contact.GetFixtureB()->GetRestitution()));
}
