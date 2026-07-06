struct SdfObject
{
    obj_type : i32,
    object_id : i32,

    parent_id : i32,
    pad0 : i32,

    position : vec4f,
    rotation : vec4f,
    size : vec4f,
    color : vec4f,
};

struct SceneResult
{
    distance : f32,
    color : vec3f,
};

// GPU-facing RPN command — mirrors the CPU-side CsgCommand (16 bytes).
struct CsgCommand
{
    cmd_type : i32,
    operand : i32,
    node_id : i32,
    pad0 : i32,
}

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

    let count = arrayLength(&csg_program);
    if (count == 0u)
    {
        return result;
    }

    // Fixed-size stacks for the RPN evaluator (WGSL has no recursion). One stack for
    // distances and a parallel one for colors, sharing the stack pointer sp.
    var stack : array<f32, 16>;
    var stackColor : array<vec3f, 16>;
    var sp : i32 = 0;

    for (var i : u32 = 0u; i < count; i = i + 1u)
    {
        let cmd = csg_program[i];

        if (cmd.cmd_type == 0)
        {
            // PUSH_OBJECT: evaluate the primitive and push its distance/color.
            let obj = scene_objects[cmd.operand];
            let localPos = p - obj.position.xyz;
            var dist = 100000.0;
            switch(obj.obj_type)
            {
                case 0: { dist = sdfSphere(localPos, obj.size.x); }
                case 1: { dist = sdfBox(localPos, obj.size.xyz); }
                case 2: { dist = sdfCylinder(localPos, obj.size.x, obj.size.y); }
                case 3: { dist = sdfTorus(localPos, obj.size.x, obj.size.y); }
                default: { dist = 100000.0; }
            }

            var objColor = obj.color.xyz;
            if (cmd.operand == globals.selectedIndex)
            {
                objColor = mix(objColor, vec3f(1.0), 0.4);
            }

            stack[sp] = dist;
            stackColor[sp] = objColor;
            sp = sp + 1;
        }
        else if (cmd.cmd_type == 1 || cmd.cmd_type == 2 || cmd.cmd_type == 3)
        {
            // Binary op: pop the top two entries (A = accumulator below, B = top),
            // combine, and push the single result back. Net stack effect is -1.
            let dB = stack[sp - 1];
            let cB = stackColor[sp - 1];
            let dA = stack[sp - 2];
            let cA = stackColor[sp - 2];

            var d = 100000.0;
            var c = cA;

            if (cmd.cmd_type == 1)
            {
                // Union: closer surface wins the color (matches old behavior).
                d = opUnion(dA, dB);
                if (dB < dA)
                {
                    c = cB;
                }
            }
            else if (cmd.cmd_type == 2)
            {
                // Subtract: opSubtract(dist, result) order; accumulator keeps its color.
                d = opSubtract(dB, dA);
                c = cA;
            }
            else
            {
                // Intersection: opIntersection(result, dist); accumulator keeps its color.
                d = opIntersection(dA, dB);
                c = cA;
            }

            sp = sp - 1;
            stack[sp - 1] = d;
            stackColor[sp - 1] = c;
        }
    }

    if (sp > 0)
    {
        result.distance = stack[sp - 1];
        result.color = stackColor[sp - 1];
    }

    return result;
}