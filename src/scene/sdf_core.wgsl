struct SdfObject
{
    obj_type : i32,
    op_type : i32,

    pad0 : i32,
    pad1 : i32,

    position : vec4f,
    size : vec4f,
    color : vec4f,
};

struct SceneResult
{
    distance : f32,
    color : vec3f,
};

fn sdfSphere(p : vec3f, radius : f32) -> f32
{
    return length(p) - radius;
}

fn sdfBox(p : vec3f, b : vec3f) -> f32
{
    let q = abs(p) - b;
    return length(max(q, vec3f(0.0)))
        + min(max(q.x, max(q.y, q.z)), 0.0);
}

fn sdfCylinder(p: vec3f, radius: f32, height: f32) -> f32
{
    let d = abs(vec2f(length(p.xz), p.y)) - vec2f(radius, height * 0.5);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2f(0.0)));
}

fn sdfTorus(p: vec3f, majorRadius: f32, minorRadius: f32) -> f32
{
    let q = vec2f(length(p.xz) - majorRadius, p.y);
    return length(q) - minorRadius;
}

fn opUnion(d1 : f32, d2 : f32) -> f32
{
    return min(d1, d2);
}

fn opSubtract(d1 : f32, d2 : f32) -> f32
{
    return max(-d1, d2);
}

fn opIntersection(d1 : f32, d2 : f32) -> f32
{
    return max(d1, d2);
}

fn evaluateScene(p : vec3f) -> SceneResult
{
    var result : SceneResult;

    result.distance = 100000.0;
    result.color = vec3f(0.7);

    let count = arrayLength(&scene_objects);

    if (count == 0u)
    {
        return result;
    }

    for(var i : u32 = 0u; i < count; i = i + 1u)
    {
        let obj = scene_objects[i];

        let localPos = p - obj.position.xyz;

        var dist = 100000.0;

        switch(obj.obj_type)
        {
            case 0:
            {
                dist = sdfSphere(localPos, obj.size.x);
            }

            case 1:
            {
                dist = sdfBox(localPos, obj.size.xyz);
            }

            case 2:
            {
                dist = sdfCylinder(localPos, obj.size.x, obj.size.y);
            }

            case 3:
            {
                dist = sdfTorus(localPos, obj.size.x, obj.size.y);
            }

            default:
            {
                continue;
            }
        }

        if(i == 0u || obj.op_type == 0)
        {
            result.distance = dist;
            result.color = obj.color.xyz;
        }
        else if(obj.op_type == 1)
        {
            if(dist < result.distance)
            {
                result.color = obj.color.xyz;
            }

            result.distance = opUnion(result.distance, dist);
        }
        else if(obj.op_type == 2)
        {
            result.distance = opSubtract(dist, result.distance);
        }
        else if(obj.op_type == 3)
        {
            result.distance = opIntersection(result.distance, dist);
        }
    }

    return result;
}