#pragma once
#include <fbxsdk.h>
#include <string>
#include <vector>

//-----------------------------------------------------------
// FBXChecker
// FBXfile check class for viewer compatibility
//-----------------------------------------------------------
class FbxChecker
{
public:
    struct CheckResult
    {
        enum Level { OK, WARNING, ERR };

        Level       level;
        std::string category;
        std::string message;
    };

private:
    std::vector<CheckResult> results_;

    void CheckTriangulation(FbxScene* scene);
    void CheckMaterial(FbxScene* scene);
    void CheckTexturePath(FbxScene* scene);
    void CheckUV(FbxScene* scene);
    void CheckSkin(FbxScene* scene);
    void CheckFrameRate(FbxScene* scene);
    void CheckAxisSystem(FbxScene* scene);

    void AddResult(CheckResult::Level level, const std::string& category, const std::string& message);

public:
    // Run all checks. Returns true if no errors.
    bool Run(FbxScene* scene);

    const std::vector<CheckResult>& GetResults() const { return results_; }
    bool HasError() const;
    void DumpToDebugOutput() const;
};
