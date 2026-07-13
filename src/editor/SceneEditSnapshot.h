#pragma once

#include "editor/EditorObjectStore.h"
#include "editor/Settings.h"
#include "interaction/SelectionState.h"
#include "scene/SceneManager.h"

struct SceneEditSnapshot
{
    SceneManagerState scene;
    EditorObjectStoreState editorObjects;
    SelectionState selection;
    Settings settings;
};
