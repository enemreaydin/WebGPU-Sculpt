#include "scene/SceneManager.h"
#include "editor/command/UndoStack.h"

#include <cassert>
#include <sstream>

namespace
{
class CounterCommand final : public IEditorCommand
{
public:
    explicit CounterCommand(int& value) : m_Value(value) {}
    void Execute(SceneManager&) override { ++m_Value; }
    void Undo(SceneManager&) override { --m_Value; }
    const char* GetName() const override { return "Counter"; }
private:
    int& m_Value;
};
}

int main()
{
    CsgTree tree;
    const int root = tree.AddOperationNode(CsgNodeType::Union);
    const int op = tree.AddOperationNode(CsgNodeType::Subtract, root);
    const int firstPrimitive = tree.AddPrimitiveNode(10, op);
    tree.AddPrimitiveNode(20, op);
    CsgNode* operation = tree.FindNodeById(tree.GetNodes()[op].nodeId);
    assert(operation);
    operation->blendMode = BlendMode::Smooth;
    operation->blendRadius = 0.25f;

    const auto program = tree.CompileToRPN({{10, 3}, {20, 7}});
    assert(program.size() == 3);
    assert(program[0].operand == 3 && program[1].operand == 7);
    assert(program[2].cmdType == CsgCmdType::OP_SMOOTH_SUBTRACT);
    assert(program[2].parameter == 0.25f);

    operation->type = CsgNodeType::Union;
    assert(tree.CompileToRPN({{10, 3}, {20, 7}}).back().cmdType == CsgCmdType::OP_SMOOTH_UNION);
    operation->type = CsgNodeType::Intersection;
    assert(tree.CompileToRPN({{10, 3}, {20, 7}}).back().cmdType == CsgCmdType::OP_SMOOTH_INTERSECTION);
    operation->blendRadius = 0.0f;
    assert(tree.CompileToRPN({{10, 3}, {20, 7}}).back().parameter == 0.0f);
    operation->type = CsgNodeType::Subtract;
    operation->blendRadius = 0.25f;

    CsgNode* roundedPrimitive = tree.FindNodeById(tree.GetNodes()[firstPrimitive].nodeId);
    roundedPrimitive->modifierType = ModifierType::Roundness;
    roundedPrimitive->modifierAmount = 0.1f;
    roundedPrimitive->modifierEnabled = true;
    const auto rounded = tree.CompileToRPN({{10, 3}, {20, 7}});
    assert(rounded.size() == 3 && rounded.front().parameter == 0.1f);
    assert(tree.GetNodes()[firstPrimitive].parent == op);
    roundedPrimitive->modifierEnabled = false;
    assert(tree.CompileToRPN({{10, 3}, {20, 7}}).front().parameter == 0.0f);
    roundedPrimitive->modifierEnabled = true;

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    tree.WriteToStream(stream);
    CsgTree restored;
    restored.ReadFromStream(stream);
    const CsgNode* restoredPrimitive = restored.FindNodeById(roundedPrimitive->nodeId);
    assert(restoredPrimitive && restoredPrimitive->modifierAmount == 0.1f);
    assert(restoredPrimitive->parent == op);

    CsgTree legacyTree;
    CsgTree::State legacyState;
    legacyState.root = 0;
    legacyState.nextNodeId = 3;
    legacyState.nodes.resize(3);
    legacyState.nodes[0].type = CsgNodeType::Union;
    legacyState.nodes[0].nodeId = 0;
    legacyState.nodes[0].firstChild = legacyState.nodes[0].lastChild = 1;
    legacyState.nodes[1].type = CsgNodeType::Modifier;
    legacyState.nodes[1].nodeId = 1;
    legacyState.nodes[1].parent = 0;
    legacyState.nodes[1].firstChild = legacyState.nodes[1].lastChild = 2;
    legacyState.nodes[1].modifierType = ModifierType::Roundness;
    legacyState.nodes[1].modifierAmount = 0.2f;
    legacyState.nodes[2].type = CsgNodeType::Primitive;
    legacyState.nodes[2].nodeId = 2;
    legacyState.nodes[2].objectId = 30;
    legacyState.nodes[2].parent = 1;
    legacyTree.RestoreState(legacyState);
    std::stringstream legacyStream(std::ios::in | std::ios::out | std::ios::binary);
    legacyTree.WriteToStream(legacyStream);
    CsgTree migrated;
    migrated.ReadFromStream(legacyStream);
    const CsgNode* migratedPrimitive = migrated.FindNodeById(2);
    assert(migratedPrimitive && migratedPrimitive->parent == 0);
    assert(migratedPrimitive->modifierAmount == 0.2f);
    assert(migrated.IndexForNodeId(1) == -1);

    SceneManager scene;
    UndoStack history;
    int counter = 0;
    for (int i = 0; i < 120; ++i)
        history.Execute(std::make_unique<CounterCommand>(counter), scene);
    assert(counter == 120 && history.UndoCount() == 100);
    for (int i = 0; i < 100; ++i) history.Undo(scene);
    assert(counter == 20 && !history.CanUndo() && history.RedoCount() == 100);
    history.Execute(std::make_unique<CounterCommand>(counter), scene);
    assert(counter == 21 && !history.CanRedo());

    UndoStack disabledHistory(0);
    disabledHistory.Execute(std::make_unique<CounterCommand>(counter), scene);
    assert(counter == 22 && !disabledHistory.CanUndo());
    return 0;
}
