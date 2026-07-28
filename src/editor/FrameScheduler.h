#pragma once

class FrameScheduler
{
private:
    int frameCount = 0;
    bool forceContinuous = false;
public:
    void Request(int frames);
    bool ShouldRender() const;
    void Consume();
    void ForceContinuous(bool enabled);
    bool IsForceContinuous() const { return forceContinuous; }
};
