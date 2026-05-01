#include "TestScene.h"
#include "Player.h"
#include "Ground.h"
#include "Enemy.h"
#include "Engine\\Camera.h"

//コンストラクタ
TestScene::TestScene(GameObject * parent)
	: GameObject(parent, "TestScene")
{
}

//初期化
void TestScene::Initialize()
{	
	//pWp = Instantiate<Weapon>(this);
	
	Instantiate <Ground>(this);
	Instantiate <Player>(this);
	Instantiate <Enemy>(this);
	Camera::SetPosition({ 0, 5, -15 });
	Camera::SetTarget({ 0, 0, 0 });

}

//更新
void TestScene::Update()
{
}

//描画
void TestScene::Draw()
{
}

//開放
void TestScene::Release()
{
}
