// ==========================================
// BAYKAR ANA ÇÝZÝM VE RAYMARCHING EKRANI
// ==========================================

// 1. ÝÞTE O MEÞHUR KARGO KAPISI (STORAGE BUFFER)
// C++ tarafýndaki SceneManager'ýn yollayacaðý dizi tam olarak buraya düþecek!
@group(0) @binding(0) var<storage, read> scene_objects: array<SdfObject>;


// 2. KÖÞE (VERTEX) YAPILARI
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

// 3. VERTEX SHADER (Tam Ekran Tuval)
// C++'tan nokta göndermeye gerek kalmadan ekraný kaplayan kareyi kendisi çizer!
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Ekraný kaplayan 2 üçgenin 6 köþesi
    var pos = array<vec2f, 6>(
        vec2f(-1.0, -1.0), vec2f( 1.0, -1.0), vec2f( 1.0,  1.0),
        vec2f(-1.0, -1.0), vec2f( 1.0,  1.0), vec2f(-1.0,  1.0)
    );
    
    var out: VertexOutput;
    out.position = vec4f(pos[vertexIndex], 0.0, 1.0);
    out.uv = pos[vertexIndex];
    return out;
}

// 4. FRAGMENT SHADER (Asýl Sihrin Döndüðü Yer - Raymarching)
@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Ekran en-boy oraný (Basýk görünmemesi için)
    let ratio = 640.0 / 480.0;
    let uv = vec2f(in.uv.x * ratio, in.uv.y);

    // Kamera Ayarlarý (Biraz geriden bakýyoruz ki sahneyi görelim)
    let ro = vec3f(0.0, 0.0, -5.0); 
    let rd = normalize(vec3f(uv, 1.0));

    var t = 0.0;
    var hit = false;
    var final_color = vec3f(0.0);

    // Iþýn Yürütme (Raymarching) Döngüsü
    for (var i = 0; i < 100; i++) {
        let p = ro + rd * t;
        
        // ÖNEMLÝ: C++'tan gelen 'scene_objects' dizisini 
        // sdf_core.wgsl içindeki matematik motorumuza gönderiyoruz!
        let result = evaluateScene(p, scene_objects);

        if (result.distance < 0.001) {
            hit = true;
            final_color = result.color;
            break;
        }
        
        t += result.distance; // Güvenli adým at
        if (t > 50.0) { break; } // Boþluða düþtüysek çýk
    }

    if (hit) {
        // Iþýk/Gölge hesaplamalarýný þimdilik basit bir derinlik hissiyle yapýyoruz
        // Uzaktaki nesneler daha karanlýk, yakýndakiler daha parlak görünür
        let depth_shade = 1.0 - (t / 15.0);
        return vec4f(final_color * depth_shade, 1.0);
    }

    // Uzay boþluðu rengi (Koyu gri/siyah)
    return vec4f(0.05, 0.05, 0.05, 1.0);
}