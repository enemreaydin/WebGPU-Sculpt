cmake_minimum_required(VERSION 3.20)
project(sdf_editor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# DEPENDENCIES
# ============================================================

# The engine code targets the older WebGPU API (const char* labels,
# ShaderModuleWGSLDescriptor, SurfaceGetCurrentTextureStatus::Success, etc.),
# which is what the pinned Emscripten distribution provides. For the native
# build, select the matching older wgpu-native (v0.19.x, WGPU_STATIC) instead of
# the default v24 distribution whose API was rewritten and would not compile.
if (NOT EMSCRIPTEN)
    set(WEBGPU_BACKEND "WGPU_STATIC" CACHE STRING "WebGPU backend" FORCE)
endif()

include(deps/webgpu/webgpu.cmake)

# SDL3 is built from source for both native and Emscripten. On the web it
# creates the HTML <canvas> the WebGPU surface binds to.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL3_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(deps/SDL)

if (EXISTS "${CMAKE_SOURCE_DIR}/deps/glm/CMakeLists.txt")
    add_subdirectory(deps/glm)
endif()

# NOTE: ImGui / ImGuizmo have been removed. The editor UI now lives in the web
# front-end (see web/), which drives the engine through src/bindings.

# ============================================================
# PROJECT SOURCES
# ============================================================

# Editor layer: application + main render loop.
set(EDITOR_SOURCES
    src/editor/Application.cpp
    src/editor/FrameScheduler.cpp
    src/editor/FrameStats.cpp
    src/editor/EditorObjectStore.cpp
    src/editor/TransformTransaction.cpp
    src/editor/command/UndoStack.cpp
    src/editor/command/TransformObjectsCommand.cpp
    src/editor/command/SceneEditCommand.cpp
)

# Platform layer: window, graphics context, WebGPU wrapper implementation.
set(PLATFORM_SOURCES
    src/platform/Window.cpp
    src/platform/GraphicsContext.cpp
    src/platform/WebGPUImpl.cpp
)

# SDF engine: rendering, scene, camera, serialization, utilities.
set(ENGINE_SOURCES
    src/renderer/SdfRenderer.cpp
    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/scene/SceneManager.cpp
    src/scene/SdfObjectFactory.cpp
    src/scene/PrimitiveGeometry.cpp
    src/scene/csg/CsgTree.cpp
    src/interaction/GizmoMath.cpp
    src/serialization/ExtensionLoader.cpp
    src/serialization/SceneSerializer.cpp
    src/utils/FileHelper.cpp
)

# Web bindings: compiled into the WASM module only (guarded by __EMSCRIPTEN__).
set(BINDING_SOURCES
    src/bindings/WebBindings.cpp
)

# Listed so headers show up in IDE project views.
set(PROJECT_HEADERS
    src/core/Vec2.h
    src/core/ViewportInput.h
    src/platform/Window.h
    src/platform/GraphicsContext.h
    src/editor/Application.h
    src/editor/FrameScheduler.h
    src/editor/FrameStats.h
    src/editor/Settings.h
    src/editor/QualitySettings.h
    src/scene/data/SdfEnums.h
    src/scene/data/SdfObjectData.h
    src/scene/data/EditorObject.h
    src/scene/data/GlobalsData.h
    src/scene/data/ObjectTransformSnapshot.h
    src/scene/PrimitiveGeometry.h
    src/editor/EditorObjectStore.h
    src/scene/csg/CsgCommand.h
    src/scene/csg/CsgNode.h
    src/scene/csg/CsgTree.h
    src/renderer/SdfRenderer.h
    src/camera/Camera.h
    src/camera/CameraController.h
    src/camera/CameraTransition.h
    src/scene/SceneManager.h
    src/scene/SdfObjectFactory.h
    src/interaction/SelectionState.h
    src/interaction/GizmoState.h
    src/interaction/GizmoMath.h
    src/interaction/GizmoController.h
    src/serialization/ExtensionLoader.h
    src/serialization/SceneSerializer.h
    src/utils/FileHelper.h
    src/editor/command/IEditorCommand.h
    src/editor/command/UndoStack.h
    src/editor/command/TransformObjectsCommand.h
    src/editor/command/SceneEditCommand.h
    src/editor/SceneEditSnapshot.h
    src/editor/TransformTransaction.h
)

# ============================================================
# MAIN APPLICATION
# ============================================================

add_executable(main
    src/main.cpp
    ${EDITOR_SOURCES}
    ${PLATFORM_SOURCES}
    ${ENGINE_SOURCES}
    ${BINDING_SOURCES}
    ${PROJECT_HEADERS}
)

target_include_directories(main PRIVATE
    src
    deps/glm
    deps/SDL/include
)

target_link_libraries(main PRIVATE
    webgpu
    SDL3-static
    glm
)

if (TARGET glm::glm)
    target_link_libraries(main PRIVATE glm::glm)
endif()

# The static wgpu-native library (Rust) pulls in Windows system libraries for
# sockets, NT syscalls, crypto RNG and the WGL/OpenGL fallback path. These must
# be linked explicitly by the consumer.
if (NOT EMSCRIPTEN AND WIN32)
    target_link_libraries(main PRIVATE
        ws2_32 userenv bcrypt ntdll opengl32 d3dcompiler propsys runtimeobject oleaut32
    )
endif()

# ------------------------------------------------------------
# Emscripten (web) build
# ------------------------------------------------------------
if (EMSCRIPTEN)
    # Emit an ES/UMD module + .wasm + preloaded .data into web/public/engine so
    # the React app (web/) can load it.
    set_target_properties(main PROPERTIES
        OUTPUT_NAME "sdf-engine"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/public/engine"
    )

    target_link_options(main PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sASYNCIFY                # lets the Promise-based requestAdapter/requestDevice
                                  # be written as synchronous calls during init
        -sMODULARIZE=1
        -sEXPORT_NAME=createSdfModule
        -sENVIRONMENT=web
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
        # Bundle the shaders/assets into the virtual filesystem at /assets.
        "--preload-file" "${CMAKE_SOURCE_DIR}/assets@/assets"
    )
endif()

# ============================================================
# SDFO TEST FILE GENERATOR (native CLI tool only)
# ============================================================

if (NOT EMSCRIPTEN)
    add_executable(csg_features_test
        tests/CsgFeaturesTest.cpp
        src/scene/SceneManager.cpp
        src/scene/csg/CsgTree.cpp
        src/editor/command/UndoStack.cpp
    )
    target_include_directories(csg_features_test PRIVATE src)

    add_executable(create_test_sdfo
        src/serialization/ExtensionLoader.cpp
        src/scene/SceneManager.cpp
        src/scene/csg/CsgTree.cpp
    )

    target_include_directories(create_test_sdfo PRIVATE
        src
    )

    if (TARGET glm::glm)
        target_link_libraries(create_test_sdfo PRIVATE glm::glm)
    endif()
endif()

# ============================================================
# POST BUILD COPY (native only)
# ============================================================

if (NOT EMSCRIPTEN)
    # wgpu-native DLL lives next to the executable.
    target_copy_webgpu_binaries(main)

    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:main>/assets
        COMMENT "Copying assets next to executable"
    )
endif()
