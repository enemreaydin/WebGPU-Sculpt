#pragma once

#include "IEditorCommand.h"

#include <memory>
#include <deque>
#include <cstddef>

class SceneManager;

class UndoStack
{
public:
    static constexpr std::size_t DEFAULT_MAX_HISTORY_ENTRIES = 100;
    explicit UndoStack(std::size_t maxEntries = DEFAULT_MAX_HISTORY_ENTRIES)
        : m_MaxEntries(maxEntries) {}
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
    std::size_t UndoCount() const { return m_UndoStack.size(); }
    std::size_t RedoCount() const { return m_RedoStack.size(); }

private:
    std::deque<std::unique_ptr<IEditorCommand>> m_UndoStack;
    std::deque<std::unique_ptr<IEditorCommand>> m_RedoStack;
    std::size_t m_MaxEntries;

    void Trim(std::deque<std::unique_ptr<IEditorCommand>>& stack);

    bool m_IsApplyingHistory = false;
};
