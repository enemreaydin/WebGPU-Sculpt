#include "TransformTransaction.h"

#include "editor/command/TransformObjectsCommand.h"
#include "editor/command/UndoStack.h"
#include "scene/SceneManager.h"

#include <memory>
#include <utility>

void TransformTransaction::Begin(
    SceneManager& scene,
    const std::vector<int32_t>& objectIds
)
{
    m_Active = true;
    m_ObjectIds = objectIds;
    m_BeforeSnapshots.clear();

    for (int32_t objectId : m_ObjectIds)
    {
        auto snapshot = scene.GetObjectTransformSnapshotById(objectId);

        if (snapshot.has_value())
        {
            m_BeforeSnapshots.push_back(snapshot.value());
        }
    }

    if (m_BeforeSnapshots.empty())
    {
        m_Active = false;
        m_ObjectIds.clear();
    }
}

void TransformTransaction::Commit(SceneManager& scene, UndoStack& undoStack)
{
    if (!m_Active)
        return;

    std::vector<ObjectTransformSnapshot> afterSnapshots;
    afterSnapshots.reserve(m_ObjectIds.size());

    for (int32_t objectId : m_ObjectIds)
    {
        auto snapshot = scene.GetObjectTransformSnapshotById(objectId);

        if (snapshot.has_value())
        {
            afterSnapshots.push_back(snapshot.value());
        }
    }

    auto command = std::make_unique<TransformObjectsCommand>(
        m_BeforeSnapshots,
        afterSnapshots
    );

    if (command->IsValid())
    {
        undoStack.PushAlreadyExecuted(std::move(command));
    }

    m_Active = false;
    m_ObjectIds.clear();
    m_BeforeSnapshots.clear();
}

void TransformTransaction::Cancel(SceneManager& scene)
{
    if (!m_Active)
        return;

    for (const auto& snapshot : m_BeforeSnapshots)
    {
        scene.SetObjectTransformById(snapshot);
    }

    m_Active = false;
    m_ObjectIds.clear();
    m_BeforeSnapshots.clear();
}

bool TransformTransaction::IsActive() const
{
    return m_Active;
}