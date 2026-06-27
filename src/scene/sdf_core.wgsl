// ==========================================
// BAYKAR MODÜLER 3D SDF ÇEKÝRDEK KÜTÜPHANESÝ
// ==========================================

// 1. C++ TARAFINDAKÝ SdfObjectData YAPISININ WGSL KARÞILIÐI
struct SdfObject {
    obj_type: i32,       // 0: Sphere, 1: Box, 2: Cylinder
    op_type: i32,        // 0: None, 1: Union, 2: Subtract, 3: Intersection
    position: vec3f,     // Nesnenin Merkez Konumu (X, Y, Z)
    size: vec3f,         // Boyut Verileri (Yarýçap veya Geniþlikler)
    color: vec3f,        // Nesne Rengi (R, G, B)
}

// 2. SAF MATEMATÝKSEL ÞEKÝL ÇEKMECESÝ
// Gönderdiðin kaynak sitedeki ham formüllerin modüler halleri

// Saf Küre Formülü
fn sdfSphere(p: vec3f, radius: f32) -> f32 {
    return length(p) - radius;
}

// Saf Kutu (Box) Formülü
fn sdfBox(p: vec3f, b: vec3f) -> f32 {
    let q = abs(p) - b;
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}


// 3. BLENDER TARZI KATI GEOMETRÝ (CSG) OPERASYONLARI

// Birleþtirme: Ýki nesneyi birbirine eriterek baðlar
fn opUnion(d1: f32, d2: f32) -> f32 {
    return min(d1, d2);
}

// Oyma / Çýkartma: Ýlk nesneyi kullanarak ikincinin içini oyar
fn opSubtract(d1: f32, d2: f32) -> f32 {
    return max(-d1, d2);
}

// Kesiþim: Sadece iki nesnenin üst üste bindiði ortak alaný býrakýr
fn opIntersection(d1: f32, d2: f32) -> f32 {
    return max(d1, d2);
}


// 4. ASIL KÜTÜPHANE MOTORU: SAHNE DEÐERLENDÝRÝCÝ (EVALUATOR)
// C++'tan veya Extension'dan gelen nesne listesini sýrayla okuyup
// Blender mantýðýyla nihai 3D þekli ve onun rengini hesaplar.
struct SceneResult {
    distance: f32,
    color: vec3f,
}

fn evaluateScene(p: vec3f, objects: array<SdfObject, 4>) -> SceneResult {
    var result: SceneResult;
    result.distance = 10000.0; // Baþlangýçta sonsuz boþluk mesafesi
    result.color = vec3f(0.0);

    // Sahnede kayýtlý olan tüm nesneleri (Extension verilerini) sýrayla dönüyoruz
    for (var i = 0; i < 4; i = i + 1) {
        let obj = objects[i];
        
        // Eðer boþ/tanýmsýz bir nesne yuvasýysa es geç
        if (obj.obj_type < 0) { continue; }

        // Noktayý nesnenin kendi yerel konumuna göre kaydýrýyoruz (Dönüþüm)
        let local_p = p - obj.position;
        
        // Þeklin türüne göre ham mesafeyi hesapla
        var current_dist = 10000.0;
        if (obj.obj_type == 0) { 
            current_dist = sdfSphere(local_p, obj.size.x); // Küre için size.x yarýçaptýr
        } else if (obj.obj_type == 1) { 
            current_dist = sdfBox(local_p, obj.size); 
        }

        // Blender Operasyonu: Bu nesne kendinden önceki katmanlarla nasýl birleþecek?
        if (i == 0 || obj.op_type == 0) {
            // Ýlk nesne ise direkt taban kabul et
            result.distance = current_dist;
            result.color = obj.color;
        } else if (obj.op_type == 1) {
            // Birleþtirme (Union) yapýlýyorsa mesafeyi erit, rengi harmanla
            if (current_dist < result.distance) {
                result.color = obj.color;
            }
            result.distance = opUnion(result.distance, current_dist);
        } else if (obj.op_type == 2) {
            // Oyma (Subtract) yapýlýyorsa içini çýkar
            result.distance = opSubtract(current_dist, result.distance);
            // Oyulan yüzeyde oyan nesnenin rengi belirsin
            if (current_dist > -result.distance) {
                result.color = obj.color;
            }
        } else if (obj.op_type == 3) {
            // Kesiþim (Intersection)
            result.distance = opIntersection(result.distance, current_dist);
            result.color = (result.color + obj.color) * 0.5;
        }
    }

    return result;
}