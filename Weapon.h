#pragma once
#include "Engine\GameObject.h"

class Weapon :
    public GameObject
{
	int hWeapon_;
	Transform localPos;
public:
	//コンストラクタ
	//引数：parent  親オブジェクト（SceneManager）
	Weapon(GameObject* parent);

	//初期化
	void Initialize() override;

	//更新
	void Update() override;

	//描画
	void Draw() override;

	//開放
	void Release() override;

public:
	void SetPos(const XMFLOAT3& pos){ transform_.position_ = pos; }
	void SetWpos(const Transform& tr);
};

