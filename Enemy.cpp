#include "Enemy.h"
#include "Engine/Model.h"


Enemy::Enemy(GameObject* parent)
{
}

void Enemy::Initialize()
{
	hEModel = Model::Load("bee.fbx");
	//Model::SetAnimStack(hEModel, 1);  // スタックの正しいフレーム範囲を自動設定
	//Model::SetAnimSpeed(hEModel, 1.0f); // 速度だけ指定（フレーム範囲は上書きしない）
	//Model::SetAnimFrame(hEModel, 0, 60, 1.0f); // フレーム範囲と速度を指定
}

void Enemy::Update()
{
}

void Enemy::Draw()
{

	transform_.scale_ = {0.01f, 0.01f, 0.01f};
	transform_.position_ = { 0, 3.0, -0 };
	transform_.rotate_ = { 0, -180, 0 };
	Model::SetTransform(hEModel, transform_);
	Model::Draw(hEModel);
}

void Enemy::Release()
{
}
