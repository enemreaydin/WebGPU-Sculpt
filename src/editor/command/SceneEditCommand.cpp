#include "editor/command/SceneEditCommand.h"

#include <utility>

SceneEditCommand::SceneEditCommand(
    std::string name,
    SceneEditSnapshot before,
    SceneEditSnapshot after,
    EditorObjectStore& editorStore,
    SelectionState& selectionState,
    Settings& settings
)
    : m_Name(std::move(name)),
      m_Before(std::move(before)),
      m_After(std::move(after)),
      m_EditorStore(&editorStore),
      m_SelectionState(&selectionState),
      m_Settings(&settings)
{
}

void SceneEditCommand::Execute(SceneManager& scene)
{
    Apply(scene, m_After);
}

void SceneEditCommand::Undo(SceneManager& scene)
{
    Apply(scene, m_Before);
}

const char* SceneEditCommand::GetName() const
{
    return m_Name.c_str();
}

void SceneEditCommand::Apply(SceneManager& scene, const SceneEditSnapshot& snapshot)
{
    scene.RestoreState(snapshot.scene);
    if (m_EditorStore) m_EditorStore->RestoreState(snapshot.editorObjects);
    if (m_SelectionState) *m_SelectionState = snapshot.selection;
    if (m_Settings) *m_Settings = snapshot.settings;
}
