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
	int hGround = Model::Load("blockRounded.fbx");
	int hTree[3] = {
		Model::Load("tree.fbx"),
		Model::Load("treePine.fbx"),
		Model::Load("treePineSmall.fbx")
	};
	int hMashroom = Model::Load("mushrooms.fbx");
	int hSign = Model::Load("sign.fbx");


	hGModels.clear();
	//hGModels.reserve(GROUND_NUM_X * GROUND_NUM_Z);
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

			hGModels.emplace_back(groundTransform, hGround);
			int m = rand() % 10;
			if (m == 0 || m == 1)
			{
				const int treeIndex = rand() % 3;

				Transform treeTransform;

				// ブロックと同じ幅、高さ2
				treeTransform.scale_ = {0.1f, 0.1f, 0.1f };

				// ブロック中央
				treeTransform.position_ =
				{
					groundTransform.position_.x,
					2.0f,
					groundTransform.position_.z
				};

				hGModels.emplace_back(treeTransform, hTree[treeIndex]);
				// 木が生えた場所のうち、1/3 の確率でキノコを生やす
				if (rand() % 3 == 0)
				{
					const int dir = rand() % 4;

					float mashroomX = groundTransform.position_.x;
					float mashroomZ = groundTransform.position_.z;

					if (dir == 0)
					{
						mashroomX += 0.25f;
					}
					else if (dir == 1)
					{
						mashroomX -= 0.25f;
					}
					else if (dir == 2)
					{
						mashroomZ += 0.25f;
					}
					else
					{
						mashroomZ -= 0.25f;
					}

					Transform mashroomTransform;
					mashroomTransform.scale_ = {0.1f, 0.1f, 0.1f};
					mashroomTransform.position_ =
					{
						mashroomX,
						2.0f,
						mashroomZ
					};

					hGModels.emplace_back(mashroomTransform, hMashroom);
				}

			}
		}
	}
	Transform signTransform;
	signTransform.scale_ = {0.1f, 0.1f, 0.1f};
	signTransform.position_ = {-2.0f, 2.0f, -5.0f};
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
