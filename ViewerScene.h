#pragma once
#include "Engine/GameObject.h"
#include "Engine/FbxChecker.h"
#include "Engine/Fbx.h"
#include <string>

//-----------------------------------------------------------
// FBX Checker + Simple Model Viewer Scene
//
// Drop FBX file onto window to load and check.
//   Mouse Left Drag  : rotate model
//   Mouse Wheel      : zoom in/out
//   R key            : reset camera
//-----------------------------------------------------------
class ViewerScene : public GameObject
{
    int         hModel_;
    FbxChecker  checker_;
    bool        checked_;
    std::string loadedFile_;

    // AABB of the loaded model
    AABB        modelAABB_;
    bool        hasAABB_;

    // Camera orbit
    float camYaw_;
    float camPitch_;
    float camDist_;

    // Fit distance computed from AABB
    float fitDist_;

    // Drag state
    bool  isDragging_;
    float prevMouseX_;
    float prevMouseY_;

    void LoadFbx(const std::string& filePath);
    void FitCameraToAABB();
    void UpdateCamera();
    void DrawCheckResults();

public:
    ViewerScene(GameObject* parent);

    void Initialize()   override;
    void Update()       override;
    void Draw()         override;
    void Release()      override;

    // Called from WndProc on WM_DROPFILES
    static void OnDropFile(const std::string& filePath);

    // Pending drop buffer (set by WndProc, consumed by Update)
    static std::string pendingDropFile_;
};
