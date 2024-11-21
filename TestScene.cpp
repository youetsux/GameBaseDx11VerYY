#include "TestScene.h"
#include "Player.h"
//コンストラクタ
TestScene::TestScene(GameObject * parent)
	: GameObject(parent, "TestScene")
{
}

//初期化
void TestScene::Initialize()
{	
	//pWp = Instantiate<Weapon>(this);
	Instantiate <Player>(this);

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
