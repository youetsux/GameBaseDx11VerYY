#pragma once
#include <d3d11.h>
#include <fbxsdk.h>
#include <vector>
#include <string>
#include "Transform.h"



class FbxParts;

//レイキャスト用構造体
struct RayCastData
{
	XMFLOAT3	start;	//レイ発射位置
	XMFLOAT3	dir;	//レイの向きベクトル
	float       dist;	//衝突点までの距離
	BOOL        hit;	//レイが当たったか
	XMFLOAT3 normal;	//法線

	RayCastData() { dist = 99999.0f; }
};

//-----------------------------------------------------------
// Axis-Aligned Bounding Box
//-----------------------------------------------------------
struct AABB
{
	XMFLOAT3 min_;
	XMFLOAT3 max_;

	AABB()
		: min_(XMFLOAT3( 1e30f,  1e30f,  1e30f))
		, max_(XMFLOAT3(-1e30f, -1e30f, -1e30f))
	{}

	// 中心座標
	XMFLOAT3 Center() const
	{
		return XMFLOAT3(
			(min_.x + max_.x) * 0.5f,
			(min_.y + max_.y) * 0.5f,
			(min_.z + max_.z) * 0.5f);
	}

	// 各軸のサイズ
	XMFLOAT3 Size() const
	{
		return XMFLOAT3(
			max_.x - min_.x,
			max_.y - min_.y,
			max_.z - min_.z);
	}

	// 最長辺の長さ
	float LongestEdge() const
	{
		XMFLOAT3 s = Size();
		float m = s.x;
		if (s.y > m) m = s.y;
		if (s.z > m) m = s.z;
		return m;
	}
};

//-----------------------------------------------------------
//　FBXファイルを扱うクラス
//　ほとんどの処理は各パーツごとにFbxPartsクラスで行う
//-----------------------------------------------------------
class Fbx
{
	//FbxPartクラスをフレンドクラスにする
	//FbxPartのprivateな関数にもアクセス可
	friend class FbxParts;



	//モデルの各パーツ（複数あるかも）
	std::vector<FbxParts*>	parts_;

	//FBXファイルを扱う機能の本体
	FbxManager* pFbxManager_;

	//FBXファイルのシーン（Load後にDestroyする）
	FbxScene*	pFbxScene_;

	// アニメーション評価器（pFbxScene_をDestroyした後も使用する）
	FbxAnimEvaluator* pAnimEvaluator_;



	// アニメーションのフレームレート
	FbxTime::EMode	_frameRate;

	//アニメーション速度
	float			_animSpeed;

	//アニメーションの最初と最後のフレーム
	int _startFrame, _endFrame;

	// AABB（ロード後に計算。初期ポーズの頂点座標をベースにする）
	AABB aabb_;

	std::string fileName_;	// ロード時のファイル名

	void CalcAABB();	// Load内部から呼ぶ

	//ノードの中身を調べる
	//引数：pNode		調べるノード
	//引数：pPartsList	パーツのリスト
	void CheckNode(FbxNode* pNode, std::vector<FbxParts*> *pPartsList);

public:
	Fbx();
	~Fbx();

	void SetAnimFrame(int start, int end, float speed = -1.0f)
	{
		_startFrame = start;
		_endFrame   = end;
		if (speed >= 0.0f) _animSpeed = speed;
	}

	FbxManager* GetFbxManager() {
		return pFbxManager_;
	}
	FbxAnimEvaluator* GetAnimEvaluator() {
		return pAnimEvaluator_;
	}

	//ロード
	//引数：fileName	ファイル名
	//戻値：成功したかどうか
	virtual HRESULT Load(std::string fileName);

	//描画
	//引数：World	ワールド行列
	void    Draw(Transform& transform, int frame);

	//解放
	void    Release();

	//任意のボーンの位置を取得
	//引数：boneName	取得したいボーンの位置
	//戻値：ボーンの位置
	XMFLOAT3 GetBonePosition(std::string boneName);

	//スキンメッシュアニメ中の現在の任意のボーンの位置を取得
	//引数：boneName	取得したいボーンの位置
	//戻値：ボーンの位置
	XMFLOAT3 GetAnimBonePosition(std::string boneName);

	//レイキャスト（レイを飛ばして当たり判定）
	//引数：data	必要なものをまとめたデータ
	void RayCast(RayCastData *data);

	// AABBを取得（Load後に有効）
	const AABB& GetAABB() const { return aabb_; }
};
