#include "Player.h"
#include "Engine/Model.h"
#include "Engine/Debug.h"
#include "TestScene.h"



Player::Player(GameObject* parent)
	:GameObject(parent), hSilly(-1){
	//swordDirには、初期方向として、ローカルモデルの剣の根っこから
	//先端までのベクトルとして（0,1,0)を代入しておく
	//初期位置は原点
}

void Player::Initialize()
{
	hSilly = Model::Load("GS_MotionSet.fbx");
	Model::SetAnimFrame(hSilly, 0, 1200, 1.0);


}

void Player::Update()
{
	transform_.rotate_.y +=1;

}

void Player::Draw()
{
	transform_.scale_ = { 0.05,0.05,0.05 };
	transform_.position_ = { 0, -5.0, 0 };
	Model::SetTransform(hSilly, transform_);
	Model::Draw(hSilly);
}


void Player::Release()
{
}
