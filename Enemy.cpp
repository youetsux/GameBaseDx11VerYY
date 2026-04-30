#include "Enemy.h"
#include "Engine/Model.h"


Enemy::Enemy(GameObject* parent)
{
}

void Enemy::Initialize()
{
	hEModel = Model::Load("animal-bee.fbx");
	Model::SetAnimFrame(hEModel, 0, 30, 1.0);
}

void Enemy::Update()
{
}

void Enemy::Draw()
{
	transform_.scale_ = {0.002f, 0.002f, 0.002f};
	transform_.position_ = { 0, 3.0, -4 };
	transform_.rotate_ = { 0, -90, 0 };
	Model::SetTransform(hEModel, transform_);
	Model::Draw(hEModel);
}

void Enemy::Release()
{
}
