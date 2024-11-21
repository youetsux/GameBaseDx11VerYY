#include "Fbx.h"
#include "Direct3D.h"
#include "FbxParts.h"


#pragma comment(lib, "LibFbxSDK-MT.lib")
#pragma comment(lib, "LibXml2-MT.lib")
#pragma comment(lib, "zlib-MT.lib")

Fbx::Fbx():_animSpeed(0)
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
		// その瞬間の自分の姿勢行列を得る
		FbxTime     time;
		time.SetTime(0, 0, 0, frame, 0, 0, _frameRate);

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
