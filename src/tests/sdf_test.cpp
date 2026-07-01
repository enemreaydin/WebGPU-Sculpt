#include <iostream>
#include "src/scene/SceneManager.h"
#include "src/io/ExtensionLoader.h"

int main() {
    std::cout << "=== BAYKAR 3D SDF MODELLER TESTI ===\n\n";

    // 1. Sahne yöneticimizi ayaða kaldýrýyoruz
    SceneManager myScene;

    // 2. Arayüzden kullanýcý "ucus_araci.bdf" yüklediðini farz edelim
    ExtensionLoader::LoadMockFile("ucus_araci.bdf", myScene);

    // 3. Veriler gerçekten SceneManager'a ulaþtý mý? Test Edelim:
    std::cout << "\n[TEST SONUCU]:\n";
    std::cout << "Sahnede toplam " << myScene.GetObjectCount() << " adet nesne var.\n";

    const auto& objects = myScene.GetObjects();
    for (size_t i = 0; i < objects.size(); i++) {
        std::cout << "-> Nesne ID [" << i << "] | Tip: " << (int)objects[i].type
            << " | Islem: " << (int)objects[i].op
            << " | Konum X: " << objects[i].position[0] << "\n";
    }

    return 0;
}