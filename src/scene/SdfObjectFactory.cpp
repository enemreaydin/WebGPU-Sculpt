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
