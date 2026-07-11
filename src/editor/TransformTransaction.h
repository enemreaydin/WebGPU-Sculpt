#pragma once

#include "scene/data/ObjectTransformSnapshot.h"

#include <cstdint>
#include <vector>

class SceneManager;
class UndoStack;

class TransformTransaction
{
public:
    void Begin(SceneManager& scene, const std::vector<int32_t>& objectIds);
    void Commit(SceneManager& scene, UndoStack& undoStack);
    void Cancel(SceneManager& scene);

    bool IsActive() const;

private:
    bool m_Active = false;

    std::vector<int32_t> m_ObjectIds;
    std::vector<ObjectTransformSnapshot> m_BeforeSnapshots;
};