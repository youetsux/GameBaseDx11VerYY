#include "Player.h"
#include "Engine/Model.h"
#include "Engine/Debug.h"
#include "TestScene.h"



Player::Player(GameObject* parent)
	:GameObject(parent), hSilly(-1){
	//swordDir�ɂ́A���������Ƃ��āA���[�J�����f���̌��̍���������
	//��[�܂ł̃x�N�g���Ƃ��āi0,1,0)�������Ă���
	//�����ʒu�͌��_
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
