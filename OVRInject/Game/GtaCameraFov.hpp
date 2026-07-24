#pragma once

#include "../VR/SharedSettings.hpp"
#include "PatternScanner.hpp"

#include <cstdint>

namespace OVRInject {
namespace Game {

class GtaCameraFov {
public:
    bool Initialize();
    void Update(const VR::FovSettings& settings);
    bool IsReady() const { return get_cam_director_ != nullptr; }
    uintptr_t GetActiveCameraAddress() const;
    uintptr_t GetDirectorAddress() const;

private:
    struct camBaseObjectMetadata {
        void** vftable;
        uint32_t hashKey;
    };

    struct camBaseCameraMetadata : camBaseObjectMetadata {
        uint32_t hashName;
    };

    struct camBaseCamera {
        char pad[0x540];
        camBaseCameraMetadata* metadata;
    };

    struct camBaseDirector {
        char pad[0x2C0];
        camBaseCamera* activeCamera;
    };

    using GetCamDirectorFromPool = camBaseDirector* (*)();

    bool Resolve();
    // SEH-guarded trial call of a candidate getCamDirectorFromPool address
    // (kept out of Resolve, whose std::vector locals forbid __try - C2712).
    bool TryAdoptDirectorFunction(GetCamDirectorFromPool fn, uintptr_t target, int patternIndex);
    camBaseCamera* GetActiveCamera() const;
    camBaseCameraMetadata* GetActiveMetadata() const;
    bool IsReadable(uintptr_t address, size_t size) const;
    bool WriteFloat(uintptr_t address, float value) const;
    int GetFovOffset(uint32_t hashKey, uint32_t hashName) const;
    float GetDesiredFov(const VR::FovSettings& settings,
                        uint32_t hashKey,
                        uint32_t hashName) const;

    GetCamDirectorFromPool get_cam_director_ = nullptr;
    bool init_attempted_ = false;
};

} // namespace Game
} // namespace OVRInject
