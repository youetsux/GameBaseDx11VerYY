#pragma once
#include "Engine//GameObject.h"
#include <utility>
#include <vector>

class Ground :
	public GameObject
{
	std::vector<std::pair<Transform, int>> hGModels;
public:
	//コンストラクタ
	//引数：parent  親オブジェクト（SceneManager）
	Ground(GameObject* parent);

	//初期化
	void Initialize() override;

	//更新
	void Update() override;

	//描画
	void Draw() override;

	//開放
	void Release() override;
};

