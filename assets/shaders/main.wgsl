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
    gizmoPad : i32,
}

@group(0) @binding(1) var<uniform> globals : Globals;

@group(0) @binding(2) var<storage, read> csg_program: array<CsgCommand>;

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
    let mapped = linearColor / (linearColor + vec3f(1.0));
    return pow(mapped, vec3f(1.0 / 2.2));
}

@fragment
fn fs_main(in: VertexOutput) -> FragmentOutput {
    let lightDirection = normalize(vec3f(0.5, 0.8, 0.6));
    let lightColor = vec3f(1.0);
    let intensity = 1.0;
    let ambient = vec3f(0.12);

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

    var t = 0.0;
    var hit = false;

    for (var i = 0; i < 100; i++) {
        let p = ro + rd * t;
        let nearestObjectDistance = distanceCalc(p);

        if (nearestObjectDistance < 0.001) {
            hit = true;
            break;
        }
        t += nearestObjectDistance;
        if (t > 50.0) { break; }
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
        let shininess = 48.0;
        let specularStrength = 0.4;
        let spec = pow(max(dot(n, h), 0.0), shininess);
        col = result.color * (ambient + lightColor * intensity * diff) + lightColor * intensity * spec * specularStrength;
        id = result.object_id;
    } else if (globals.showGrid == 1) {
        col = gridBackground(ro, rd);
    } else {
        col = vec3f(0.02, 0.02, 0.02);
    }

    col = toneMapAndEncode(col);

    col = overlayGizmo(col, uv, ro, right, up, forward, focalLength);

    var out : FragmentOutput;
    out.color = vec4f(col, 1.0);
    out.object_id = id;
    return out;
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

fn overlayGizmo(baseCol : vec3f, uv : vec2f, ro : vec3f, right : vec3f, up : vec3f, forward : vec3f, focal : f32) -> vec3f {
    if (globals.gizmoVisible != 1) { return baseCol; }

    let o = gizmoProject(globals.gizmoOrigin.xyz, ro, right, up, forward, focal);
    if (o.z < 0.0) { return baseCol; }

    let uvPerPx = 2.0 / globals.resolution.y;
    let lineHalf = 2.5 * uvPerPx;
    let dotR = 5.0 * uvPerPx;

    var col = baseCol;

    var ends = array<vec3f, 3>(globals.gizmoAxisX.xyz, globals.gizmoAxisY.xyz, globals.gizmoAxisZ.xyz);
    for (var a = 0; a < 3; a++) {
        let e = gizmoProject(ends[a], ro, right, up, forward, focal);
        if (e.z < 0.0) { continue; }
        let d = gizmoSegDist(uv, o.xy, e.xy);
        if (d < lineHalf) {
            let axisId = a + 1;
            let hot = (globals.gizmoActive == axisId) || (globals.gizmoActive == 0 && globals.gizmoHovered == axisId);
            var c = gizmoAxisColor(axisId);
            if (hot) { c = vec3f(0.96, 0.77, 0.26); }
            let aa = 1.0 - smoothstep(lineHalf - uvPerPx, lineHalf, d);
            col = mix(col, c, aa);
        }
    }

    let dO = length(uv - o.xy);
    if (dO < dotR) {
        let aa = 1.0 - smoothstep(dotR - uvPerPx, dotR, dO);
        col = mix(col, vec3f(0.92), aa);
    }

    return col;
}


fn gridBackground(ro: vec3f, rd: vec3f) -> vec3f {
    let bgColor = vec3f(0.05, 0.05, 0.05);

    if (abs(rd.y) < 0.0001) {
        return bgColor;
    }

    let t = -ro.y / rd.y;

    if (t < 0.0) {
        return bgColor;
    }

    let p = ro + rd * t;
    let gridSize = 1.0;
    let lineWidth = 0.02;

    let gx = abs(fract(p.x / gridSize) - 0.5);
    let gz = abs(fract(p.z / gridSize) - 0.5);
    let lineMask = 1.0 - smoothstep(0.0, lineWidth, min(gx, gz));

    let fade = 1.0 / (1.0 + t * t * 0.01);
    let gridColor = vec3f(0.3) * lineMask * fade;

    return bgColor + gridColor;
}