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
    object_id: u32,
};

struct CsgCommand
{
    cmd_type : i32,
    operand : i32,
    node_id : i32,
    pad0 : i32,
}

fn rotateInverse(v: vec3f, q: vec4f) -> vec3f
{
    let u = -q.xyz;
    let w = q.w;
    return v + 2*cross(u, cross(u, v) + w*v);
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

fn distanceCalc(p : vec3f) -> f32
{
    var resultDistance = 100000.0;
    let count = arrayLength(&csg_program);
    if (count == 0u)
    {
        return resultDistance;
    }
    var distanceStack : array<f32, 16>;
    var sp : i32 = 0;

    for (var i : u32 = 0u; i < count; i = i + 1u)
    {
        let cmd = csg_program[i];

        if (cmd.cmd_type == 0)
        {
            let obj = scene_objects[cmd.operand];
            let localPos = rotateInverse(p - obj.position.xyz, obj.rotation);
            var dist = 100000.0;
            switch(obj.obj_type)
            {
                case 0: { dist = sdfSphere(localPos, obj.size.x); }
                case 1: { dist = sdfBox(localPos, obj.size.xyz); }
                case 2: { dist = sdfCylinder(localPos, obj.size.x, obj.size.y); }
                case 3: { dist = sdfTorus(localPos, obj.size.x, obj.size.y); }
                default: { dist = 100000.0; }
            }
            distanceStack[sp] = dist;
            sp = sp + 1;
        }
        else if (cmd.cmd_type == 1 || cmd.cmd_type == 2 || cmd.cmd_type == 3)
        {
            let dB = distanceStack[sp - 1];
            let dA = distanceStack[sp - 2];
            var d = 100000.0;
            if (cmd.cmd_type == 1)
            {
                d = opUnion(dA, dB);
            }
            else if (cmd.cmd_type == 2)
            {
                d = opSubtract(dB, dA);
            }
            else
            {
                d = opIntersection(dA, dB);
            }

            sp = sp - 1;
            distanceStack[sp - 1] = d;
        }
    }
     if (sp > 0)
    {
        resultDistance = distanceStack[sp - 1];
    }
    return resultDistance;
}

fn calcNormal(p: vec3f) -> vec3f
{
    let h = 0.0005;
    let k0 = vec3f(1.0, -1.0, -1.0);
    let k1 = vec3f(-1.0, -1.0, 1.0);
    let k2 = vec3f(-1.0, 1.0, -1.0);
    let k3 = vec3f(1.0, 1.0, 1.0);
    let sumVectorDists = k0 * distanceCalc(p + k0*h)
                       + k1 * distanceCalc(p + k1*h)
                       + k2 * distanceCalc(p + k2*h)
                       + k3 * distanceCalc(p + k3*h);
    return normalize(sumVectorDists);
}

fn calcSoftShadow(origin: vec3f, lightDir: vec3f) -> f32
{
    let tmin = 0.03;
    let tmax = globals.shadowMaxDistance;
    let k = globals.shadowSoftness;
    let eps = 0.001;

    var res = 1.0;
    var t = tmin;
    for (var i = 0; i < globals.shadowMaxSteps; i++)
    {
        let h = distanceCalc(origin + lightDir * t);
        if (h < eps)
        {
            return 0.0;
        }
        res = min(res, k * h / t);
        t += h;
        if (t > tmax)
        {
            break;
        }
    }
    return clamp(res, 0.0, 1.0);
}

fn calcAO(hitPos: vec3f, n: vec3f) -> f32
{
    var occ = 0.0;
    var sca = 1.0;
    let span = f32(max(globals.aoSamples - 1, 1));
    for (var i = 0; i < globals.aoSamples; i++)
    {
        let h = 0.01 + 0.12 * f32(i) / span;
        let d = distanceCalc(hitPos + n * h);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - globals.aoStrength * occ, 0.0, 1.0);
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

    var stack : array<f32, 16>;
    var stackColor : array<vec3f, 16>;
    var stackId : array<u32, 16>;
    var sp : i32 = 0;

    for (var i : u32 = 0u; i < count; i = i + 1u)
    {
        let cmd = csg_program[i];

        if (cmd.cmd_type == 0)
        {
            let obj = scene_objects[cmd.operand];
            let localPos = rotateInverse(p - obj.position.xyz, obj.rotation);
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
            stackId[sp] = u32(cmd.operand);
            sp = sp + 1;
        }
        else if (cmd.cmd_type == 1 || cmd.cmd_type == 2 || cmd.cmd_type == 3)
        {
            let dB = stack[sp - 1];
            let cB = stackColor[sp - 1];
            let idB = stackId[sp - 1];
            let dA = stack[sp - 2];
            let cA = stackColor[sp - 2];
            let idA = stackId[sp - 2];

            var d = 100000.0;
            var c = cA;
            var id = idA;

            if (cmd.cmd_type == 1)
            {
                d = opUnion(dA, dB);
                if (dB < dA)
                {
                    c = cB;
                    id = idB;
                }
            }
            else if (cmd.cmd_type == 2)
            {
                d = opSubtract(dB, dA);
                c = cA;
            }
            else
            {
                d = opIntersection(dA, dB);
                c = cA;
            }

            sp = sp - 1;
            stack[sp - 1] = d;
            stackColor[sp - 1] = c;
            stackId[sp - 1] = id;
        }
    }

    if (sp > 0)
    {
        result.distance = stack[sp - 1];
        result.color = stackColor[sp - 1];
        result.object_id = stackId[sp - 1];
    }else
    {
    result.object_id = 0xffffffffu;
    }

    return result;
}