#pragma once

#include "editor/SceneEditSnapshot.h"
#include "editor/command/IEditorCommand.h"

#include <string>

class SceneEditCommand : public IEditorCommand
{
public:
    SceneEditCommand(
        std::string name,
        SceneEditSnapshot before,
        SceneEditSnapshot after,
        EditorObjectStore& editorStore,
        SelectionState& selectionState,
        Settings& settings
    );

    void Execute(SceneManager& scene) override;
    void Undo(SceneManager& scene) override;
    const char* GetName() const override;

private:
    std::string m_Name;
    SceneEditSnapshot m_Before;
    SceneEditSnapshot m_After;
    EditorObjectStore* m_EditorStore = nullptr;
    SelectionState* m_SelectionState = nullptr;
    Settings* m_Settings = nullptr;

    void Apply(SceneManager& scene, const SceneEditSnapshot& snapshot);
};
