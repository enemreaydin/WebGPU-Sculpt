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
    tree.AddPrimitiveNode(10, op);
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

    const int modifierId = tree.AddRoundnessModifier(op, 0.1f);
    const int modifierIndex = tree.IndexForNodeId(modifierId);
    assert(modifierIndex >= 0);
    const int extra = tree.AddPrimitiveNode(30);
    tree.AttachChild(modifierIndex, extra);
    assert(tree.GetNodes()[modifierIndex].firstChild == tree.GetNodes()[modifierIndex].lastChild);
    const auto rounded = tree.CompileToRPN({{10, 3}, {20, 7}, {30, 8}});
    assert(rounded.back().cmdType == CsgCmdType::MOD_ROUNDNESS);
    tree.FindNodeById(modifierId)->modifierEnabled = false;
    const auto disabled = tree.CompileToRPN({{10, 3}, {20, 7}, {30, 8}});
    assert(disabled.back().cmdType == CsgCmdType::OP_SMOOTH_SUBTRACT);
    tree.FindNodeById(modifierId)->modifierEnabled = true;

    std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
    tree.WriteToStream(stream);
    CsgTree restored;
    restored.ReadFromStream(stream);
    const CsgNode* restoredModifier = restored.FindNodeById(modifierId);
    assert(restoredModifier && restoredModifier->modifierAmount == 0.1f);

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
