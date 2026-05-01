#include "Fbx.h"
#include "Direct3D.h"
#include "FbxParts.h"


#pragma comment(lib, "LibFbxSDK-MT.lib")
#pragma comment(lib, "LibXml2-MT.lib")
#pragma comment(lib, "zlib-MT.lib")

Fbx::Fbx():_animSpeed(0), _currentAnimStack(0), _startFrame(0), _endFrame(0)
{
}

Fbx::~Fbx()
{
	for (int i = 0; i < parts_.size(); i++)
	{
		delete parts_[i];
	}
	parts_.clear();

	pFbxScene_->Destroy();
	pFbxManager_->Destroy();
}

HRESULT Fbx::Load(std::string fileName)
{
	// FBXの読み込み
	pFbxManager_ = FbxManager::Create();
	pFbxScene_ = FbxScene::Create(pFbxManager_, "fbxscene");
	FbxString FileName(fileName.c_str());
	FbxImporter *fbxImporter = FbxImporter::Create(pFbxManager_, "imp");
	
	if (!fbxImporter->Initialize(FileName.Buffer(), -1, pFbxManager_->GetIOSettings()))
	{
		//失敗
		return E_FAIL;
	}

	fbxImporter->Import(pFbxScene_);
	fbxImporter->Destroy();

	// FBX座標系（右手Y-up）→ DirectX座標系（左手Y-up）へ一括変換
	FbxAxisSystem::DirectX.DeepConvertScene(pFbxScene_);

	FbxGeometryConverter geometryConverter(pFbxManager_);
	//geometryConverter.Triangulate(pFbxScene_, true);
	//geometryConverter.RemoveBadPolygonsFromMeshes(pFbxScene_);

#pragma region SplitMesh
	//geometryConverter.SplitMeshesPerMaterial(pFbxScene_, true);

	//meshCount = pFbxScene_->GetSrcObjectCount<FbxMesh>();
	//matCount = pFbxScene_->GetSrcObjectCount<FbxSurfacePhong>();
	//std::vector<FbxSurfacePhong*> matlist;
	//for (int i = 0; i < matCount; i++)
	//{
	//		FbxSurfaceMaterial* pMaterial = pFbxScene_->GetSrcObject<FbxSurfaceMaterial>(i);
	//		FbxSurfacePhong* pPhong = (FbxSurfacePhong*)pMaterial;
	//		matlist.push_back(pPhong);
	//}
	//std::vector<FbxMesh*> meshlist;
	//for (int i = 0; i < meshCount; i++)
	//{
	//	FbxMesh* pMesh = pFbxScene_->GetSrcObject<FbxMesh>(i);
	//	pMesh->GetNode();
	//	meshlist.push_back(pMesh);
	//}
#pragma endregion SplitMesh


	// アニメーションのタイムモードの取得
	_frameRate = pFbxScene_->GetGlobalSettings().GetTimeMode();

	//現在のカレントディレクトリを覚えておく
	char defaultCurrentDir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, defaultCurrentDir);

	//カレントディレクトリをファイルがあった場所に変更
	char dir[MAX_PATH];
	_splitpath_s(fileName.c_str(), nullptr, 0, dir, MAX_PATH, nullptr, 0, nullptr, 0);
	SetCurrentDirectory(dir);

	//ルートノードを取得して
	//FbxNode* rootNode = pFbxScene_->GetRootNode();

	////そいつの子供の数を調べて
	//int childCount = rootNode->GetChildCount();

	////1個ずつチェック
	//for (int i = 0; childCount > i; i++)
	//{
	//	CheckNode(rootNode->GetChild(i), &parts_);
	//}

	//pFbxScene_->GetSrcObjectCount<FbxSurfacePhong>();
	//std::vector<FbxMesh*> meshList;
	//
	int meshCount = pFbxScene_->GetSrcObjectCount<FbxMesh>();
	for (int i = 0; i < meshCount; ++i)
	{
		// <たったこれだけで全てのメッシュデータを取得できる>
		FbxMesh* mesh = pFbxScene_->GetSrcObject<FbxMesh>(i);
		//パーツを用意
		FbxParts* pParts = new FbxParts(this);
		pParts->Init(mesh);

		//パーツ情報を動的配列に追加
		parts_.push_back(pParts);
	
	}
	//	std::string name = mesh->GetName();
	//	//m_fbxMeshNames.push_back(name);
	//	//m_fbxMeshes.insert({ mesh, name });
	//	meshList.push_back(mesh);
	//}

	//カレントディレクトリを元の位置に戻す
	SetCurrentDirectory(defaultCurrentDir);

	return S_OK;
}

void Fbx::CheckNode(FbxNode * pNode, std::vector<FbxParts*>* pPartsList)
{
	//そのノードにはメッシュ情報が入っているだろうか？
	FbxNodeAttribute* attr = pNode->GetNodeAttribute();
	if (attr != nullptr && attr->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		//パーツを用意
		FbxParts* pParts = new FbxParts(this);
		pParts->Init(pNode);

		//パーツ情報を動的配列に追加
		pPartsList->push_back(pParts);
	}

	//子ノードにもデータがあるかも！！
	{
		//子供の数を調べて
		int childCount = pNode->GetChildCount();

		//一人ずつチェック
		for (int i = 0; i < childCount; i++)
		{
			CheckNode(pNode->GetChild(i), pPartsList);
		}
	}
}

void Fbx::Release()
{
}

XMFLOAT3 Fbx::GetBonePosition(std::string boneName)
{
	XMFLOAT3 position = XMFLOAT3(0, 0, 0);
	for (int i = 0; i < parts_.size(); i++)
	{
		if (parts_[i]->GetBonePosition(boneName, &position))
			break;
	}
	return position;
}

XMFLOAT3 Fbx::GetAnimBonePosition(std::string boneName)
{
	XMFLOAT3 position = XMFLOAT3(0, 0, 0);
	for (int i = 0; i < parts_.size(); i++)
	{
		if (parts_[i]->GetBonePositionAtNow(boneName, &position))
			break;
	}
	return position;
}


void Fbx::Draw(Transform& transform, int frame)
{
	Direct3D::SetBlendMode(Direct3D::BLEND_DEFAULT);

	//パーツを1個ずつ描画
	for (int k = 0; k < parts_.size(); k++)
	{
		// 相対フレームを絶対フレームに変換
		FbxTime     time;
		time.SetTime(0, 0, 0, frame + _startFrame, 0, 0, _frameRate);

		//スキンアニメーション（ボーン有り）の場合
		if (parts_[k]->GetSkinInfo() != nullptr)
		{
			parts_[k]->DrawSkinAnime(transform, time);
		}

		//メッシュアニメーションの場合
		else
		{
			parts_[k]->DrawMeshAnime(transform, time, pFbxScene_);
		}
	}
}


//レイキャスト（レイを飛ばして当たり判定）
void Fbx::RayCast(RayCastData * data)
{
	//すべてのパーツと判定
	for (int i = 0; i < parts_.size(); i++)
	{
		parts_[i]->RayCast(data);
	}
}


// アニメーションスタックの総数を返す
int Fbx::GetAnimStackCount()
{
return pFbxScene_->GetSrcObjectCount<FbxAnimStack>();
}

// 現在のスタックインデックスを返す
int Fbx::GetCurrentAnimStack()
{
return _currentAnimStack;
}

// スタックを切り替え、開始・終了フレームを更新する
void Fbx::SetAnimStack(int index)
{
if (index < 0 || index >= GetAnimStackCount()) return;

FbxAnimStack* pAnimStack = pFbxScene_->GetSrcObject<FbxAnimStack>(index);
if (pAnimStack == nullptr) return;

pFbxScene_->SetCurrentAnimationStack(pAnimStack);

	if (pFbxScene_->GetAnimationEvaluator() != nullptr)
	{
		pFbxScene_->GetAnimationEvaluator()->Reset();
	}

	_currentAnimStack = index;

	// LocalTimeSpan と ReferenceTimeSpan の長い方を使う
	FbxTimeSpan localSpan = pAnimStack->GetLocalTimeSpan();
	FbxTimeSpan refSpan   = pAnimStack->GetReferenceTimeSpan();
	FbxTimeSpan span = (localSpan.GetDuration() >= refSpan.GetDuration()) ? localSpan : refSpan;

	_startFrame = (int)span.GetStart().GetFrameCount(_frameRate);
	_endFrame   = (int)span.GetStop() .GetFrameCount(_frameRate);
}

// 開始フレームを返す
int Fbx::GetStartFrame()
{
return _startFrame;
}

// 終了フレームを返す
int Fbx::GetEndFrame()
{
return _endFrame;
}
