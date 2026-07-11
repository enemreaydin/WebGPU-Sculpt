#include "UndoStack.h"

#include <utility>

void UndoStack::Execute(std::unique_ptr<IEditorCommand> command, SceneManager& scene)
{
    if (!command)
        return;

    command->Execute(scene);

    m_UndoStack.push_back(std::move(command));

    m_RedoStack.clear();
}

void UndoStack::PushAlreadyExecuted(std::unique_ptr<IEditorCommand> command)
{
    if (!command)
        return;

    m_UndoStack.push_back(std::move(command));

    m_RedoStack.clear();
}

void UndoStack::Undo(SceneManager& scene)
{
    if (m_UndoStack.empty())
        return;

    auto command = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();

    m_IsApplyingHistory = true;
    command->Undo(scene);
    m_IsApplyingHistory = false;

    m_RedoStack.push_back(std::move(command));
}

void UndoStack::Redo(SceneManager& scene)
{
    if (m_RedoStack.empty())
        return;

    auto command = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();

    m_IsApplyingHistory = true;
    command->Execute(scene);
    m_IsApplyingHistory = false;

    m_UndoStack.push_back(std::move(command));
}

bool UndoStack::CanUndo() const
{
    return !m_UndoStack.empty();
}

bool UndoStack::CanRedo() const
{
    return !m_RedoStack.empty();
}

bool UndoStack::IsApplyingHistory() const
{
    return m_IsApplyingHistory;
}

void UndoStack::Clear()
{
    m_UndoStack.clear();
    m_RedoStack.clear();
    m_IsApplyingHistory = false;
}