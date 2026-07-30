#pragma once

#include <DirectXMath.h>

namespace OVRInject {
namespace Stereo {

// Tracks the runtime eye pose that was written to the game camera and later
// became the content of each persistent eye texture. AER intentionally keeps
// one stale texture; its submitted layer must keep that texture's own render
// pose instead of being relabelled with the current tracking pose.
class RenderPoseHistory {
public:
    struct Slot {
        DirectX::XMMATRIX pose = DirectX::XMMatrixIdentity();
        bool valid = false;
    };

    void Reset() {
        pending_[0] = Slot{};
        pending_[1] = Slot{};
        texture_[0] = Slot{};
        texture_[1] = Slot{};
    }

    void RecordCameraWrite(int eye, const DirectX::XMMATRIX& pose) {
        pending_[eye & 1].pose = pose;
        pending_[eye & 1].valid = true;
    }

    bool CommitRenderedTexture(int eye) {
        const int index = eye & 1;
        if (!pending_[index].valid) {
            return false;
        }
        texture_[index] = pending_[index];
        return true;
    }

    const DirectX::XMMATRIX* GetTexturePose(int eye) const {
        const Slot& slot = texture_[eye & 1];
        return slot.valid ? &slot.pose : nullptr;
    }

    bool HasPendingPose(int eye) const {
        return pending_[eye & 1].valid;
    }

private:
    Slot pending_[2];
    Slot texture_[2];
};

} // namespace Stereo
} // namespace OVRInject
