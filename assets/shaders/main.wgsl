@group(0) @binding(0) var<storage, read> scene_objects: array<SdfObject>;

struct Globals {
    resolution : vec2f,
    showGrid : i32,
    selectedIndex : i32,
    cameraPos : vec4f,
    cameraTarget : vec4f,
}

@group(0) @binding(1) var<uniform> globals : Globals;

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

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
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
    var final_color = vec3f(0.0);

    for (var i = 0; i < 100; i++) {
        let p = ro + rd * t;
        let result = evaluateScene(p);

        if (result.distance < 0.001) {
            hit = true;
            final_color = result.color;
            break;
        }

        t += result.distance;
        if (t > 50.0) { break; }
    }

    if (hit) {
        let depth_shade = 1.0 - (t / 15.0);
        return vec4f(final_color * depth_shade, 1.0);
    }

    if (globals.showGrid == 1) {
        return vec4f(gridBackground(ro, rd), 1.0);
    }

    return vec4f(0.02, 0.02, 0.02, 1.0);
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