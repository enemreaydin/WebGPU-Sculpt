#pragma once
#include <cstdint>
#include <vector>

// Tracks which scene object is currently selected (-1 = none).
class SelectionState
{
public:
    std::vector<int32_t> selectedNodeIds;
    int32_t primaryNodeId = -1;          // -1 = nothing selected. Must be
                                         // default-initialized: GetSceneJson()
                                         // and UpdateSelectionHighlight() read it on
                                         // startup, before any Clear()/Add().
    int32_t selectedObjectIndex = -1;
    
    bool Contains(int32_t nodeId) {
        for (int32_t n : selectedNodeIds) if (n == nodeId)
        {
            return true;
        }return false;
    }
    void Add(int32_t nodeId){if (!Contains(nodeId)){selectedNodeIds.push_back(nodeId);}
    }
    void Remove(int32_t nodeId) {
        for (size_t i = 0; i < selectedNodeIds.size(); ++i)
            if (selectedNodeIds[i] == nodeId) { selectedNodeIds.erase(selectedNodeIds.begin() + i); return; }
    }
    void Clear() { selectedNodeIds.clear(); primaryNodeId = -1; }

};
