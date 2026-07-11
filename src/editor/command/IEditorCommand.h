#pragma once

class SceneManager;

class IEditorCommand
{
public:
    virtual ~IEditorCommand() = default;

    virtual void Execute(SceneManager& scene) = 0;
    virtual void Undo(SceneManager& scene) = 0;

    virtual const char* GetName() const = 0;
};