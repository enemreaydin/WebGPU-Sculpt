#include "SdfObjectFactory.h"

SdfObjectData SdfObjectFactory::CreateSphere(float radius)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Sphere);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    obj.size = { radius, 0.0f, 0.0f, 0.0f };
    obj.color = { 0.0f, 0.45f, 0.85f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateBox(float x, float y, float z)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Box);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    obj.size = { x, y, z, 0.0f };
    obj.color = { 0.8f, 0.2f, 0.1f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateCylinder(float radius, float height)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Cylinder);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    obj.size = { radius, height, 0.0f, 0.0f };
    obj.color = { 0.2f, 0.8f, 0.3f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateTorus(float majorRadius, float minorRadius)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Torus);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };

    // size.x = major radius, size.y = minor radius
    obj.size = { majorRadius, minorRadius, 0.0f, 0.0f };

    obj.color = { 0.9f, 0.6f, 0.1f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateLink(float length, float radius, float thickness)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Link);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // size.x = ring radius (r1), size.y = segment half-length (le), size.z = tube thickness (r2)
    obj.size = { radius, length, thickness, 0.0f };
    obj.color = { 0.7f, 0.4f, 0.85f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateCone(float height, float angle)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Cone);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // size.x = height, size.y = half-angle (radians); shader derives sin/cos
    obj.size = { height, angle, 0.0f, 0.0f };
    obj.color = { 0.85f, 0.5f, 0.2f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateCapsule(float radius, float halfHeight)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Capsule);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // size.x = radius, size.y = segment half-length along Y
    obj.size = { radius, halfHeight, 0.0f, 0.0f };
    obj.color = { 0.2f, 0.7f, 0.8f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreateSolidAngle(float radius, float angle)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::SolidAngle);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // size.x = radius, size.y = half-angle (radians); shader derives sin/cos
    obj.size = { radius, angle, 0.0f, 0.0f };
    obj.color = { 0.9f, 0.3f, 0.4f, 0.0f };

    return obj;
}

SdfObjectData SdfObjectFactory::CreatePyramid(float halfBase, float height)
{
    SdfObjectData obj{};

    obj.type = static_cast<int32_t>(SdfObjectType::Pyramid);
    obj.parentId = -1;

    obj.position = { 0.0f, 0.0f, 0.0f, 0.0f };
    obj.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // size.x = base half-width (square base), size.y = height
    obj.size = { halfBase, height, 0.0f, 0.0f };
    obj.color = { 0.6f, 0.75f, 0.25f, 0.0f };

    return obj;
}
