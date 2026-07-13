#include "UndoStack.h"

#include <utility>

void UndoStack::Execute(std::unique_ptr<IEditorCommand> command, SceneManager& scene)
{
    if (!command)
        return;

    command->Execute(scene);

    if (m_MaxEntries == 0) { m_RedoStack.clear(); return; }

    m_UndoStack.push_back(std::move(command));
    Trim(m_UndoStack);

    m_RedoStack.clear();
}

void UndoStack::PushAlreadyExecuted(std::unique_ptr<IEditorCommand> command)
{
    if (!command)
        return;

    if (m_MaxEntries == 0) { m_RedoStack.clear(); return; }

    m_UndoStack.push_back(std::move(command));
    Trim(m_UndoStack);

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
    Trim(m_RedoStack);
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
    Trim(m_UndoStack);
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

void UndoStack::Trim(std::deque<std::unique_ptr<IEditorCommand>>& stack)
{
    while (stack.size() > m_MaxEntries) stack.pop_front();
}
