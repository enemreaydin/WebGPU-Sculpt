#pragma once

#include "IEditorCommand.h"
#include "../../scene/data/ObjectTransformSnapshot.h"

#include <vector>

class SceneManager;

class TransformObjectsCommand : public IEditorCommand
{
public:
    TransformObjectsCommand(
        std::vector<ObjectTransformSnapshot> beforeSnapshots,
        std::vector<ObjectTransformSnapshot> afterSnapshots
    );

    void Execute(SceneManager& scene) override;
    void Undo(SceneManager& scene) override;

    const char* GetName() const override;

    bool IsValid() const;

private:
    std::vector<ObjectTransformSnapshot> m_BeforeSnapshots;
    std::vector<ObjectTransformSnapshot> m_AfterSnapshots;

    void ApplySnapshots(
        SceneManager& scene,
        const std::vector<ObjectTransformSnapshot>& snapshots
    );
};