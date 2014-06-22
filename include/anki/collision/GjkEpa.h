// Copyright (C) 2014, Panagiotis Christopoulos Charitos.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#ifndef ANKI_COLLISION_GJK_EPA_H
#define ANKI_COLLISION_GJK_EPA_H

#include "anki/Math.h"
#include "anki/collision/ContactPoint.h"

namespace anki {

// Forward
class ConvexShape;

/// @addtogroup collision
/// @{

/// The implementation of the GJK algorithm. This algorithm is being used for
/// checking the intersection between convex shapes.
class Gjk
{
public:
	/// Return true if the two convex shapes intersect
	Bool intersect(const ConvexShape& shape0, const ConvexShape& shape1);

protected:
	U32 m_count; ///< Simplex count
	Vec4 m_a, m_b, m_c, m_d; ///< Simplex
	Vec4 m_dir;

	/// Compute the support
	static Vec4 support(const ConvexShape& shape0, const ConvexShape& shape1,
		const Vec4& dir);

	/// Update simplex
	Bool update(const Vec4& a);

	/// Helper of axbxa
	static Vec4 crossAba(const Vec4& a, const Vec4& b)
	{
		return (a.cross(b)).cross(a);
	}
};

/// The implementation of EPA
class GjkEpa: public Gjk
{
public:
	Bool intersect(const ConvexShape& shape0, const ConvexShape& shape1,
		ContactPoint& contact);

private:
	Array<Vec4, 100> m_simplex;

	void findClosestEdge(F32& distance, Vec4& normal, U& index);
};

/// @}

} // end namespace anki

#endif

