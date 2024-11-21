#include "JointBall.h"
#include "Engine/Model.h"
JointBall::JointBall(GameObject* parent)
	:GameObject(parent),hBall(-1)
{
}

void JointBall::Initialize()
{
	hBall = Model::Load("yajirusi.fbx");
	transform_.scale_ = { 0.5,0.5,0.5 };
}

void JointBall::Update()
{
}

void JointBall::Draw()
{
	Model::SetTransform(hBall, transform_);
	Model::Draw(hBall);
}

void JointBall::Release()
{
}
