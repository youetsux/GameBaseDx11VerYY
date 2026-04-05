#include "Fbx.h"
#include "Direct3D.h"
#include "FbxParts.h"
#include "Debug.h"
#include <Windows.h>


#pragma comment(lib, "LibFbxSDK-MT.lib")
#pragma comment(lib, "LibXml2-MT.lib")
#pragma comment(lib, "zlib-MT.lib")

Fbx::Fbx():_animSpeed(0), pFbxManager_(nullptr), pFbxScene_(nullptr), pAnimEvaluator_(nullptr)
{
}

Fbx::~Fbx()
{
	for (int i = 0; i < parts_.size(); i++)
	{
		delete parts_[i];
	}
	parts_.clear();

	// pFbxScene_ は Load 末尾で Destroy 済み（nullptr になっている）
	if (pFbxScene_)
	{
		pFbxScene_->Destroy();
		pFbxScene_ = nullptr;
	}
	if (pFbxManager_)
	{
		pFbxManager_->Destroy();
		pFbxManager_ = nullptr;
	}
}

HRESULT Fbx::Load(std::string fileName)
{
	// FBXの読み込み
	pFbxManager_ = FbxManager::Create();
	pFbxScene_ = FbxScene::Create(pFbxManager_, "fbxscene");

	// Shift-JIS パスを UTF-8 に変換して FBX SDK へ渡す
	// （パスに日本語が含まれる場合も正しく開けるようにする）
	std::string utf8FileName;
	{
		int wlen = MultiByteToWideChar(CP_ACP, 0, fileName.c_str(), -1, nullptr, 0);
		std::wstring wstr(wlen, L'\0');
		MultiByteToWideChar(CP_ACP, 0, fileName.c_str(), -1, &wstr[0], wlen);
		int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
		utf8FileName.resize(ulen);
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8FileName[0], ulen, nullptr, nullptr);
	}

	FbxImporter *fbxImporter = FbxImporter::Create(pFbxManager_, "imp");

	if (!fbxImporter->Initialize(utf8FileName.c_str(), -1, pFbxManager_->GetIOSettings()))
	{
		//失敗
		return E_FAIL;
	}

	fbxImporter->Import(pFbxScene_);
	fbxImporter->Destroy();

	// ① DeepConvertScene 前の軸系をログ
	{
		FbxAxisSystem axis = pFbxScene_->GetGlobalSettings().GetAxisSystem();
		int upSign, frontSign;
		FbxAxisSystem::EUpVector up = axis.GetUpVector(upSign);
		FbxAxisSystem::EFrontVector front = axis.GetFrontVector(frontSign);
		char buf[256];
		sprintf_s(buf, "[AxisSystem BEFORE] UpVector=%d(sign=%d) FrontVector=%d(sign=%d)", (int)up, upSign, (int)front, frontSign);
		Debug::Log(buf, true);
	}

	// ② DeepConvertScene 前の EvaluateGlobalTransform をログ
	{
		FbxNode* root = pFbxScene_->GetRootNode();
		if (root && root->GetChildCount() > 0)
		{
			FbxNode* node = root->GetChild(0);
			FbxAMatrix m = node->EvaluateGlobalTransform();
			FbxVector4 t = m.GetT();
			FbxVector4 s = m.GetS();
			FbxVector4 r = m.GetR();
			char buf[256];
			sprintf_s(buf, "[EvaluateGlobal BEFORE] node=%s T=(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f) S=(%.2f,%.2f,%.2f)",
				node->GetName(),
				(float)t[0], (float)t[1], (float)t[2],
				(float)r[0], (float)r[1], (float)r[2],
				(float)s[0], (float)s[1], (float)s[2]);
			Debug::Log(buf, true);
		}
	}

	// 座標系をDirectX左手系Y-upに変換する（Maya/Blender共通）
	{
		FbxAxisSystem targetAxis(
			FbxAxisSystem::eYAxis,
			FbxAxisSystem::eParityOdd,
			FbxAxisSystem::eLeftHanded);
		targetAxis.DeepConvertScene(pFbxScene_);
	}

	// ① DeepConvertScene 後の軸系をログ
	{
		FbxAxisSystem axis = pFbxScene_->GetGlobalSettings().GetAxisSystem();
		int upSign, frontSign;
		FbxAxisSystem::EUpVector up = axis.GetUpVector(upSign);
		FbxAxisSystem::EFrontVector front = axis.GetFrontVector(frontSign);
		char buf[256];
		sprintf_s(buf, "[AxisSystem AFTER] UpVector=%d(sign=%d) FrontVector=%d(sign=%d)", (int)up, upSign, (int)front, frontSign);
		Debug::Log(buf, true);
	}

	// ② DeepConvertScene 後の EvaluateGlobalTransform をログ
	{
		FbxNode* root = pFbxScene_->GetRootNode();
		if (root && root->GetChildCount() > 0)
		{
			FbxNode* node = root->GetChild(0);
			FbxAMatrix m = node->EvaluateGlobalTransform();
			FbxVector4 t = m.GetT();
			FbxVector4 s = m.GetS();
			FbxVector4 r = m.GetR();
			char buf[256];
			sprintf_s(buf, "[EvaluateGlobal AFTER] node=%s T=(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f) S=(%.2f,%.2f,%.2f)",
				node->GetName(),
				(float)t[0], (float)t[1], (float)t[2],
				(float)r[0], (float)r[1], (float)r[2],
				(float)s[0], (float)s[1], (float)s[2]);
			Debug::Log(buf, true);
		}
	}

	// 座標系変換後に三角化する
	{
		bool needTriangulate = false;
		int meshCount = pFbxScene_->GetSrcObjectCount<FbxMesh>();
		for (int i = 0; i < meshCount && !needTriangulate; i++)
		{
			FbxMesh* mesh = pFbxScene_->GetSrcObject<FbxMesh>(i);
			for (int j = 0; j < mesh->GetPolygonCount(); j++)
			{
				if (mesh->GetPolygonSize(j) != 3)
				{
					needTriangulate = true;
					break;
				}
			}
		}
		if (needTriangulate)
		{
			FbxGeometryConverter converter(pFbxManager_);
			converter.Triangulate(pFbxScene_, true);
			Debug::Log("[Fbx] Non-triangle polygons detected. Auto-triangulated.", true);
		}
	}
	
	
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

	//現在のカレントディレクトリを覚えておく（ワイド文字版で日本語パス対応）
	wchar_t defaultCurrentDir[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, defaultCurrentDir);

	//カレントディレクトリをファイルがあった場所に変更
	{
		int wlen = MultiByteToWideChar(CP_ACP, 0, fileName.c_str(), -1, nullptr, 0);
		std::wstring wFileName(wlen, L'\0');
		MultiByteToWideChar(CP_ACP, 0, fileName.c_str(), -1, &wFileName[0], wlen);
		wchar_t wdir[MAX_PATH];
		_wsplitpath_s(wFileName.c_str(), nullptr, 0, wdir, MAX_PATH, nullptr, 0, nullptr, 0);
		SetCurrentDirectoryW(wdir);
	}

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
	SetCurrentDirectoryW(defaultCurrentDir);

	// 全パーツの頂点からAABBを計算
	CalcAABB();

	// AnimStackを明示的にセットしてからevaluatorを取り出す
	{
		FbxAnimStack* stack = pFbxScene_->GetSrcObject<FbxAnimStack>(0);
		if (stack)
			pFbxScene_->SetCurrentAnimationStack(stack);
	}
	pAnimEvaluator_ = pFbxScene_->GetAnimationEvaluator();

	// pFbxScene_ は ppCluster_ 等 FBX SDK オブジェクトが生きている限り
	// Destroy できない。デストラクタで pFbxManager_ と一緒に解放する。

	return S_OK;
}

// 全パーツのAABBをマージして全体のAABBを計算
void Fbx::CalcAABB()
{
	aabb_ = AABB();
	for (int i = 0; i < (int)parts_.size(); i++)
	{
		const AABB& pa = parts_[i]->GetAABB();
		if (pa.min_.x < aabb_.min_.x) aabb_.min_.x = pa.min_.x;
		if (pa.min_.y < aabb_.min_.y) aabb_.min_.y = pa.min_.y;
		if (pa.min_.z < aabb_.min_.z) aabb_.min_.z = pa.min_.z;
		if (pa.max_.x > aabb_.max_.x) aabb_.max_.x = pa.max_.x;
		if (pa.max_.y > aabb_.max_.y) aabb_.max_.y = pa.max_.y;
		if (pa.max_.z > aabb_.max_.z) aabb_.max_.z = pa.max_.z;
	}
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
			parts_[k]->DrawMeshAnime(transform, time);
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
