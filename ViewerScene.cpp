#include "ViewerScene.h"
#include "Engine/Model.h"
#include "Engine/Camera.h"
#include "Engine/Input.h"
#include "Engine/Direct3D.h"
#include "Engine/Debug.h"
#include "Engine/Fbx.h"

std::string ViewerScene::pendingDropFile_;
const float ViewerScene::ANIM_SPEED = 0.5f;

static const float INIT_DIST  = 10.0f;
static const float INIT_PITCH = 15.0f;
static const float INIT_YAW   = 0.0f;

ViewerScene::ViewerScene(GameObject* parent)
    : GameObject(parent, "ViewerScene"),
      hModel_(-1), checked_(false), hasAABB_(false), fitDist_(INIT_DIST),
      camYaw_(INIT_YAW), camPitch_(INIT_PITCH),
      camDist_(INIT_DIST), camDistTarget_(INIT_DIST),
      isDragging_(false), prevMouseX_(0.0f), prevMouseY_(0.0f),
      isAnimPlaying_(false), animEndFrame_(0)
{
}

void ViewerScene::Initialize()
{
    SetWindowTextA(GetActiveWindow(), "FBX Viewer  - Drop FBX file here -");
}

//-----------------------------------------------------------
// Load FBX: run checker first, then Model::Load if OK
//-----------------------------------------------------------
void ViewerScene::LoadFbx(const std::string& filePath)
{
    if (hModel_ >= 0)
    {
        Model::Release(hModel_);
        hModel_ = -1;
    }
    checked_ = false;
    checker_ = FbxChecker();
    loadedFile_ = filePath;

    // Run checker (uses FBX SDK directly, independent of FbxParts)
    {
        FbxManager* mgr   = FbxManager::Create();
        FbxScene*   scene = FbxScene::Create(mgr, "checkerScene");
        FbxImporter* imp  = FbxImporter::Create(mgr, "imp");

        if (imp->Initialize(filePath.c_str(), -1, mgr->GetIOSettings()))
        {
            imp->Import(scene);
            imp->Destroy();
            checker_.Run(scene);
            checker_.DumpToDebugOutput();
        }
        else
        {
            imp->Destroy();
            Debug::Log("[ViewerScene] FBX import failed: " + filePath, true);
        }
        scene->Destroy();
        mgr->Destroy();
    }
    checked_ = true;

    // Load model only when no errors
    if (!checker_.HasError())
    {
        hModel_ = Model::Load(filePath);
        if (hModel_ >= 0)
        {
            modelAABB_ = Model::GetAABB(hModel_);
            hasAABB_   = true;
            FitCameraToAABB();

            // アニメーションのフレーム数をFBXシーンから取得
            animEndFrame_ = 0;
            const Model::ModelData* data = Model::GetData(hModel_);
            if (data && data->pFbx)
            {
                FbxScene* fbxScene = data->pFbx->GetFbxScene();
                // GetCurrentAnimationStack() は未設定だと nullptr になるので
                // GetSrcObject で最初のスタックを確実に取得してからセットする
                FbxAnimStack* stack = fbxScene->GetSrcObject<FbxAnimStack>(0);
                if (stack)
                {
                    fbxScene->SetCurrentAnimationStack(stack);
                    FbxTimeSpan span    = stack->GetLocalTimeSpan();
                    FbxTime::EMode mode = fbxScene->GetGlobalSettings().GetTimeMode();
                    animEndFrame_ = (int)span.GetDuration().GetFrameCount(mode);
                }
            }

            char buf[64];
            wsprintfA(buf, "[Viewer] animEndFrame=%d", animEndFrame_);
            Debug::Log(buf, true);

            // ロード直後は静止モード（frame=0）
            isAnimPlaying_ = false;
            Model::SetAnimFrame(hModel_, 0, animEndFrame_, 0.0f);
        }
        else
        {
            Debug::Log("[ViewerScene] Model::Load failed: " + filePath, true);
        }
    }
}

//-----------------------------------------------------------
// Fit camera distance and target to model AABB
//-----------------------------------------------------------
void ViewerScene::FitCameraToAABB()
{
    if (!hasAABB_) return;

    // Camera target = AABB center
    XMFLOAT3 center = modelAABB_.Center();

    // Fit distance: half the longest edge / tan(FOV/2)
    // FOV = 45deg (XM_PIDIV4), so tan(22.5deg) = ~0.4142
    float halfExtent = modelAABB_.LongestEdge() * 0.5f;
    fitDist_ = halfExtent / 0.4142f + halfExtent; // margin = 1 extra halfExtent
    if (fitDist_ < 1.0f) fitDist_ = 1.0f;

    camDist_       = fitDist_;
    camDistTarget_ = fitDist_;
    camYaw_   = INIT_YAW;
    camPitch_ = INIT_PITCH;

    // Store center as camera target offset (used in UpdateCamera)
    // We keep origin-relative orbit: translate model to origin in Draw()
    // so target stays at (0,0,0), but we offset camPitch-compensated Y
    // by center.y in the camera position.
    char buf[128];
    wsprintfA(buf, "[AABB] center=(%.2f,%.2f,%.2f) size=(%.2f,%.2f,%.2f) fitDist=%.2f",
        center.x, center.y, center.z,
        modelAABB_.Size().x, modelAABB_.Size().y, modelAABB_.Size().z,
        fitDist_);
    Debug::Log(buf, true);
}

//-----------------------------------------------------------
// Animation toggle (Space key)
//-----------------------------------------------------------
void ViewerScene::UpdateAnimation()
{
    if (hModel_ < 0) return;

    if (Input::IsKeyDown(DIK_SPACE))
    {
        isAnimPlaying_ = !isAnimPlaying_;
        if (isAnimPlaying_)
            Model::SetAnimFrame(hModel_, 0, animEndFrame_, ANIM_SPEED);
        else
            Model::SetAnimFrame(hModel_, 0, animEndFrame_, 0.0f);
    }
}

//-----------------------------------------------------------
// Camera: spherical orbit around model AABB center
//-----------------------------------------------------------
void ViewerScene::UpdateCamera()
{
    if (Input::IsMouseButtonDown(0))
    {
        isDragging_ = true;
        XMFLOAT3 pos = Input::GetMousePosition();
        prevMouseX_ = pos.x;
        prevMouseY_ = pos.y;
    }
    if (Input::IsMouseButtonUp(0))
        isDragging_ = false;

    if (isDragging_ && Input::IsMouseButton(0))
    {
        XMFLOAT3 pos = Input::GetMousePosition();
        camYaw_   += (pos.x - prevMouseX_) * 0.5f;
        camPitch_ += (pos.y - prevMouseY_) * 0.5f;
        if (camPitch_ >  89.0f) camPitch_ =  89.0f;
        if (camPitch_ < -89.0f) camPitch_ = -89.0f;
        prevMouseX_ = pos.x;
        prevMouseY_ = pos.y;
    }

    // Wheel zoom: lZ は1ノッチ=±120なので120で割ってノッチ数に正規化
    // fitDist_ 基準の割合で目標距離を変化させる
    float wheel = Input::GetMouseMove().z / 120.0f;
    float zoomBase = hasAABB_ ? fitDist_ : INIT_DIST;
    camDistTarget_ -= wheel * zoomBase * 0.15f;
    float minDist = zoomBase * 0.05f;
    float maxDist = zoomBase * 10.0f;
    if (camDistTarget_ < minDist) camDistTarget_ = minDist;
    if (camDistTarget_ > maxDist) camDistTarget_ = maxDist;

    // 現在距離を目標距離へ滑らかに近づける（lerp）
    camDist_ += (camDistTarget_ - camDist_) * 0.15f;

    // R: reset camera to fitted position
    if (Input::IsKeyDown(DIK_R))
    {
        camYaw_        = INIT_YAW;
        camPitch_      = INIT_PITCH;
        camDist_       = hasAABB_ ? fitDist_ : INIT_DIST;
        camDistTarget_ = camDist_;
    }

    float yawRad   = XMConvertToRadians(camYaw_);
    float pitchRad = XMConvertToRadians(camPitch_);
    float cx = camDist_ * cosf(pitchRad) * sinf(yawRad);
    float cy = camDist_ * sinf(pitchRad);
    float cz = -camDist_ * cosf(pitchRad) * cosf(yawRad);

    // Orbit around AABB center (Y component)
    float targetY = hasAABB_ ? modelAABB_.Center().y : 0.0f;
    Camera::SetPosition(XMFLOAT3(cx, cy + targetY, cz));
    Camera::SetTarget(XMFLOAT3(0.0f, targetY, 0.0f));
}

//-----------------------------------------------------------
// Show check summary in window title bar
//-----------------------------------------------------------
void ViewerScene::DrawCheckResults()
{
    if (!checked_) return;

    int okCnt = 0, warnCnt = 0, errCnt = 0;
    const std::vector<FbxChecker::CheckResult>& results = checker_.GetResults();
    for (size_t i = 0; i < results.size(); i++)
    {
        switch (results[i].level)
        {
        case FbxChecker::CheckResult::OK:      okCnt++;   break;
        case FbxChecker::CheckResult::WARNING: warnCnt++; break;
        case FbxChecker::CheckResult::ERR:     errCnt++;  break;
        }
    }

    // Extract filename only
    std::string fname = loadedFile_;
    size_t sep = fname.find_last_of("/\\");
    if (sep != std::string::npos)
        fname = fname.substr(sep + 1);

    char title[256];
    const char* animState = isAnimPlaying_ ? "[ANIM]" : "[STATIC]";
    if (errCnt > 0)
        wsprintfA(title, "FBX Viewer %s [%s]  ERROR:%d WARN:%d OK:%d  <- Cannot display", animState, fname.c_str(), errCnt, warnCnt, okCnt);
    else if (warnCnt > 0)
        wsprintfA(title, "FBX Viewer %s [%s]  WARN:%d OK:%d  <- Displayable (check warnings)", animState, fname.c_str(), warnCnt, okCnt);
    else
        wsprintfA(title, "FBX Viewer %s [%s]  OK:%d  <- All checks passed", animState, fname.c_str(), okCnt);

    SetWindowTextA(GetActiveWindow(), title);
}

//-----------------------------------------------------------
// Update
//-----------------------------------------------------------
void ViewerScene::Update()
{
    if (!pendingDropFile_.empty())
    {
        LoadFbx(pendingDropFile_);
        pendingDropFile_.clear();
    }
    UpdateAnimation();
    UpdateCamera();
}

//-----------------------------------------------------------
// Draw
//-----------------------------------------------------------
void ViewerScene::Draw()
{
    DrawCheckResults();

    if (hModel_ < 0) return;

    Transform t;
    t.position_ = { 0.0f, 0.0f, 0.0f };
    t.rotate_   = { 0.0f, 0.0f, 0.0f };
    t.scale_    = { 1.0f, 1.0f, 1.0f };
    Model::SetTransform(hModel_, t);
    Model::Draw(hModel_);
}

//-----------------------------------------------------------
// Release
//-----------------------------------------------------------
void ViewerScene::Release()
{
    if (hModel_ >= 0)
    {
        Model::Release(hModel_);
        hModel_ = -1;
    }
}

//-----------------------------------------------------------
// Static: called from WndProc
//-----------------------------------------------------------
void ViewerScene::OnDropFile(const std::string& filePath)
{
    pendingDropFile_ = filePath;
}
