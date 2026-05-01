#include "Enemy.h"
#include "Engine/Model.h"


Enemy::Enemy(GameObject* parent)
{
}

void Enemy::Initialize()
{
	hEModel = Model::Load("bee2.fbx");
	Model::SetAnimFrame(hEModel, 0, 30, 1.0);
}

void Enemy::Update()
{
	static float ratio = 0.0f;
	ratio += 0.01f;
	if (ratio > 2 * 3.14f)
	{
		ratio = 0.0f;
	}
	transform_.position_ = { 0, 3.0, -4 };
	transform_.position_.x = cosf(ratio) * 3.0f;
	transform_.position_.y = 3.0f + 0.1*sinf(10.0f * ratio);
	if (sinf(ratio) < 0.0f)
	{
		transform_.rotate_ = { 0, 90, 0 };
	}
	else if (sinf(ratio) > 0.0f)
	{
		transform_.rotate_ = { 0, -90, 0 };
	}

}

void Enemy::Draw()
{
	transform_.scale_ = {0.002f, 0.002f, 0.002f};


	Model::SetTransform(hEModel, transform_);
	Model::Draw(hEModel);
}

void Enemy::Release()
{
}
