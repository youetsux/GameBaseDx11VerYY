#include "FbxChecker.h"
#include "Debug.h"
#include <sstream>

void FbxChecker::AddResult(CheckResult::Level level, const std::string& category, const std::string& message)
{
    CheckResult r;
    r.level    = level;
    r.category = category;
    r.message  = message;
    results_.push_back(r);
}

// Check 1: Triangulation
void FbxChecker::CheckTriangulation(FbxScene* scene)
{
    int meshCount = scene->GetSrcObjectCount<FbxMesh>();
    if (meshCount == 0)
    {
        AddResult(CheckResult::WARNING, "Triangulation", "No mesh found in scene.");
        return;
    }

    bool allOk = true;
    for (int i = 0; i < meshCount; i++)
    {
        FbxMesh* mesh = scene->GetSrcObject<FbxMesh>(i);
        int polyCount = mesh->GetPolygonCount();
        for (int j = 0; j < polyCount; j++)
        {
            if (mesh->GetPolygonSize(j) != 3)
            {
                std::ostringstream oss;
                oss << "Non-triangle polygon found."
                    << " mesh[" << i << "]=\"" << mesh->GetName() << "\""
                    << " poly[" << j << "] size=" << mesh->GetPolygonSize(j)
                    << " -> Please triangulate before export.";
                AddResult(CheckResult::ERR, "Triangulation", oss.str());
                allOk = false;
                break;
            }
        }
    }
    if (allOk)
        AddResult(CheckResult::OK, "Triangulation", "All polygons are triangles.");
}

// Check 2: Material type
void FbxChecker::CheckMaterial(FbxScene* scene)
{
    int meshCount = scene->GetSrcObjectCount<FbxMesh>();
    for (int i = 0; i < meshCount; i++)
    {
        FbxMesh* mesh = scene->GetSrcObject<FbxMesh>(i);
        FbxNode* node = mesh->GetNode();
        if (node == nullptr) continue;

        int matCount = node->GetMaterialCount();
        if (matCount == 0)
        {
            std::ostringstream oss;
            oss << "No material. mesh[" << i << "]=\"" << mesh->GetName() << "\"";
            AddResult(CheckResult::ERR, "Material", oss.str());
            continue;
        }

        for (int m = 0; m < matCount; m++)
        {
            FbxSurfaceMaterial* mat = node->GetMaterial(m);
            if (mat == nullptr) continue;

            std::ostringstream oss;
            if (mat->GetClassId().Is(FbxSurfacePhong::ClassId))
            {
                oss << "Phong  mesh[" << i << "] mat[" << m << "]=\"" << mat->GetName() << "\"";
                AddResult(CheckResult::OK, "Material", oss.str());
            }
            else if (mat->GetClassId().Is(FbxSurfaceLambert::ClassId))
            {
                oss << "Lambert (Specular/Shininess unavailable)"
                    << " mesh[" << i << "] mat[" << m << "]=\"" << mat->GetName() << "\"";
                AddResult(CheckResult::WARNING, "Material", oss.str());
            }
            else
            {
                oss << "Unsupported material type: " << mat->GetClassId().GetName()
                    << " mesh[" << i << "] mat[" << m << "]=\"" << mat->GetName() << "\""
                    << " -> Phong or Lambert required.";
                AddResult(CheckResult::ERR, "Material", oss.str());
            }
        }
    }
}

// Check 3: Texture path
void FbxChecker::CheckTexturePath(FbxScene* scene)
{
    int texCount = scene->GetSrcObjectCount<FbxFileTexture>();
    if (texCount == 0)
    {
        AddResult(CheckResult::WARNING, "Texture", "No textures (texture-less model).");
        return;
    }

    for (int i = 0; i < texCount; i++)
    {
        FbxFileTexture* tex = scene->GetSrcObject<FbxFileTexture>(i);
        const char* relPath = tex->GetRelativeFileName();
        const char* absPath = tex->GetFileName();

        bool relEmpty = (relPath == nullptr || relPath[0] == '\0');
        bool absEmpty = (absPath == nullptr || absPath[0] == '\0');

        std::ostringstream oss;
        if (relEmpty && absEmpty)
        {
            oss << "Empty texture path. tex[" << i << "]=\"" << tex->GetName() << "\"";
            AddResult(CheckResult::ERR, "Texture", oss.str());
        }
        else if (relEmpty)
        {
            oss << "Relative path missing (absolute only). tex[" << i << "]=\"" << tex->GetName()
                << "\" absPath=" << absPath
                << " -> May fail to load. Use relative path or place texture next to FBX.";
            AddResult(CheckResult::WARNING, "Texture", oss.str());
        }
        else
        {
            oss << "OK  tex[" << i << "]=\"" << tex->GetName() << "\" relPath=" << relPath;
            AddResult(CheckResult::OK, "Texture", oss.str());
        }
    }
}

// Check 4: UV
void FbxChecker::CheckUV(FbxScene* scene)
{
    int meshCount = scene->GetSrcObjectCount<FbxMesh>();
    for (int i = 0; i < meshCount; i++)
    {
        FbxMesh* mesh = scene->GetSrcObject<FbxMesh>(i);
        int uvCount = mesh->GetUVLayerCount();
        std::ostringstream oss;
        if (uvCount == 0)
        {
            oss << "No UV. mesh[" << i << "]=\"" << mesh->GetName() << "\"";
            AddResult(CheckResult::ERR, "UV", oss.str());
        }
        else
        {
            oss << "UV sets=" << uvCount << " mesh[" << i << "]=\"" << mesh->GetName() << "\"";
            if (uvCount > 1)
                oss << " (viewer uses UV[0] only)";
            AddResult(uvCount > 1 ? CheckResult::WARNING : CheckResult::OK, "UV", oss.str());
        }
    }
}

// Check 5: Skin (info only)
void FbxChecker::CheckSkin(FbxScene* scene)
{
    int meshCount = scene->GetSrcObjectCount<FbxMesh>();
    int skinnedCount = 0;
    for (int i = 0; i < meshCount; i++)
    {
        FbxMesh* mesh = scene->GetSrcObject<FbxMesh>(i);
        FbxDeformer* deformer = mesh->GetDeformer(0);
        if (deformer != nullptr)
        {
            FbxSkin* skin = static_cast<FbxSkin*>(deformer);
            std::ostringstream oss;
            oss << "Skinned mesh. mesh[" << i << "]=\"" << mesh->GetName()
                << "\" bones=" << skin->GetClusterCount();
            AddResult(CheckResult::OK, "Skin", oss.str());
            skinnedCount++;
        }
    }
    if (skinnedCount == 0)
        AddResult(CheckResult::OK, "Skin", "No skin (static mesh or mesh animation).");
}

// Check 6: Frame rate (info only)
void FbxChecker::CheckFrameRate(FbxScene* scene)
{
    FbxTime::EMode mode = scene->GetGlobalSettings().GetTimeMode();
    double fps = FbxTime::GetFrameRate(mode);

    std::ostringstream oss;
    oss << "FPS=" << (int)fps << " (TimeMode=" << (int)mode << ")";

    CheckResult::Level lv = CheckResult::OK;
    if (mode != FbxTime::eFrames30 && mode != FbxTime::eFrames24)
    {
        oss << " -> Not 24/30fps. Animation speed may differ from intended.";
        lv = CheckResult::WARNING;
    }
    AddResult(lv, "FrameRate", oss.str());
}

// Check 7: Axis system (info only)
void FbxChecker::CheckAxisSystem(FbxScene* scene)
{
    FbxAxisSystem axis = scene->GetGlobalSettings().GetAxisSystem();
    int sign = 0;
    FbxAxisSystem::EUpVector upVec = axis.GetUpVector(sign);

    std::ostringstream oss;
    if (upVec == FbxAxisSystem::eYAxis)
    {
        oss << "Y-up (Maya standard). Viewer expects Y-up. OK.";
        AddResult(CheckResult::OK, "AxisSystem", oss.str());
    }
    else if (upVec == FbxAxisSystem::eZAxis)
    {
        oss << "Z-up (Blender default). Enable [Apply Transform] on FBX export.";
        AddResult(CheckResult::WARNING, "AxisSystem", oss.str());
    }
    else
    {
        oss << "X-up detected (unusual setting).";
        AddResult(CheckResult::WARNING, "AxisSystem", oss.str());
    }
}

// Run all checks
bool FbxChecker::Run(FbxScene* scene)
{
    results_.clear();
    CheckTriangulation(scene);
    CheckMaterial(scene);
    CheckTexturePath(scene);
    CheckUV(scene);
    CheckSkin(scene);
    CheckFrameRate(scene);
    CheckAxisSystem(scene);
    return !HasError();
}

bool FbxChecker::HasError() const
{
    for (size_t i = 0; i < results_.size(); i++)
    {
        if (results_[i].level == CheckResult::ERR)
            return true;
    }
    return false;
}

void FbxChecker::DumpToDebugOutput() const
{
    Debug::Log("========== FBX Checker Results ==========", true);
    for (size_t i = 0; i < results_.size(); i++)
    {
        const CheckResult& r = results_[i];
        std::string prefix;
        switch (r.level)
        {
        case CheckResult::OK:      prefix = "[  OK  ] "; break;
        case CheckResult::WARNING: prefix = "[ WARN ] "; break;
        case CheckResult::ERR:     prefix = "[ERROR ] "; break;
        }
        Debug::Log(prefix + "[" + r.category + "] " + r.message, true);
    }
    Debug::Log("=========================================", true);
}
