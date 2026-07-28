@group(0) @binding(0) var<storage, read> scene_objects: array<SdfObject>;

struct Globals {
    resolution : vec2f,
    showGrid : i32,
    selectedIndex : i32,
    cameraPos : vec4f,
    cameraTarget : vec4f,
    gizmoOrigin : vec4f,
    gizmoAxisX : vec4f,
    gizmoAxisY : vec4f,
    gizmoAxisZ : vec4f,
    gizmoVisible : i32,
    gizmoHovered : i32,
    gizmoActive : i32,
    gizmoMode : i32,
    lightDirection : vec4f,
    lightColor : vec4f,
    ambientColor : vec4f,
    lightIntensity : f32,
    specularShininess : f32,
    specularStrength : f32,
    shadowSoftness : f32,
    aoStrength : f32,
    maxSteps : i32,
    hitEpsilon : f32,
    maxDistance : f32,
    shadowMaxSteps : i32,
    shadowMaxDistance : f32,
    aoSamples : i32,
    aaSamples : i32,
    shadowsEnabled : i32,
    aoEnabled : i32,
    qualityPad1 : i32,
    gizmoPixelScale : f32,
    jitterOffset : vec2f,
    taaEnabled : i32,
    accumCount : i32
}

@group(0) @binding(1) var<uniform> globals : Globals;

@group(0) @binding(2) var<storage, read> csg_program: array<CsgCommand>;

@group(0) @binding(3) var accumTex : texture_2d_array<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var pos = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f( 1.0, -1.0), vec2f( 1.0,  1.0),
        vec2f(-1.0, -1.0), vec2f( 1.0,  1.0), vec2f(-1.0,  1.0)
    );

    var out: VertexOutput;
    out.position = vec4f(pos[vertexIndex], 0.0, 1.0);
    out.uv = pos[vertexIndex];
    return out;
}

struct FragmentOutput
{
    @location(0) color : vec4f,
    @location(1) object_id : u32,
}

fn toneMapAndEncode(linearColor : vec3f) -> vec3f {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    let mapped = clamp(
        (linearColor * (a * linearColor + b)) / (linearColor * (c * linearColor + d) + e),
        vec3f(0.0),
        vec3f(1.0)
    );
    return pow(mapped, vec3f(1.0 / 2.2));
}

struct RayShade {
    color : vec3f,
    id : u32,
}

fn shadeRay(uv : vec2f, ro : vec3f, right : vec3f, up : vec3f, forward : vec3f, focalLength : f32) -> RayShade {
    let lightDirection = normalize(globals.lightDirection.xyz);
    let lightColor = globals.lightColor.xyz;
    let intensity = globals.lightIntensity;
    let ambient = globals.ambientColor.xyz;

    let rd = normalize(right * uv.x + up * uv.y + forward * focalLength);

    var t = 0.0;
    var hit = false;

    for (var i = 0; i < globals.maxSteps; i++) {
        let p = ro + rd * t;
        let nearestObjectDistance = distanceCalc(p);

        if (nearestObjectDistance < globals.hitEpsilon) {
            hit = true;
            break;
        }
        t += nearestObjectDistance;
        if (t > globals.maxDistance) { break; }
    }

    var col : vec3f;
    var id : u32 = 0xffffffffu;

    if (hit) {
        let hitPos = ro + rd * t;
        let n = calcNormal(hitPos);
        let result = evaluateScene(hitPos);
        let diff = max(dot(n, lightDirection), 0.0);
        let v = normalize(ro - hitPos);
        let h = normalize(lightDirection + v);
        let shininess = globals.specularShininess;
        let specularStrength = globals.specularStrength;
        let spec = pow(max(dot(n, h), 0.0), shininess);
        var shadow = 1.0;
        if (globals.shadowsEnabled == 1 && intensity > 0.0) { shadow = calcSoftShadow(hitPos, lightDirection); }
        var ao = 1.0;
        if (globals.aoEnabled == 1) { ao = calcAO(hitPos, n); }
        let diffuseTerm = result.color * lightColor * intensity * diff;
        let specularTerm = lightColor * intensity * spec * specularStrength;
        col = result.color * ambient * ao + shadow * (diffuseTerm + specularTerm);
        id = result.object_id;
    } else if (globals.showGrid == 1) {
        col = vec3f(0.05, 0.05, 0.05);
    } else {
        col = vec3f(0.02, 0.02, 0.02);
    }

    if (globals.showGrid == 1) {
        let g = gridSample(ro, rd);
        let inFront = g.tGrid >= 0.0 && (!hit || g.tGrid < t);
        if (inFront && g.intensity > 0.0) {
            col = mix(col, vec3f(0.3), g.intensity);
        }
    }

    var r : RayShade;
    r.color = col;
    r.id = id;
    return r;
}

@fragment
fn fs_main(in: VertexOutput) -> FragmentOutput {
    let ratio = globals.resolution.x / globals.resolution.y;
    var uv = vec2f(in.uv.x * ratio, in.uv.y);
    uv = uv + globals.jitterOffset * (2.0 / globals.resolution.y);


    let ro = globals.cameraPos.xyz;
    let targetPoint = globals.cameraTarget.xyz;
    let worldUp = vec3f(0.0, 1.0, 0.0);

    let forward = normalize(targetPoint - ro);
    let right = normalize(cross(forward, worldUp));
    let up = cross(right, forward);

    let focalLength = 2.414;

    let n = clamp(globals.aaSamples, 1, 8);

    let center = shadeRay(uv, ro, right, up, forward, focalLength);
    let id = center.id;

    var col : vec3f;
    if (n <= 1) {
        col = center.color;
    } else {
        let uvPerPx = 2.0 / globals.resolution.y;
        let inv = 1.0 / f32(n);
        var accum = vec3f(0.0);
        for (var sy = 0; sy < n; sy++) {
            for (var sx = 0; sx < n; sx++) {
                let off = (vec2f(f32(sx), f32(sy)) + 0.5) * inv - 0.5;
                let uvS = uv + off * uvPerPx;
                accum += shadeRay(uvS, ro, right, up, forward, focalLength).color;
            }
        }
        col = accum * inv * inv;
    }

    col = toneMapAndEncode(col);

    var out : FragmentOutput;
    out.color = vec4f(col, 1.0);
    out.object_id = id;
    return out;
}

@fragment
fn fs_resolve(in: VertexOutput) -> @location(0) vec4f {
    let coord = vec2i(in.position.xy);

    var c = vec3f(0.0);
    let n = clamp(globals.accumCount, 1, 4);
    for (var i = 0; i < n; i++) {
        c = c + textureLoad(accumTex, coord, i, 0).rgb;
    }
    c = c / f32(n);

    let ratio = globals.resolution.x / globals.resolution.y;
    let uv = vec2f(in.uv.x * ratio, in.uv.y);

    let ro = globals.cameraPos.xyz;
    let targetPoint = globals.cameraTarget.xyz;
    let worldUp = vec3f(0.0, 1.0, 0.0);

    let forward = normalize(targetPoint - ro);
    let right = normalize(cross(forward, worldUp));
    let up = cross(right, forward);

    let focalLength = 2.414;
    let rd = normalize(right * uv.x + up * uv.y + forward * focalLength);

    c = overlayGizmo(c, uv, ro, rd, right, up, forward, focalLength);

    return vec4f(c, 1.0);
}


fn gizmoAxisColor(axis : i32) -> vec3f {
    if (axis == 1) { return vec3f(0.85, 0.31, 0.29); }
    if (axis == 2) { return vec3f(0.50, 0.68, 0.23); }
    return vec3f(0.29, 0.52, 0.84);
}

fn gizmoProject(world : vec3f, ro : vec3f, right : vec3f, up : vec3f, forward : vec3f, focal : f32) -> vec3f {
    let rel = world - ro;
    let zc = dot(rel, forward);
    if (zc <= 0.0001) { return vec3f(0.0, 0.0, -1.0); }
    let xc = dot(rel, right);
    let yc = dot(rel, up);
    return vec3f(xc * focal / zc, yc * focal / zc, 1.0);
}

fn gizmoSegDist(p : vec2f, a : vec2f, b : vec2f) -> f32 {
    let ab = b - a;
    let l2 = dot(ab, ab);
    if (l2 < 1e-8) { return length(p - a); }
    let s = clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    return length(p - (a + ab * s));
}

fn gizmoTint(axisId : i32) -> vec3f {
    let hot = (globals.gizmoActive == axisId) || (globals.gizmoActive == 0 && globals.gizmoHovered == axisId);
    if (hot) { return vec3f(0.96, 0.77, 0.26); }
    return gizmoAxisColor(axisId);
}

fn overlayTranslateGizmo(baseCol : vec3f, uv : vec2f, ro : vec3f, right : vec3f, up : vec3f, forward : vec3f, focal : f32) -> vec3f {
    let o = gizmoProject(globals.gizmoOrigin.xyz, ro, right, up, forward, focal);
    if (o.z < 0.0) { return baseCol; }

    let uvPerPx = 2.0 * globals.gizmoPixelScale / globals.resolution.y;
    let lineHalf = 2.5 * uvPerPx;
    let dotR = 5.0 * uvPerPx;

    var col = baseCol;

    var ends = array<vec3f, 3>(globals.gizmoAxisX.xyz, globals.gizmoAxisY.xyz, globals.gizmoAxisZ.xyz);
    for (var a = 0; a < 3; a++) {
        let e = gizmoProject(ends[a], ro, right, up, forward, focal);
        if (e.z < 0.0) { continue; }
        let d = gizmoSegDist(uv, o.xy, e.xy);
        if (d < lineHalf) {
            let aa = 1.0 - smoothstep(lineHalf - uvPerPx, lineHalf, d);
            col = mix(col, gizmoTint(a + 1), aa);
        }
    }

    let dO = length(uv - o.xy);
    if (dO < dotR) {
        let aa = 1.0 - smoothstep(dotR - uvPerPx, dotR, dO);
        col = mix(col, vec3f(0.92), aa);
    }

    return col;
}

fn overlayRotateGizmo(baseCol : vec3f, ro : vec3f, rd : vec3f, forward : vec3f, focal : f32) -> vec3f {
    let o = globals.gizmoOrigin.xyz;
    let uvPerPx = 2.0 * globals.gizmoPixelScale / globals.resolution.y;

    var col = baseCol;

    var ends = array<vec3f, 3>(globals.gizmoAxisX.xyz, globals.gizmoAxisY.xyz, globals.gizmoAxisZ.xyz);
    for (var a = 0; a < 3; a++) {
        let spoke = ends[a] - o;
        let radius = length(spoke);
        if (radius < 0.00001) { continue; }
        let n = spoke / radius;

        let denom = dot(rd, n);
        if (abs(denom) < 0.0005) { continue; }

        let t = dot(o - ro, n) / denom;
        if (t <= 0.0) { continue; }

        let p = ro + rd * t;

        if (dot(p - o, forward) > 0.02 * radius) { continue; }

        let zc = dot(p - ro, forward);
        if (zc <= 0.0001) { continue; }

        let worldPerUv = zc / focal;
        let halfW = 2.5 * uvPerPx * worldPerUv;
        let feather = uvPerPx * worldPerUv;

        let radial = abs(length(p - o) - radius);
        if (radial > halfW) { continue; }

        let aa = 1.0 - smoothstep(halfW - feather, halfW, radial);
        col = mix(col, gizmoTint(a + 1), aa);
    }

    return col;
}

fn overlayGizmo(baseCol : vec3f, uv : vec2f, ro : vec3f, rd : vec3f, right : vec3f, up : vec3f, forward : vec3f, focal : f32) -> vec3f {
    if (globals.gizmoVisible != 1) { return baseCol; }

    if (globals.gizmoMode == 1) {
        return overlayRotateGizmo(baseCol, ro, rd, forward, focal);
    }

    return overlayTranslateGizmo(baseCol, uv, ro, right, up, forward, focal);
}


struct GridSample {
    tGrid : f32,
    intensity : f32,
}

fn gridSample(ro: vec3f, rd: vec3f) -> GridSample {
    var s : GridSample;
    s.tGrid = -1.0;
    s.intensity = 0.0;

    if (abs(rd.y) < 0.0001) {
        return s;
    }

    let t = -ro.y / rd.y;

    if (t < 0.0) {
        return s;
    }

    let p = ro + rd * t;
    let gridSize = 1.0;
    let lineWidth = 0.02;

    let gx = abs(fract(p.x / gridSize) - 0.5);
    let gz = abs(fract(p.z / gridSize) - 0.5);
    let lineMask = 1.0 - smoothstep(0.0, lineWidth, min(gx, gz));

    let fade = 1.0 / (1.0 + t * t * 0.01);

    s.tGrid = t;
    s.intensity = lineMask * fade;
    return s;
}