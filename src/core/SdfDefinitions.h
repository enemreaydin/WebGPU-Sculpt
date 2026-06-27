#pragma once
#include <array>

// 1. KÜTÜPHANEMÝZDEKÝ GEOMETRÝK ÞEKÝLLER (ID Sistemi)
enum class SdfObjectType : int {
    Sphere = 0, // Küre
    Box = 1,    // Kutu
    Cylinder = 2 // Silindir
};

// 2. BLENDER TARZI OPERASYONLAR
enum class SdfOperationType : int {
    None = 0,        // Ýlk nesne ise etkileþim yok, taban kabul et
    Union = 1,       // Birleþtir (Eriterek ekle)
    Subtract = 2,    // Oy (Ýçinden çýkart)
    Intersection = 3 // Kesiþim (Sadece ortak alaný býrak)
};

// 3. GPU'YA VE EXTENSION SÝSTEMÝNE AKACAK OLAN STANDART KUTU (Struct)
struct SdfObjectData {
    SdfObjectType type;            // Þekil Türü (Küre, Kutu vb.)
    SdfOperationType op;           // Ýþlem Türü (Oyma, Birleþtirme vb.)
    std::array<float, 3> position; // Sahnedeki Konumu (X, Y, Z)
    std::array<float, 3> size;     // Boyutlarý (Yarýçap veya Kenar Uzunluklarý)
    std::array<float, 3> color;    // Kendine has rengi (R, G, B)
    float _pad;                    // WebGPU donanýmýnýn 16-byte hizalama kuralý için dolgu
};