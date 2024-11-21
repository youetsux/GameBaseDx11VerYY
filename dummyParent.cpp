#include "dummyParent.h"

dummyParent::dummyParent(GameObject* parent)
	:GameObject(parent)
{
}

void dummyParent::Initialize()
{
	//transform_.scale_ = { 0.5,0.5,0.5 };
	transform_.position_ = { 0, 0, 0 };
}

void dummyParent::Update()
{
	transform_.Calclation();
}

void dummyParent::Draw()
{
}

void dummyParent::Release()
{
}
