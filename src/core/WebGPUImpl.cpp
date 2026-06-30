// Bu dosya webgpu.hpp wrapper'inin GERCEK implementasyonunu uretir.
// Tum projede WEBGPU_CPP_IMPLEMENTATION define'i SADECE burada olmali -
// baska bir .cpp dosyasinda tekrar tanimlanirsa "LNK2005 duplicate symbol" hatasi alinir.
// Bu dosya CMakeLists.txt'teki SDF_CORE_SOURCES listesine eklenerek hem
// main hem sdf_test_run hedeflerine otomatik paylastiriliyor.
#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
