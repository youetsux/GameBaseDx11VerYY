#include "FbxParts.h"
#include "Fbx.h"
#include "Global.h"
#include "Direct3D.h"
#include "Camera.h"
#include "Debug.h"
#include <cassert>
#include <filesystem>
#include <vector>

//コンストラクタ
FbxParts::FbxParts() :
	ppIndexBuffer_(nullptr), pMaterial_(nullptr),
	pVertexBuffer_(nullptr), pConstantBuffer_(nullptr),
	pVertexData_(nullptr), ppIndexData_(nullptr),
	pSkinInfo_(nullptr), ppCluster_(nullptr), numBone_(0),
	pBoneArray_(nullptr), pWeightArray_(nullptr)
{
}

//コンストラクタ
FbxParts::FbxParts(Fbx *parent) :
	ppIndexBuffer_(nullptr), pMaterial_(nullptr),
	pVertexBuffer_(nullptr), pConstantBuffer_(nullptr),
	pVertexData_(nullptr), ppIndexData_(nullptr),
	pSkinInfo_(nullptr), ppCluster_(nullptr), numBone_(0),
	pBoneArray_(nullptr), pWeightArray_(nullptr)
{
	parent_ = parent;
}


//デストラクタ
FbxParts::~FbxParts()
{
	SAFE_DELETE_ARRAY(pBoneArray_);
	SAFE_DELETE_ARRAY(ppCluster_);

	if (pWeightArray_ != nullptr)
	{
		for (DWORD i = 0; i < vertexCount_; i++)
		{
			SAFE_DELETE_ARRAY(pWeightArray_[i].pBoneIndex);
			SAFE_DELETE_ARRAY(pWeightArray_[i].pBoneWeight);
		}
		SAFE_DELETE_ARRAY(pWeightArray_);
	}



	SAFE_DELETE_ARRAY(pVertexData_);
	for (DWORD i = 0; i < materialCount_; i++)
	{
		SAFE_RELEASE(ppIndexBuffer_[i]);
		SAFE_DELETE(ppIndexData_[i]);
		SAFE_DELETE(pMaterial_[i].pTexture);

	}
	SAFE_DELETE_ARRAY(ppIndexBuffer_);
	SAFE_DELETE_ARRAY(ppIndexData_);
	SAFE_DELETE_ARRAY(pMaterial_);

	SAFE_RELEASE(pVertexBuffer_);
	SAFE_RELEASE(pConstantBuffer_);
}

//FBXファイルから情報をロードして諸々準備する
HRESULT FbxParts::Init(FbxNode* pNode)
{
	//ノードからメッシュの情報を取得

	FbxMesh* mesh = pNode->GetMesh();
	
	
	mesh->SplitPoints(FbxLayerElement::eTextureDiffuse);

	//各情報の個数を取得
	vertexCount_ = mesh->GetControlPointsCount();			//頂点の数
	polygonCount_ = mesh->GetPolygonCount();				//ポリゴンの数
	polygonVertexCount_ = mesh->GetPolygonVertexCount();	//ポリゴン頂点インデックス数 

	InitVertex(mesh);		//頂点バッファ準備
	InitMaterial(pNode);	//マテリアル準備
	InitIndex(mesh);		//インデックスバッファ準備
	InitSkelton(mesh);		//骨の情報を準備
	IntConstantBuffer();	//コンスタントバッファ（シェーダーに情報を送るやつ）準備

	return S_OK;
}

HRESULT FbxParts::Init(fbxsdk::FbxMesh* pMesh)
{
	//メッシュの情報を取得 

	//三角化チェック：モデラー側で必ず三角化すること
	{
		int polyCount = pMesh->GetPolygonCount();
		for (int i = 0; i < polyCount; i++)
		{
			if (pMesh->GetPolygonSize(i) != 3)
			{
				Debug::Log(std::string("[FBX ERROR] 非三角形ポリゴンが含まれています。三角化してエクスポートしてください。 mesh: ") + pMesh->GetName(), true);
				assert(false && "FBX mesh is not triangulated. Export with triangulation enabled.");
				return E_FAIL;
			}
		}
	}

	//メッシュのコントロールポイントを、マテリアルをベースに分割する
	// スキンメッシュ（デフォーマあり）は SplitPoints を呼ぶとクラスターの
	// CP インデックスとズレるため呼ばない
	bool hasSkin = (pMesh->GetDeformer(0) != nullptr);
	if (!hasSkin)
	{
		pMesh->SplitPoints(FbxLayerElement::eTextureDiffuse);
	}

	vertexCount_ = pMesh->GetControlPointsCount();			//頂点の数
	polygonCount_ = pMesh->GetPolygonCount();				//ポリゴンの数
	polygonVertexCount_ = pMesh->GetPolygonVertexCount();	//ポリゴン頂点インデックス数 

	InitVertex(pMesh);		//頂点バッファ準備
	InitMaterial(pMesh);	//マテリアル準備
	InitIndex(pMesh);		//インデックスバッファ準備
	InitSkelton(pMesh);		//骨の情報を準備
	IntConstantBuffer();	//コンスタントバッファ（シェーダーに情報を送るやつ）準備

	// 頂点データからAABBを計算
	// スキンメッシュ（アニメあり）は初期ポーズの頂点座標から計算
	// 静止メッシュはノードのグローバル行列（Scale含む）を適用
	{
		XMMATRIX world = XMMatrixIdentity();
		if (numBone_ == 0)
		{
			FbxAMatrix globalMatrix = pMesh->GetNode()->EvaluateGlobalTransform();

			// ② 頂点[0]のローカル座標とワールド座標を比較
			{
				XMFLOAT3 localPos = pVertexData_[0].position;
				XMFLOAT4X4 gm;
				for (int x = 0; x < 4; x++)
					for (int y = 0; y < 4; y++)
						gm(x, y) = (float)globalMatrix.Get(x, y);
				XMMATRIX world = XMLoadFloat4x4(&gm);
				XMFLOAT3 worldPos;
				XMStoreFloat3(&worldPos, XMVector3TransformCoord(XMLoadFloat3(&localPos), world));
				FbxVector4 s = globalMatrix.GetS();
				FbxVector4 r = globalMatrix.GetR();
				char buf[256];
				sprintf_s(buf, "[AABB CHECK] vertex[0] local=(%.3f,%.3f,%.3f) world=(%.3f,%.3f,%.3f) S=(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f)",
					localPos.x, localPos.y, localPos.z,
					worldPos.x, worldPos.y, worldPos.z,
					(float)s[0], (float)s[1], (float)s[2],
					(float)r[0], (float)r[1], (float)r[2]);
				Debug::Log(buf, true);
			}

			// ④ 手動Z反転が別途入っていないか確認
			Debug::Log("[AABB CHECK] No manual Z-flip. Vertices converted by DeepConvertScene only.", true);

			XMFLOAT4X4 gm;
			for (int x = 0; x < 4; x++)
				for (int y = 0; y < 4; y++)
					gm(x, y) = (float)globalMatrix.Get(x, y);
			XMMATRIX world = XMLoadFloat4x4(&gm);

			for (DWORD i = 0; i < vertexCount_; i++)
			{
				XMVECTOR v = XMLoadFloat3(&pVertexData_[i].position);
				XMFLOAT3 p;
				XMStoreFloat3(&p, XMVector3TransformCoord(v, world));
				if (p.x < aabb_.min_.x) aabb_.min_.x = p.x;
				if (p.y < aabb_.min_.y) aabb_.min_.y = p.y;
				if (p.z < aabb_.min_.z) aabb_.min_.z = p.z;
				if (p.x > aabb_.max_.x) aabb_.max_.x = p.x;
				if (p.y > aabb_.max_.y) aabb_.max_.y = p.y;
				if (p.z > aabb_.max_.z) aabb_.max_.z = p.z;
			}
		}
		else
		{
			// スキンメッシュ：初期ポーズ（posOrigin）からAABBを計算
			for (DWORD i = 0; i < vertexCount_; i++)
			{
				const XMFLOAT3& p = pWeightArray_[i].posOrigin;
				if (p.x < aabb_.min_.x) aabb_.min_.x = p.x;
				if (p.y < aabb_.min_.y) aabb_.min_.y = p.y;
				if (p.z < aabb_.min_.z) aabb_.min_.z = p.z;
				if (p.x > aabb_.max_.x) aabb_.max_.x = p.x;
				if (p.y > aabb_.max_.y) aabb_.max_.y = p.y;
				if (p.z > aabb_.max_.z) aabb_.max_.z = p.z;
			}
		}
	}

	return S_OK;
}


//頂点バッファ準備
void FbxParts::InitVertex(fbxsdk::FbxMesh* mesh)
{
	// polygon vertex 展開済みサイズに上書き
	vertexCount_ = polygonCount_ * 3;
	pVertexData_ = new VERTEX[vertexCount_];

	// UV レイヤー取得（null ガード）
	FbxLayer* layer0 = mesh->GetLayer(0);
	FbxLayerElementUV* pUV = (layer0 != nullptr) ? layer0->GetUVs() : nullptr;

	FbxStringList uvSetNames;
	FbxString uvSetName;
	if (pUV != nullptr)
	{
		mesh->GetUVSetNames(uvSetNames);
		uvSetName = uvSetNames.GetStringAt(0);
	}

	for (DWORD poly = 0; poly < polygonCount_; poly++)
	{
		for (int vertex = 0; vertex < 3; vertex++)
		{
			int linearIndex = (int)(poly * 3 + vertex);
			int cpIndex = mesh->GetPolygonVertex(poly, vertex);	// 位置取得にだけ使う

			// 位置
			FbxVector4 pos = mesh->GetControlPointAt(cpIndex);
			pVertexData_[linearIndex].position = XMFLOAT3((float)pos[0], (float)pos[1], (float)pos[2]);

			// 法線
			FbxVector4 normal;
			mesh->GetPolygonVertexNormal(poly, vertex, normal);
			pVertexData_[linearIndex].normal = XMFLOAT3((float)normal[0], (float)normal[1], (float)normal[2]);

			// UV（UV レイヤーがない場合は 0 埋め）
			FbxVector2 uv(0.0, 0.0);
			if (pUV != nullptr)
			{
				if (pUV->GetMappingMode() == FbxLayerElement::eByPolygonVertex)
					{
						bool unmapped = false;
						mesh->GetPolygonVertexUV(poly, vertex, uvSetName, uv, unmapped);
					}
				else // eByControlPoint
				{
					if (pUV->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
					{
						int uvIndex = pUV->GetIndexArray().GetAt(cpIndex);
						if (uvIndex >= 0)
							uv = pUV->GetDirectArray().GetAt(uvIndex);
					}
					else // eDirect
					{
						uv = pUV->GetDirectArray().GetAt(cpIndex);
					}
				}
			}
			pVertexData_[linearIndex].uv = { (float)uv[0], (float)(1.0 - uv[1]), 0.0f };
		}
	}

	// 頂点データ用バッファの設定
	D3D11_BUFFER_DESC bd_vertex;
	bd_vertex.ByteWidth = sizeof(VERTEX) * vertexCount_;
	bd_vertex.Usage = D3D11_USAGE_DYNAMIC;
	bd_vertex.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd_vertex.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd_vertex.MiscFlags = 0;
	bd_vertex.StructureByteStride = 0;
	D3D11_SUBRESOURCE_DATA data_vertex;
	data_vertex.pSysMem = pVertexData_;
	Direct3D::pDevice_->CreateBuffer(&bd_vertex, &data_vertex, &pVertexBuffer_);
}

//マテリアル準備
void FbxParts::InitMaterial(fbxsdk::FbxNode* pNode)
{

	// マテリアルバッファの生成
	materialCount_ = pNode->GetMaterialCount();
	pMaterial_ = new MATERIAL[materialCount_];

	for (DWORD i = 0; i < materialCount_; i++)
	{
		ZeroMemory(&pMaterial_[i], sizeof(pMaterial_[i]));

		// マテリアルの種類（Phong / Lambert）に応じて安全に取得する
		FbxSurfaceMaterial* pMaterial = pNode->GetMaterial(i);

		FbxDouble3 ambient  = FbxDouble3(0, 0, 0);
		FbxDouble3 diffuse  = FbxDouble3(0, 0, 0);
		FbxDouble3 specular = FbxDouble3(0, 0, 0);

		if (pMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* pPhong = static_cast<FbxSurfacePhong*>(pMaterial);
			ambient  = pPhong->Ambient;
			diffuse  = pPhong->Diffuse;
			specular = pPhong->Specular;
			pMaterial_[i].shininess = (float)pPhong->Shininess;
		}
		else if (pMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* pLambert = static_cast<FbxSurfaceLambert*>(pMaterial);
			ambient = pLambert->Ambient;
			diffuse = pLambert->Diffuse;
			// Lambert には Specular / Shininess がないので 0 固定
		}

		pMaterial_[i].ambient   = XMFLOAT4((float)ambient[0],  (float)ambient[1],  (float)ambient[2],  0.0f);
		pMaterial_[i].diffuse   = XMFLOAT4((float)diffuse[0],  (float)diffuse[1],  (float)diffuse[2],  1.0f);
		pMaterial_[i].specular  = XMFLOAT4((float)specular[0], (float)specular[1], (float)specular[2], 1.0f);

		InitTexture(pMaterial, i);
	}

}

void FbxParts::InitMaterial(fbxsdk::FbxMesh* pMesh)
{
	// マテリアルバッファの生成
	materialCount_ = pMesh->GetNode()->GetMaterialCount();
	pMaterial_ = new MATERIAL[materialCount_];

	for (DWORD i = 0; i < materialCount_; i++)
	{
		ZeroMemory(&pMaterial_[i], sizeof(pMaterial_[i]));

		// マテリアルの種類（Phong / Lambert）に応じて安全に取得する
		FbxSurfaceMaterial* pMaterial = pMesh->GetNode()->GetMaterial(i);

		FbxDouble3 ambient  = FbxDouble3(0, 0, 0);
		FbxDouble3 diffuse  = FbxDouble3(0, 0, 0);
		FbxDouble3 specular = FbxDouble3(0, 0, 0);

		if (pMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* pPhong = static_cast<FbxSurfacePhong*>(pMaterial);
			ambient  = pPhong->Ambient;
			diffuse  = pPhong->Diffuse;
			specular = pPhong->Specular;
			pMaterial_[i].shininess = (float)pPhong->Shininess;
		}
		else if (pMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* pLambert = static_cast<FbxSurfaceLambert*>(pMaterial);
			ambient = pLambert->Ambient;
			diffuse = pLambert->Diffuse;
			// Lambert には Specular / Shininess がないので 0 固定
		}

		pMaterial_[i].ambient   = XMFLOAT4((float)ambient[0],  (float)ambient[1],  (float)ambient[2],  0.0f);
		pMaterial_[i].diffuse   = XMFLOAT4((float)diffuse[0],  (float)diffuse[1],  (float)diffuse[2],  1.0f);
		pMaterial_[i].specular  = XMFLOAT4((float)specular[0], (float)specular[1], (float)specular[2], 1.0f);

		InitTexture(pMaterial, i);
	}
}

//テクスチャ準備
void FbxParts::InitTexture(fbxsdk::FbxSurfaceMaterial* pMaterial, const DWORD& i)
{
	pMaterial_[i].pTexture = nullptr;

	// テクスチャー情報の取得
	FbxProperty  lProperty = pMaterial->FindProperty(FbxSurfaceMaterial::sDiffuse);

	//テクスチャの数
	int fileTextureCount = lProperty.GetSrcObjectCount<FbxFileTexture>();

	if (fileTextureCount > 0)
	{
		FbxFileTexture* texture = lProperty.GetSrcObject<FbxFileTexture>(0);

		// 相対パスが空の場合（Blender等）は絶対パスにフォールバック
		const char* relPath = texture->GetRelativeFileName();
		const char* absPath = texture->GetFileName();
		const char* texPath = (relPath && relPath[0] != '\0') ? relPath : absPath;

		if (texPath && texPath[0] != '\0')
		{
			// ファイル名+拡張子だけを取り出す
			std::string filename = std::filesystem::path(texPath).filename().string();
			pMaterial_[i].pTexture = new Texture;
			pMaterial_[i].pTexture->Load(filename);
		}
	}
}

//インデックスバッファ準備
void FbxParts::InitIndex(fbxsdk::FbxMesh* mesh)
{
	// マテリアルの数だけインデックスバッファーを作成
	ppIndexBuffer_ = new ID3D11Buffer * [materialCount_];
	ppIndexData_ = new DWORD * [materialCount_];

	FbxLayerElementMaterial* mtl = mesh->GetLayer(0)->GetMaterials();

	for (DWORD i = 0; i < materialCount_; i++)
	{
		int count = 0;
		DWORD* pIndex = new DWORD[polygonCount_ * 3];

		// linearIndex = poly * 3 + vertex が頂点の実体なのでそのまま詰める
		for (DWORD j = 0; j < polygonCount_; j++)
		{
			int mtlId = mtl->GetIndexArray().GetAt(j);
			if (mtlId == (int)i)
			{
				pIndex[count++] = j * 3 + 0;
				pIndex[count++] = j * 3 + 1;
				pIndex[count++] = j * 3 + 2;
			}
		}

		// インデックスバッファを生成する
		D3D11_BUFFER_DESC   bd;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(DWORD) * count;
		bd.BindFlags = D3D10_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = 0;
		bd.MiscFlags = 0;

		D3D11_SUBRESOURCE_DATA InitData;
		InitData.pSysMem = pIndex;
		InitData.SysMemPitch = 0;
		InitData.SysMemSlicePitch = 0;
		if (FAILED(Direct3D::pDevice_->CreateBuffer(&bd, &InitData, &ppIndexBuffer_[i])))
		{
			//MessageBox(0, "インデックスバッファの生成に失敗", fbxFileName, MB_OK);
		}
		pMaterial_[i].polygonCount = count / 3;
		ppIndexData_[i] = new DWORD[count];
		memcpy(ppIndexData_[i], pIndex, sizeof(DWORD) * count);
		SAFE_DELETE_ARRAY(pIndex);
	}
}

//骨の情報を準備
void FbxParts::InitSkelton(FbxMesh* pMesh)
{
	// デフォーマ情報（ボーンとモデルの関連付け）の取得
	FbxDeformer* pDeformer = pMesh->GetDeformer(0);
	if (pDeformer == nullptr)
	{
		//ボーン情報なし
		return;
	}

	// デフォーマ情報からスキンメッシュ情報を取得
	pSkinInfo_ = (FbxSkin*)pDeformer;

	// 頂点からポリゴンを逆引きするための情報を作成する
	struct  POLY_INDEX
	{
		int* polyIndex;      // ポリゴンの番号
		int* vertexIndex;    // 頂点の番号
		int     numRef;         // 頂点を共有するポリゴンの数
	};

#pragma region MeshInfo
	//POLY_INDEX* polyTable = new POLY_INDEX[vertexCount_];
	//for (DWORD i = 0; i < vertexCount_; i++)
	//{
	//	// 三角形ポリゴンに合わせて、頂点とポリゴンの関連情報を構築する
	//	// 総頂点数＝ポリゴン数×３頂点
	//	polyTable[i].polyIndex = new int[polygonCount_ * 3];
	//	polyTable[i].vertexIndex = new int[polygonCount_ * 3];
	//	polyTable[i].numRef = 0;
	//	ZeroMemory(polyTable[i].polyIndex, sizeof(int) * polygonCount_ * 3);
	//	ZeroMemory(polyTable[i].vertexIndex, sizeof(int) * polygonCount_ * 3);

	//	// ポリゴン間で共有する頂点を列挙する
	//	for (DWORD k = 0; k < polygonCount_; k++)
	//	{
	//		for (int m = 0; m < 3; m++)
	//		{
	//			if (pMesh->GetPolygonVertex(k, m) == i)
	//			{
	//				polyTable[i].polyIndex[polyTable[i].numRef] = k;
	//				polyTable[i].vertexIndex[polyTable[i].numRef] = m;
	//				polyTable[i].numRef++;
	//			}
	//		}
	//	}
	//}
#pragma endregion MeshInfo

	// ボーン情報を取得する
	numBone_ = pSkinInfo_->GetClusterCount();
	ppCluster_ = new FbxCluster * [numBone_];
	for (int i = 0; i < numBone_; i++)
	{
		ppCluster_[i] = pSkinInfo_->GetCluster(i);
	}

	// ボーンの数に合わせてウェイト情報を準備する
	pWeightArray_ = new FbxParts::Weight[vertexCount_];
	for (DWORD i = 0; i < vertexCount_; i++)
	{
		pWeightArray_[i].posOrigin = pVertexData_[i].position;
		pWeightArray_[i].normalOrigin = pVertexData_[i].normal;
		pWeightArray_[i].pBoneIndex = new int[numBone_];
		pWeightArray_[i].pBoneWeight = new float[numBone_];
		for (int j = 0; j < numBone_; j++)
		{
			pWeightArray_[i].pBoneIndex[j] = -1;
			pWeightArray_[i].pBoneWeight[j] = 0.0f;
		}
	}

	// cp2linear テーブル: control point index → linearIndex のリスト
	int cpCount = pMesh->GetControlPointsCount();
	std::vector<std::vector<int>> cp2linear(cpCount);
	for (DWORD poly = 0; poly < polygonCount_; poly++)
	{
		for (int v = 0; v < 3; v++)
		{
			int cpIdx  = pMesh->GetPolygonVertex(poly, v);
			int linIdx = (int)(poly * 3 + v);
			cp2linear[cpIdx].push_back(linIdx);
		}
	}

	// それぞれのボーンに影響を受ける頂点を調べる
	// そこから逆に、頂点ベースでボーンインデックス・重みを整頓する
	for (int i = 0; i < numBone_; i++)
	{
		int numIndex = ppCluster_[i]->GetControlPointIndicesCount();   //このボーンに影響を受ける頂点数
		int* piIndex = ppCluster_[i]->GetControlPointIndices();       //ボーン/ウェイト情報の番号
		double* pdWeight = ppCluster_[i]->GetControlPointWeights();     //頂点ごとのウェイト情報

		//頂点側からインデックスをたどって、頂点サイドで整理する
		for (int k = 0; k < numIndex; k++)
		{
			int cpIdx = piIndex[k];
			for (int linIdx : cp2linear[cpIdx])
			{
				// 頂点に関連付けられたウェイト情報がボーン５本以上の場合は、重みの大きい順に４本に絞る
				for (int m = 0; m < 4; m++)
				{
					if (m >= numBone_)
						break;

					if (pdWeight[k] > pWeightArray_[linIdx].pBoneWeight[m])
					{
						for (int n = numBone_ - 1; n > m; n--)
						{
							pWeightArray_[linIdx].pBoneIndex[n] = pWeightArray_[linIdx].pBoneIndex[n - 1];
							pWeightArray_[linIdx].pBoneWeight[n] = pWeightArray_[linIdx].pBoneWeight[n - 1];
						}
						pWeightArray_[linIdx].pBoneIndex[m] = i;
						pWeightArray_[linIdx].pBoneWeight[m] = (float)pdWeight[k];
						break;
					}
				}
			}
		}
	}

	// ジオメトリオフセット行列をメンバに保存
	pSkinInfo_->GetCluster(0)->GetTransformMatrix(bindShapeMatrix_);

	//ボーンを生成
	pBoneArray_ = new FbxParts::Bone[numBone_];
	for (int i = 0; i < numBone_; i++)
	{
		// ボーンのバインドポーズ（linkMatrixのみ。bindShapeMatrixはDrawSkinAnime側で掛ける）
		FbxAMatrix  linkMatrix;
		ppCluster_[i]->GetTransformLinkMatrix(linkMatrix);

		// 行列コピー（Fbx形式からDirectXへの変換）
		XMFLOAT4X4 pose;
		for (DWORD x = 0; x < 4; x++)
		{
			for (DWORD y = 0; y < 4; y++)
			{
				pose(x, y) = (float)linkMatrix.Get(x, y);
			}
		}
		pBoneArray_[i].bindPose = XMLoadFloat4x4(&pose);
		bonePair[ppCluster_[i]->GetLink()->GetName()] = pBoneArray_ + i;
	}

	// 一時的なメモリ領域を解放する
	//for (DWORD i = 0; i < vertexCount_; i++)
	//{
	//	SAFE_DELETE_ARRAY(polyTable[i].polyIndex);
	//	SAFE_DELETE_ARRAY(polyTable[i].vertexIndex);
	//}
	//SAFE_DELETE_ARRAY(polyTable);

}

//コンスタントバッファ（シェーダーに情報を送るやつ）準備
void FbxParts::IntConstantBuffer()
{
	// 定数バッファの作成(パラメータ受け渡し用)
	D3D11_BUFFER_DESC cb;
	cb.ByteWidth = sizeof(CONSTANT_BUFFER);
	cb.Usage = D3D11_USAGE_DYNAMIC;
	cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cb.MiscFlags = 0;
	cb.StructureByteStride = 0;
	Direct3D::pDevice_->CreateBuffer(&cb, NULL, &pConstantBuffer_);
}

//描画
void FbxParts::Draw(Transform& transform)
{
	//今から描画する頂点情報をシェーダに伝える
	UINT stride = sizeof(VERTEX);
	UINT offset = 0;
	Direct3D::pContext_->IASetVertexBuffers(0, 1, &pVertexBuffer_, &stride, &offset);

	//使用するコンスタントバッファをシェーダに伝える
	Direct3D::pContext_->VSSetConstantBuffers(0, 1, &pConstantBuffer_);
	Direct3D::pContext_->PSSetConstantBuffers(0, 1, &pConstantBuffer_);


	//シェーダーのコンスタントバッファーに各種データを渡す
	for (DWORD i = 0; i < materialCount_; i++)
	{
		// インデックスバッファーをセット
		UINT    stride = sizeof(int);
		UINT    offset = 0;
		Direct3D::pContext_->IASetIndexBuffer(ppIndexBuffer_[i], DXGI_FORMAT_R32_UINT, 0);

		// パラメータの受け渡し
		D3D11_MAPPED_SUBRESOURCE pdata;
		CONSTANT_BUFFER cb;
		//XMMATRIX MSHADOW = XMMatrixShadow({ 0 ,0.01f ,0 ,1 }, {0,1,0,0});
		//cb.worldVewProj =	XMMatrixTranspose(transform.GetWorldMatrix() * Camera::GetViewMatrix() * Camera::GetProjectionMatrix());
		cb.worldVewProj = XMMatrixTranspose(transform.GetWorldMatrix() * Camera::GetViewMatrix() * Camera::GetProjectionMatrix());// リソースへ送る値をセット
		cb.world = XMMatrixTranspose(transform.GetWorldMatrix());

		cb.normalTrans = XMMatrixTranspose(transform.matRotate_ * XMMatrixInverse(nullptr, transform.matScale_));

		cb.ambient = pMaterial_[i].ambient;
		cb.ambient.x = max(cb.ambient.x, 0.35f);
		cb.ambient.y = max(cb.ambient.y, 0.35f);
		cb.ambient.z = max(cb.ambient.z, 0.35f);
		cb.diffuse = pMaterial_[i].diffuse;
		cb.speculer = pMaterial_[i].specular;
		cb.shininess = pMaterial_[i].shininess;
		cb.cameraPosition = XMFLOAT4(Camera::GetPosition().x, Camera::GetPosition().y, Camera::GetPosition().z, 0);
		cb.lightDirection = XMFLOAT4(-1, -1, 1, 0);
		cb.isTexture = pMaterial_[i].pTexture != nullptr;


		Direct3D::pContext_->Map(pConstantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &pdata);	// GPUからのリソースアクセスを一時止める
		memcpy_s(pdata.pData, pdata.RowPitch, (void*)(&cb), sizeof(cb));		// リソースへ値を送る


		// テクスチャをシェーダーに設定

		if (cb.isTexture)
		{
			ID3D11SamplerState* pSampler = pMaterial_[i].pTexture->GetSampler();
			Direct3D::pContext_->PSSetSamplers(0, 1, &pSampler);

			ID3D11ShaderResourceView* pSRV = pMaterial_[i].pTexture->GetSRV();
			Direct3D::pContext_->PSSetShaderResources(0, 1, &pSRV);
		}
		Direct3D::pContext_->Unmap(pConstantBuffer_, 0);									// GPUからのリソースアクセスを再開

		//ポリゴンメッシュを描画する
		Direct3D::pContext_->DrawIndexed(pMaterial_[i].polygonCount * 3, 0, 0);
	}

}

//ボーン有りのモデルを描画
void FbxParts::DrawSkinAnime(Transform& transform, FbxTime time)
{
	// ボーンごとの現在の行列を取得する
	for (int i = 0; i < numBone_; i++)
	{
		FbxAnimEvaluator* evaluator = parent_->pAnimEvaluator_;
		FbxMatrix mCurrentOrentation = evaluator->GetNodeGlobalTransform(ppCluster_[i]->GetLink(), time);

		// 行列コピー（Fbx形式からDirectXへの変換）
		XMFLOAT4X4 pose;
		for (DWORD x = 0; x < 4; x++)
		{
			for (DWORD y = 0; y < 4; y++)
			{
				pose(x, y) = (float)mCurrentOrentation.Get(x, y);
			}
		}

		// bindShapeMatrix を FbxAMatrix → XMMATRIX に変換
		XMFLOAT4X4 bsm;
		for (DWORD x = 0; x < 4; x++)
			for (DWORD y = 0; y < 4; y++)
				bsm(x, y) = (float)bindShapeMatrix_.Get(x, y);
		XMMATRIX bindShape = XMLoadFloat4x4(&bsm);

		pBoneArray_[i].newPose = XMLoadFloat4x4(&pose);
		XMMATRIX invBind = XMMatrixInverse(nullptr, pBoneArray_[i].bindPose);

		pBoneArray_[i].diffPose = bindShape * invBind * pBoneArray_[i].newPose;
	}

	// 各ボーンに対応した頂点の変形制御
	for (DWORD i = 0; i < vertexCount_; i++)
	{
		// 各頂点ごとに、「影響するボーン×ウェイト値」を反映させた関節行列を作成する
		XMMATRIX  matrix;
		ZeroMemory(&matrix, sizeof(matrix));
		for (int m = 0; m < numBone_; m++)
		{
			if (pWeightArray_[i].pBoneIndex[m] < 0)
			{
				break;
			}
			matrix += pBoneArray_[pWeightArray_[i].pBoneIndex[m]].diffPose * pWeightArray_[i].pBoneWeight[m];
		}

		// 作成された関節行列を使って、頂点を変形する
		XMVECTOR Pos = XMLoadFloat3(&pWeightArray_[i].posOrigin);
		XMVECTOR Normal = XMLoadFloat3(&pWeightArray_[i].normalOrigin);

		XMStoreFloat3(&pVertexData_[i].position, XMVector3TransformCoord(Pos, matrix));
		XMFLOAT3X3 mat33;
		XMStoreFloat3x3(&mat33, matrix);
		XMMATRIX matrix33 = XMLoadFloat3x3(&mat33);
		XMStoreFloat3(&pVertexData_[i].normal, XMVector3TransformCoord(Normal, matrix33));
	}

	// 頂点バッファをロックして、変形させた後の頂点情報で上書きする
	D3D11_MAPPED_SUBRESOURCE msr = {};
	Direct3D::pContext_->Map(pVertexBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (msr.pData)
	{
		memcpy_s(msr.pData, msr.RowPitch, pVertexData_, sizeof(VERTEX) * vertexCount_);
		Direct3D::pContext_->Unmap(pVertexBuffer_, 0);
	}

	Draw(transform);

}

void FbxParts::DrawMeshAnime(Transform& transform, FbxTime time)
{
	//// その瞬間の自分の姿勢行列を得る
	//FbxAnimEvaluator *evaluator = scene->GetAnimationEvaluator();
	//FbxMatrix mCurrentOrentation = evaluator->GetNodeGlobalTransform(_pNode, time);

	//// Fbx形式の行列からDirectX形式の行列へのコピー（4×4の行列）
	//for (DWORD x = 0; x < 4; x++)
	//{
	//	for (DWORD y = 0; y < 4; y++)
	//	{
	//		_localMatrix(x, y) = (float)mCurrentOrentation.Get(x, y);
	//	}
	//}

	Draw(transform);
}

bool FbxParts::GetBonePosition(std::string boneName, XMFLOAT3* position)
{
	for (int i = 0; i < numBone_; i++)
	{
		if (boneName == ppCluster_[i]->GetLink()->GetName())
		{
			FbxAMatrix  matrix;
			ppCluster_[i]->GetTransformLinkMatrix(matrix);
			
			position->x = (float)matrix[3][0];
			position->y = (float)matrix[3][1];
			position->z = (float)matrix[3][2];

			return true;
		}
	}

	return false;
}

bool FbxParts::GetBonePositionAtNow(std::string boneName, XMFLOAT3* position)
{

		decltype(bonePair)::iterator it = bonePair.find(boneName);
		if (it != bonePair.end())  // 見つかった	
		{
			XMFLOAT4X4  m;
			XMStoreFloat4x4(&m, it->second->newPose);
			position->x = m._41;
			position->y = m._42;
			position->z = m._43;

			return true;
		}

	return false;
}

void FbxParts::RayCast(RayCastData* data)
{
	data->hit = FALSE;

	//マテリアル毎
	for (DWORD i = 0; i < materialCount_; i++)
	{
		//そのマテリアルのポリゴン毎
		for (DWORD j = 0; j < pMaterial_[i].polygonCount; j++)
		{
			//3頂点
			XMFLOAT3 ver[3];
			ver[0] = pVertexData_[ppIndexData_[i][j * 3 + 0]].position;
			ver[1] = pVertexData_[ppIndexData_[i][j * 3 + 1]].position;
			ver[2] = pVertexData_[ppIndexData_[i][j * 3 + 2]].position;

			BOOL  hit = FALSE;
			float dist = 0.0f;

			hit = Direct3D::Intersect(data->start, data->dir, ver[0], ver[1], ver[2], &dist);


			if (hit && dist < data->dist)
			{
				data->hit = TRUE;
				data->dist = dist;
			}
		}
	}
}
