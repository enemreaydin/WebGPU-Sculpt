#include "editor/Application.h"

void Application::DrawGui() {
    DrawMainMenuBar();
    SdfObjectData newObject{};

    if (addObjectPanel.Draw(newObject))
    {
        AddObjectToScene(newObject);
    }
    DrawSettings();
    DrawWorkspace();

}
