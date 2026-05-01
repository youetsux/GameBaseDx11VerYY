#include "Ground.h"
#include "Engine/Model.h"
#include "Engine/Debug.h"


namespace
{
	const float SCALE = 0.2f;
	const float GROUND_SIZE_X = 1.0f;
	const float GROUND_SIZE_Z = 1.0f;

	const float GROUND_SCALE = 0.2f;

	const int GROUND_NUM_X = 10;
	const int GROUND_NUM_Z = 10;
}

Ground::Ground(GameObject* parent)
	:GameObject(parent, "Ground")
{
}

void Ground::Initialize()
{
	// ファイル名テーブル（木3種）
	const char* treeFiles[3] = { "tree.fbx", "treePine.fbx", "treePineSmall.fbx" };

	hGModels.clear();

	for (int z = 0; z < GROUND_NUM_Z; z++)
	{
		for (int x = 0; x < GROUND_NUM_X; x++)
		{
			Transform groundTransform;
			groundTransform.scale_ = { GROUND_SCALE, GROUND_SCALE, GROUND_SCALE };
			groundTransform.position_ =
			{
				(x - (GROUND_NUM_X - 1) * 0.5f) * GROUND_SIZE_X,
				0.0f,
				(z - (GROUND_NUM_Z - 1) * 0.5f) * GROUND_SIZE_Z
			};

			// 地面ブロックを1タイルごとに固有のハンドルで登録
			// → DrawShadowAll が各タイルを正しい位置で描画できる
			int hGround = Model::Load("blockRounded.fbx");
			Model::SetShadowCaster(hGround, false);
			Model::SetShadowReceiver(hGround, true);
			Model::SetTransform(hGround, groundTransform);	// 初フレームの影パスにも使われるので今セット
			hGModels.emplace_back(groundTransform, hGround);

			int m = rand() % 10;
			if (m == 0 || m == 1)
			{
				const int treeIndex = rand() % 3;

				Transform treeTransform;
				treeTransform.scale_ = { 0.1f, 0.1f, 0.1f };
				treeTransform.position_ =
				{
					groundTransform.position_.x,
					2.0f,
					groundTransform.position_.z
				};

				// 木も1本ごとに固有のハンドル
				int hTree = Model::Load(treeFiles[treeIndex]);
				Model::SetShadowCaster(hTree, true);
				Model::SetShadowReceiver(hTree, false);
				Model::SetTransform(hTree, treeTransform);
				hGModels.emplace_back(treeTransform, hTree);

				// 木が生えた場所のうち、1/3 の確率でキノコを生やす
				if (rand() % 3 == 0)
				{
					const int dir = rand() % 4;

					float mashroomX = groundTransform.position_.x;
					float mashroomZ = groundTransform.position_.z;

					if      (dir == 0) mashroomX += 0.25f;
					else if (dir == 1) mashroomX -= 0.25f;
					else if (dir == 2) mashroomZ += 0.25f;
					else               mashroomZ -= 0.25f;

					Transform mashroomTransform;
					mashroomTransform.scale_ = { 0.1f, 0.1f, 0.1f };
					mashroomTransform.position_ = { mashroomX, 2.0f, mashroomZ };

					// キノコも1個ごとに固有のハンドル
					int hMashroom = Model::Load("mushrooms.fbx");
					Model::SetShadowCaster(hMashroom, true);
					Model::SetShadowReceiver(hMashroom, false);
					Model::SetTransform(hMashroom, mashroomTransform);
					hGModels.emplace_back(mashroomTransform, hMashroom);
				}
			}
		}
	}

	// 看板（1個なのでそのまま）
	Transform signTransform;
	signTransform.scale_ = { 0.1f, 0.1f, 0.1f };
	signTransform.position_ = { -2.0f, 2.0f, -5.0f };
	int hSign = Model::Load("sign.fbx");
	Model::SetShadowCaster(hSign, true);
	Model::SetShadowReceiver(hSign, false);
	Model::SetTransform(hSign, signTransform);
	hGModels.emplace_back(signTransform, hSign);
}


void Ground::Update()
{
}

void Ground::Draw()
{
	for (auto& g : hGModels)
	{
		Model::SetTransform(g.second, g.first);
		Model::Draw(g.second);
	}
}

void Ground::Release()
{
}
