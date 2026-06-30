@group(0) @binding(0) var<storage, read> scene_objects: array<SdfObject>;

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
    let ratio = 640.0 / 480.0;
    let uv = vec2f(in.uv.x * ratio, in.uv.y);

    let ro = vec3f(0.0, 0.0, -5.0); 
    let rd = normalize(vec3f(uv, 1.0));

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

    return vec4f(0.05, 0.05, 0.05, 1.0);
}