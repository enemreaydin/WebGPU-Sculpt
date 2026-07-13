#include "TransformObjectsCommand.h"

#include "../../scene/SceneManager.h"

#include <utility>

namespace
{
    bool SnapshotsChanged(
        const std::vector<ObjectTransformSnapshot>& before,
        const std::vector<ObjectTransformSnapshot>& after
    )
    {
        if (before.size() != after.size())
            return true;

        for (size_t i = 0; i < before.size(); ++i)
        {
            if (!SameTransformSnapshot(before[i], after[i]))
                return true;
        }

        return false;
    }
}

TransformObjectsCommand::TransformObjectsCommand(
    std::vector<ObjectTransformSnapshot> beforeSnapshots,
    std::vector<ObjectTransformSnapshot> afterSnapshots
)
    : m_BeforeSnapshots(std::move(beforeSnapshots)),
    m_AfterSnapshots(std::move(afterSnapshots))
{
}

void TransformObjectsCommand::Execute(SceneManager& scene)
{
    ApplySnapshots(scene, m_AfterSnapshots);
}

void TransformObjectsCommand::Undo(SceneManager& scene)
{
    ApplySnapshots(scene, m_BeforeSnapshots);
}

const char* TransformObjectsCommand::GetName() const
{
    return "Transform Objects";
}

bool TransformObjectsCommand::IsValid() const
{
    if (m_BeforeSnapshots.empty())
        return false;

    if (m_AfterSnapshots.empty())
        return false;

    return SnapshotsChanged(m_BeforeSnapshots, m_AfterSnapshots);
}

void TransformObjectsCommand::ApplySnapshots(
    SceneManager& scene,
    const std::vector<ObjectTransformSnapshot>& snapshots
)
{
    for (const auto& snapshot : snapshots)
    {
        scene.SetObjectTransformById(snapshot);
    }
}