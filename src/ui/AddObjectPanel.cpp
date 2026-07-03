#include "AddObjectPanel.h"
#include "scene/SdfObjectFactory.h"

#include <imgui.h>

void AddObjectPanel::Open(SdfObjectType type)
{
    selectedType = type;
    isOpen = true;
    requestOpen = true;
}

bool AddObjectPanel::Draw(SdfObjectData& outObject)
{
    bool created = false;

    if (requestOpen)
    {
        ImGui::OpenPopup("Add Object");
        requestOpen = false;
    }

    if (!isOpen)
        return false;

    if (ImGui::BeginPopupModal("Add Object", &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
    {
        switch (selectedType)
        {
        case SdfObjectType::Sphere:
            ImGui::Text("Add Sphere");
            ImGui::InputFloat("Radius", &sphereRadius);

            if (sphereRadius < 0.01f)
                sphereRadius = 0.01f;

            break;

        case SdfObjectType::Box:
            ImGui::Text("Add Box");
            ImGui::InputFloat3("Size", boxSize);

            for (int i = 0; i < 3; i++)
            {
                if (boxSize[i] < 0.01f)
                    boxSize[i] = 0.01f;
            }

            break;

        case SdfObjectType::Cylinder:
            ImGui::Text("Add Cylinder");
            ImGui::InputFloat("Radius", &cylinderRadius);
            ImGui::InputFloat("Height", &cylinderHeight);

            if (cylinderRadius < 0.01f)
                cylinderRadius = 0.01f;

            if (cylinderHeight < 0.01f)
                cylinderHeight = 0.01f;

            break;

        case SdfObjectType::Torus:
            ImGui::Text("Add Torus");
            ImGui::InputFloat("Major Radius", &torusMajorRadius);
            ImGui::InputFloat("Minor Radius", &torusMinorRadius);

            if (torusMajorRadius < 0.01f)
                torusMajorRadius = 0.01f;

            if (torusMinorRadius < 0.01f)
                torusMinorRadius = 0.01f;

            break;
        }


        ImGui::Separator();

        if (ImGui::Button("Create"))
        {
            switch (selectedType)
            {
            case SdfObjectType::Sphere:
                outObject = SdfObjectFactory::CreateSphere(sphereRadius);
                break;

            case SdfObjectType::Box:
                outObject = SdfObjectFactory::CreateBox(
                    boxSize[0],
                    boxSize[1],
                    boxSize[2]
                );
                break;

            case SdfObjectType::Cylinder:
                outObject = SdfObjectFactory::CreateCylinder(
                    cylinderRadius,
                    cylinderHeight
                );
                break;

            case SdfObjectType::Torus:
                outObject = SdfObjectFactory::CreateTorus(
                    torusMajorRadius,
                    torusMinorRadius
                );
                break;
            }

            created = true;
            isOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
            isOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    return created;
}