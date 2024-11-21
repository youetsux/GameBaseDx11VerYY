#pragma once
#include "Engine/GameObject.h"
#include "Weapon.h"

class JointBall;
class Player :
    public GameObject
{
	int hSilly;
	Weapon* pWep;
	XMVECTOR swordDir;
	JointBall *jb[2];
public:
	//コンストラクタ
	//引数：parent  親オブジェクト（SceneManager）
	Player(GameObject* parent);

	//初期化
	void Initialize() override;

	//更新
	void Update() override;

	//描画
	void Draw() override;

	//開放
	void Release() override;
};

