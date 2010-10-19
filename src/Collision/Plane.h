#ifndef PLANE_H
#define PLANE_H

#include "CollisionShape.h"
#include "Math.h"
#include "Properties.h"


/// Plane collision shape
class Plane: public CollisionShape
{
	PROPERTY_RW(Vec3, normal, setNormal, getNormal)
	PROPERTY_RW(float, offset, setOffset, getOffset)

	public:
		Plane(): CollisionShape(CST_PLANE) {}
		Plane(const Plane& b);
		Plane(const Vec3& normal_, float offset_);

		/// @see setFrom3Points
		Plane(const Vec3& p0, const Vec3& p1, const Vec3& p2);

		/// @see setFromPlaneEquation
		Plane(float a, float b, float c, float d);

		/// Return the transformed
		Plane getTransformed(const Vec3& translate, const Mat3& rotate, float scale) const;

		/// It gives the distance between a point and a plane. if returns >0 then the point lies in front of the plane,
		/// if <0 then it is behind and if =0 then it is co-planar
		float test(const Vec3& point) const {return normal.dot(point) - offset;}

		/// Get the distance from a point to this plane
		float getDistance(const Vec3& point) const {return fabs(test(point));}

		/// Returns the perpedicular point of a given point in this plane. Plane's normal and returned-point are
		/// perpedicular
		Vec3 getClosestPoint(const Vec3& point) const {return point - normal * test(point);}

		/// Do nothing
		float testPlane(const Plane&) const {return 0.0;}

	private:
		/// Set the plane from 3 points
		void setFrom3Points(const Vec3& p0, const Vec3& p1, const Vec3& p2);

		/// Set from plane equation is ax+by+cz+d
		void setFromPlaneEquation(float a, float b, float c, float d);
};


inline Plane::Plane(const Plane& b):
	CollisionShape(CST_PLANE),
	normal(b.normal),
	offset(b.offset)
{}


inline Plane::Plane(const Vec3& normal_, float offset_):
	CollisionShape(CST_PLANE),
	normal(normal_),
	offset(offset_)
{}


inline Plane::Plane(const Vec3& p0, const Vec3& p1, const Vec3& p2):
	CollisionShape(CST_PLANE)
{
	setFrom3Points(p0, p1, p2);
}


inline Plane::Plane(float a, float b, float c, float d):
	CollisionShape(CST_PLANE)
{
	setFromPlaneEquation(a, b, c, d);
}


#endif