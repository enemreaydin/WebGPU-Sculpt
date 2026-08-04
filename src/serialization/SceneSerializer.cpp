#include "serialization/SceneSerializer.h"
#include "serialization/ExtensionLoader.h"
#include <iostream>

// Everything the GPU upload depends on, derived from the current objects + tree.
//
// Both Refresh* entry points below route through here, which is load-bearing:
// RefreshCsgBuffer can ALSO re-upload the object array (SdfRenderer's
// CreateSceneBuffer fallback when the program size changes), so derived per-object
// data living in only one entry point would ship stale on the other path.
CsgTree::CompiledProgram SceneSerializer::PrepareSceneForUpload()
{
    // Per-object bubble radii into position.w (the P1a proxy), then the whole-scene
    // box built from those same bubbles (the P4 ray clip). Radii first: they are
    // independent today, but this is the order that stays correct if the box is
    // ever changed to read position.w instead of recomputing.
    SceneBounds::WriteEnclosingRadii(sceneManager.GetObjects(), sceneManager.GetCsgTree());
    sceneBounds = SceneBounds::Compute(sceneManager.GetObjects(), sceneManager.GetCsgTree());

    // Per-subtree boxes, from the same objects and the same tree the program is
    // about to be compiled from. Recomputed here rather than cached because every
    // input to it (positions, sizes, tree shape, blend radii) can change on any
    // edit that reaches this function — and a stale box would regroup the program
    // around geometry that has moved.
    const NodeBoundsTable nodeBounds =
        SceneBounds::ComputeNodeBounds(sceneManager.GetObjects(), sceneManager.GetCsgTree());

    // objectId -> current array index, then the CSG tree compiled into the RPN
    // program the shader interprets. nodeBounds lets the compiler regroup wide flat
    // operation nodes into a spatial hierarchy (P1d) — see CsgTree::CompileToRPN.
    const auto objectIdMap = SceneManager::BuildObjectIdMap(sceneManager.GetObjects());
    CsgTree::CompiledProgram compiled = sceneManager.GetCsgTree().CompileToRPN(objectIdMap, &nodeBounds);

    // Size the shader's RPN stacks to what this program actually needs. Must happen
    // before the upload below, so the pipeline and the program it will interpret are
    // never a frame out of step. The renderer buckets, caches and warns; it is the
    // single owner of the shader-side depth (there is no C++ constant mirroring the
    // WGSL any more — the value is injected into the source at compile time).
    renderer.EnsureScenePipeline(graphics.device, compiled.requiredStackDepth);

    // Log on change only: this runs on every scene edit, including every frame of a
    // slider drag, so an unconditional print would bury the console.
    if (compiled.requiredStackDepth != csgStackDepth)
    {
        csgStackDepth = compiled.requiredStackDepth;
        std::cout << "[CSG] program " << compiled.commands.size()
                  << " cmds, required stack depth " << csgStackDepth
                  << ", shader variant " << renderer.GetActiveCsgStackDepth()
                  << ", skip nodes " << compiled.skipNodeCount
                  << " (" << (compiled.skipNodeCount * kCsgSkipHeaderCommands)
                  << " cmds overhead)\n";
    }

    return compiled;
}

void SceneSerializer::RefreshSceneBuffer()
{
    // Upload both the scene objects and the recompiled program.
    const CsgTree::CompiledProgram compiled = PrepareSceneForUpload();
    renderer.UpdateSceneBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects(),
        compiled.commands
    );
}

void SceneSerializer::RefreshCsgBuffer()
{
    // Upload the program only (unless the size changed — see the note above).
    const CsgTree::CompiledProgram compiled = PrepareSceneForUpload();
    renderer.UpdateCsgProgramBuffer(
        graphics.device,
        graphics.queue,
        sceneManager.GetObjects(),
        compiled.commands
    );
}

void SceneSerializer::NewScene()
{
    sceneManager.ClearScene();

    RefreshSceneBuffer();

    std::cout << "[INFO]: New empty scene created.\n";
}

void SceneSerializer::OpenSceneFromSDFO()
{
    std::filesystem::path path = projectRoot / "assets/test.sdfo";

    sceneManager.ClearScene();

    if (ExtensionLoader::LoadFromSDFO(path.string(), sceneManager)) {
        RefreshSceneBuffer();

        std::cout << "[INFO]: Scene loaded from: "
            << path << std::endl;
    }
    else {
        RefreshSceneBuffer();

        std::cerr << "[WARNING]: SDFO file could not be loaded: "
            << path << std::endl;
    }
}

void SceneSerializer::SaveSceneToSDFO()
{
    std::filesystem::path path = projectRoot / "assets/test.sdfo";

    if (ExtensionLoader::SaveToSDFO(path.string(), sceneManager.GetObjects(), sceneManager.GetCsgTree())) {
        std::cout << "[INFO]: Scene saved to: "
            << path << std::endl;
    }
    else {
        std::cerr << "[WARNING]: SDFO file could not be saved: "
            << path << std::endl;
    }
}
