#pragma once

#include "IEditorCommand.h"

#include <memory>
#include <vector>

class SceneManager;

class UndoStack
{
public:
    UndoStack() = default;
    ~UndoStack() = default;

    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    void Execute(std::unique_ptr<IEditorCommand> command, SceneManager& scene);
    void PushAlreadyExecuted(std::unique_ptr<IEditorCommand> command);

    void Undo(SceneManager& scene);
    void Redo(SceneManager& scene);

    bool CanUndo() const;
    bool CanRedo() const;

    bool IsApplyingHistory() const;

    void Clear();

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_UndoStack;
    std::vector<std::unique_ptr<IEditorCommand>> m_RedoStack;

    bool m_IsApplyingHistory = false;
};