# シャドウマッピング 実装ガイド
**対象：このプロジェクト（GameBaseDx11VerYY）に影を追加する手順**

---

## そもそも「影」ってどうやって作るの？

普通のゲームの影は「**シャドウマッピング**」という方法で作っています。

考え方はシンプルです。

> 「太陽（光源）の目線で見たとき、何かの後ろに隠れている場所 → そこが影」

これを2段階の描画で実現します。

```
【パス1：影マップを作る】
  光源の目線から、シーン全体を描画する
  → 色は要らない。「光源から各点までの距離（深度）」だけを記録する
  → これを「シャドウマップ（深度テクスチャ）」と呼ぶ

【パス2：通常の描画】
  カメラの目線で普通に描画する
  → 各ピクセルについて「今描いている点は光源から見えるか？」を確認する
  → シャドウマップに記録されている深度より奥にいたら → 影の中
```

---

## 実装の全体像

影を追加するためにいじるファイルはこれだけです。

| ファイル | 何をするか |
|---|---|
| `Engine/ShadowMap.h` | 新しく作る。影の管理クラス（宣言） |
| `Engine/ShadowMap.cpp` | 新しく作る。影の管理クラス（実装） |
| `Engine/Direct3D.h` | シャドウ用シェーダーの番号を追加する |
| `Engine/Direct3D.cpp` | シャドウ用シェーダーを読み込む処理を追加する |
| `Engine/FbxParts.h` | コンスタントバッファに影の情報を追加する |
| `Engine/FbxParts.cpp` | シャドウパス用の描画関数を追加する |
| `Engine/Fbx.h` | シャドウパス用の描画関数の宣言を追加する |
| `Engine/Fbx.cpp` | シャドウパス用の描画関数の実装を追加する |
| `Engine/Model.h` | 影を落とす/受ける設定の宣言を追加する |
| `Engine/Model.cpp` | 影を落とす/受ける設定の実装を追加する |
| `Engine/Main.cpp` | ゲームループに影描画パスを追加する |
| `Assets/Shader/ShadowMap.hlsl` | 新しく作る。影パス用のシェーダー |
| `Assets/Shader/Simple3D.hlsl` | 影を受けて暗くする処理を追加する |

多く見えますが、1つずつ順番にやれば大丈夫です。

---

## STEP 1：シャドウ用シェーダーを作る

### `Assets/Shader/ShadowMap.hlsl` を新規作成

影パスでは「深度だけ書き込めればいい」のでとてもシンプルです。

```hlsl
// 光源から見た WVP 行列（C++側から送られてくる）
cbuffer global : register(b0)
{
    float4x4 g_matWVP;
}

// 頂点シェーダーの出力
struct VS_OUT
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// 頂点シェーダー：光源の目線でモデルを変換するだけ
VS_OUT VS(float4 pos : POSITION, float4 normal : NORMAL, float2 uv : TEXCOORD)
{
    VS_OUT output;
    output.pos = mul(pos, g_matWVP);
    output.uv  = uv;
    return output;
}

// ピクセルシェーダー：何も出力しない（深度バッファへの書き込みは自動）
void PS(VS_OUT input)
{
}
```

**ポイント：** PSが空でも、DirectXは自動的に深度バッファを更新してくれます。

---

## STEP 2：シェーダーの番号を追加する

### `Engine/Direct3D.h`

`SHADER_TYPE` という列挙型に `SHADER_SHADOW` を追加します。

```cpp
enum SHADER_TYPE
{
    SHADER_3D,
    SHADER_2D,
    SHADER_UNLIT,
    SHADER_BILLBOARD,
    SHADER_SHADOW,   // ← これを追加
    SHADER_MAX
};
```

### `Engine/Direct3D.cpp`

シェーダーを読み込む処理（配列の初期化部分）に、ShadowMap.hlsl の読み込みを追加します。
**SHADER_3D と同じラスタライザー設定**（カリングの方向など）を使えばOKです。

```cpp
// SHADER_SHADOW 用の設定（SHADER_3D の初期化コードを参考に追加する）
// 頂点シェーダー：ShadowMap.hlsl の VS 関数
// ピクセルシェーダー：ShadowMap.hlsl の PS 関数
// 入力レイアウト：POSITION, NORMAL, TEXCOORD（3Dモデルと同じ）
// ラスタライザー：CULL_BACK（3Dと同じ）
```

---

## STEP 3：影の管理クラスを作る

### `Engine/ShadowMap.h` を新規作成

```cpp
#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
using namespace DirectX;

namespace ShadowMap
{
    const int SHADOW_MAP_SIZE = 2048; // 影テクスチャの解像度

    // シャドウパス用コンスタントバッファ（光源のWVP行列だけ）
    struct SHADOW_CB
    {
        XMMATRIX matWVP;
    };

    void Initialize();                              // 起動時に1回呼ぶ
    void Release();                                 // 終了時に1回呼ぶ
    void SetLightDir(XMFLOAT3 lightDir);            // 光の方向を設定する
    void SetEnable(bool enable);                    // 影のON/OFF
    bool IsEnabled();                               // 影がONかどうか
    XMMATRIX GetLightViewProjection();              // 光源のVP行列を取得
    void BeginShadowPass();                         // 影パス開始
    void EndShadowPass();                           // 影パス終了
    void BindToShader();                            // 影テクスチャをシェーダーにセット
    void UnbindFromShader();                        // セットを解除
    void UpdateAndBindConstantBuffer(XMMATRIX world); // 光源WVP行列を送る
}
```

### `Engine/ShadowMap.cpp` を新規作成

```cpp
#include "ShadowMap.h"
#include "Direct3D.h"
#include "Global.h"

namespace ShadowMap
{
    // ─── 変数 ─────────────────────────────────
    ID3D11Texture2D*          pDepthTexture_       = nullptr; // 深度テクスチャ本体
    ID3D11DepthStencilView*   pDepthStencilView_   = nullptr; // 深度バッファとして使うビュー
    ID3D11ShaderResourceView* pShaderResourceView_ = nullptr; // シェーダーで読むためのビュー
    ID3D11Buffer*             pShadowConstantBuffer_ = nullptr;
    bool      isEnabled_    = true;
    XMMATRIX  matLightView_;
    XMMATRIX  matLightProj_;
    ID3D11RenderTargetView*   pSavedRTV_ = nullptr; // 元のRTを保存しておく
    ID3D11DepthStencilView*   pSavedDSV_ = nullptr; // 元のDSVを保存しておく

    // ─── Initialize ──────────────────────────
    void Initialize()
    {
        // ① 深度テクスチャを作る
        //    R32_TYPELESS にすると、深度バッファ(DSV)とシェーダーリソース(SRV)の
        //    両方として使えるようになる
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width              = SHADOW_MAP_SIZE;
        texDesc.Height             = SHADOW_MAP_SIZE;
        texDesc.MipLevels          = 1;
        texDesc.ArraySize          = 1;
        texDesc.Format             = DXGI_FORMAT_R32_TYPELESS;
        texDesc.SampleDesc.Count   = 1;
        texDesc.Usage              = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        Direct3D::pDevice_->CreateTexture2D(&texDesc, nullptr, &pDepthTexture_);

        // ② 深度バッファビュー(DSV)を作る　← 深度を「書き込む」ためのビュー
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        Direct3D::pDevice_->CreateDepthStencilView(pDepthTexture_, &dsvDesc, &pDepthStencilView_);

        // ③ シェーダーリソースビュー(SRV)を作る　← 深度を「読む」ためのビュー
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        Direct3D::pDevice_->CreateShaderResourceView(pDepthTexture_, &srvDesc, &pShaderResourceView_);

        // ④ コンスタントバッファを作る
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth      = sizeof(SHADOW_CB);
        cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        Direct3D::pDevice_->CreateBuffer(&cbDesc, nullptr, &pShadowConstantBuffer_);

        // ⑤ シャドウマップ用のサンプラーを作って s1 スロットにセット
        //    テクスチャの外側（影のない部分）は深度1.0（光が届く）を返すようにする
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.BorderColor[0] = 1.0f; // 境界外は「光が届く(1.0)」扱い
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        ID3D11SamplerState* pShadowSampler = nullptr;
        Direct3D::pDevice_->CreateSamplerState(&sampDesc, &pShadowSampler);
        Direct3D::pContext_->PSSetSamplers(1, 1, &pShadowSampler);
        SAFE_RELEASE(pShadowSampler);

        // ⑥ 光源の方向を初期設定
        SetLightDir(XMFLOAT3(-1.0f, -1.0f, 1.0f));
    }

    void Release()
    {
        SAFE_RELEASE(pDepthTexture_);
        SAFE_RELEASE(pDepthStencilView_);
        SAFE_RELEASE(pShaderResourceView_);
        SAFE_RELEASE(pShadowConstantBuffer_);
    }

    // ─── SetLightDir ─────────────────────────
    // 光の方向から「光源カメラ」のビュー行列・プロジェクション行列を作る
    void SetLightDir(XMFLOAT3 lightDir)
    {
        XMVECTOR dir      = XMVector3Normalize(XMLoadFloat3(&lightDir));
        XMVECTOR lightPos = XMVectorScale(dir, -50.0f); // 光源の位置（逆方向に50離す）
        XMVECTOR target   = XMVectorZero();              // 注視点はシーンの中心
        XMVECTOR up       = XMVectorSet(0, 1, 0, 0);

        matLightView_ = XMMatrixLookAtLH(lightPos, target, up);

        // 平行投影（カメラと違って遠近感なし）
        // 数字はシーンの大きさに合わせて調整する
        matLightProj_ = XMMatrixOrthographicLH(20.0f, 20.0f, 0.1f, 200.0f);
    }

    void SetEnable(bool enable) { isEnabled_ = enable; }
    bool IsEnabled()            { return isEnabled_; }
    XMMATRIX GetLightViewProjection() { return matLightView_ * matLightProj_; }

    // ─── BeginShadowPass ─────────────────────
    void BeginShadowPass()
    {
        // 現在のRTとDSVを保存しておく（終わったら元に戻す）
        Direct3D::pContext_->OMGetRenderTargets(1, &pSavedRTV_, &pSavedDSV_);

        // シャドウマップをクリア（深度1.0 = 何もない）
        Direct3D::pContext_->ClearDepthStencilView(pDepthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);

        // 描画先を「シャドウマップの深度バッファだけ」に切り替える
        // RTVをnullにすることで色は書かない
        ID3D11RenderTargetView* pNullRTV = nullptr;
        Direct3D::pContext_->OMSetRenderTargets(1, &pNullRTV, pDepthStencilView_);

        // シャドウマップのサイズにビューポートを合わせる
        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)SHADOW_MAP_SIZE;
        vp.Height   = (float)SHADOW_MAP_SIZE;
        vp.MaxDepth = 1.0f;
        Direct3D::pContext_->RSSetViewports(1, &vp);

        // シャドウ用シェーダーに切り替える
        Direct3D::SetShader(Direct3D::SHADER_SHADOW);
    }

    // ─── EndShadowPass ───────────────────────
    void EndShadowPass()
    {
        // 保存しておいたRTとDSVを元に戻す
        Direct3D::pContext_->OMSetRenderTargets(1, &pSavedRTV_, pSavedDSV_);
        SAFE_RELEASE(pSavedRTV_);
        SAFE_RELEASE(pSavedDSV_);

        // ビューポートも画面サイズに戻す
        Direct3D::SetViewPort((float)Direct3D::GetWidth(), (float)Direct3D::GetHeight());

        // 通常の3Dシェーダーに戻す
        Direct3D::SetShader(Direct3D::SHADER_3D);
    }

    // ─── BindToShader / UnbindFromShader ─────
    void BindToShader()
    {
        // シャドウマップを t1 スロットにセット（t0 はモデルのテクスチャが使っている）
        Direct3D::pContext_->PSSetShaderResources(1, 1, &pShaderResourceView_);
    }

    void UnbindFromShader()
    {
        ID3D11ShaderResourceView* pNullSRV = nullptr;
        Direct3D::pContext_->PSSetShaderResources(1, 1, &pNullSRV);
    }

    // ─── UpdateAndBindConstantBuffer ─────────
    // 光源から見た WVP 行列をシェーダーに送る（影パスで各モデルを描画する前に呼ぶ）
    void UpdateAndBindConstantBuffer(XMMATRIX world)
    {
        SHADOW_CB cb;
        cb.matWVP = XMMatrixTranspose(world * matLightView_ * matLightProj_);

        D3D11_MAPPED_SUBRESOURCE msr;
        Direct3D::pContext_->Map(pShadowConstantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
        memcpy_s(msr.pData, msr.RowPitch, &cb, sizeof(cb));
        Direct3D::pContext_->Unmap(pShadowConstantBuffer_, 0);

        Direct3D::pContext_->VSSetConstantBuffers(0, 1, &pShadowConstantBuffer_);
    }
}
```

---

## STEP 4：コンスタントバッファに影の情報を追加する

### `Engine/FbxParts.h`

`CONSTANT_BUFFER` 構造体に、影の判定に必要な情報を足します。

```cpp
struct CONSTANT_BUFFER
{
    XMMATRIX worldVewProj;
    XMMATRIX normalTrans;
    XMMATRIX world;
    XMFLOAT4 lightDirection;
    XMFLOAT4 diffuse;
    XMFLOAT4 ambient;
    XMFLOAT4 speculer;
    XMFLOAT4 cameraPosition;
    FLOAT    shininess;
    BOOL     isTexture;

    // ↓ここから追加（16バイト境界に注意）
    FLOAT    _pad0[2];          // 詰め物（shininess+isTextureが24バイトなので2個追加して32バイトにする）
    XMMATRIX matLightVP;        // 光源のビュー×プロジェクション行列
    BOOL     isShadowReceiver;  // この物体が影を受けるかどうか
    FLOAT    _pad1[3];          // 詰め物
};
```

> **なぜ「詰め物」が必要？**  
> DirectXのコンスタントバッファは**16バイト単位**でメモリを管理します。  
> `XMMATRIX` は必ず16バイト境界から始まらないといけません。  
> `shininess(4) + isTexture(4) = 8バイト` なので、8バイト足りない。  
> `_pad0[2]`（float 2個 = 8バイト）を入れてぴったり揃えます。

---

## STEP 5：影パス用の描画関数を追加する

### `Engine/FbxParts.h`

```cpp
// 影パス用（光源の目線から深度だけ書き込む）
void DrawShadow(Transform& transform, FbxTime time, bool isShadowReceiver);
```

### `Engine/FbxParts.cpp`

```cpp
void FbxParts::DrawShadow(Transform& transform, FbxTime time, bool isShadowReceiver)
{
    // ボーンがある場合は頂点を変形してから描画する
    // （ここはDrawSkinAnimeと同じ処理なので割愛）

    // 光源の WVP 行列をコンスタントバッファに送る
    ShadowMap::UpdateAndBindConstantBuffer(transform.GetWorldMatrix());

    // 頂点バッファをセット
    UINT stride = sizeof(VERTEX);
    UINT offset = 0;
    Direct3D::pContext_->IASetVertexBuffers(0, 1, &pVertexBuffer_, &stride, &offset);

    // マテリアルの数だけ描画する
    for (DWORD i = 0; i < materialCount_; i++)
    {
        Direct3D::pContext_->IASetIndexBuffer(ppIndexBuffer_[i], DXGI_FORMAT_R32_UINT, 0);
        Direct3D::pContext_->DrawIndexed(pMaterial_[i].polygonCount * 3, 0, 0);
    }
}
```

### `Engine/FbxParts.cpp` の通常 `Draw` にも追記

通常描画時に「光源のVP行列」と「影を受けるか」をシェーダーに渡します。

```cpp
// 既存の Draw 関数の中で cb に値を入れる部分に追加
cb.matLightVP       = XMMatrixTranspose(ShadowMap::GetLightViewProjection());
cb.isShadowReceiver = ShadowMap::IsEnabled() ? TRUE : FALSE;
```

### `Engine/Fbx.h` と `Engine/Fbx.cpp`

`Fbx`クラスにも `DrawShadow` を追加して、全パーツを影パスで描画できるようにします。

```cpp
// Fbx.h に追加
void DrawShadow(Transform& transform, int frame, bool isShadowReceiver);
```

```cpp
// Fbx.cpp に追加
void Fbx::DrawShadow(Transform& transform, int frame, bool isShadowReceiver)
{
    for (int k = 0; k < parts_.size(); k++)
    {
        FbxTime time;
        time.SetTime(0, 0, 0, frame + _startFrame, 0, 0, _frameRate);
        parts_[k]->DrawShadow(transform, time, isShadowReceiver);
    }
}
```

---

## STEP 6：Model クラスに影の設定を追加する

### `Engine/Model.h`

`ModelData` 構造体に2つのフラグを追加します。

```cpp
struct ModelData
{
    // ...既存のメンバー...
    bool isShadowCaster  = true;  // 影を落とすか（デフォルトはtrue）
    bool isShadowReceiver = true; // 影を受けるか（デフォルトはtrue）
};

// 関数宣言も追加
void SetShadowCaster(int handle, bool enable);
void SetShadowReceiver(int handle, bool enable);
void DrawShadowAll();
```

### `Engine/Model.cpp`

```cpp
void SetShadowCaster(int handle, bool enable)
{
    _datas[handle]->isShadowCaster = enable;
}

void SetShadowReceiver(int handle, bool enable)
{
    _datas[handle]->isShadowReceiver = enable;
}

// 影パスで呼ばれる：影を落とすモデルだけ描画する
void DrawShadowAll()
{
    for (int i = 0; i < _datas.size(); i++)
    {
        if (_datas[i] == nullptr) continue;
        if (!_datas[i]->isShadowCaster) continue; // 影を落とさないものはスキップ
        if (_datas[i]->pFbx == nullptr) continue;

        _datas[i]->pFbx->DrawShadow(
            _datas[i]->transform,
            (int)_datas[i]->nowFrame,
            _datas[i]->isShadowReceiver
        );
    }
}
```

---

## STEP 7：ゲームループに影描画パスを追加する

### `Engine/Main.cpp`

```cpp
// ① 初期化の後ろ（Direct3D::Initialize の直後）に追加
ShadowMap::Initialize();

// ② ゲームループ内、BeginDraw() の前に追加
if (ShadowMap::IsEnabled())
{
    ShadowMap::BeginShadowPass(); // 描画先を影テクスチャに切り替える
    Model::DrawShadowAll();       // 影を作るモデルを描画
    ShadowMap::EndShadowPass();   // 描画先を画面に戻す
}

// ③ BeginDraw() の直後に追加
ShadowMap::BindToShader(); // 影テクスチャをシェーダーの t1 スロットにセット

// ④ EndDraw() の直後に追加
ShadowMap::UnbindFromShader(); // セットを解除（次フレームの書き込みの邪魔になるため）

// ⑤ 解放処理に追加（Direct3D::Release() の前）
ShadowMap::Release();
```

---

## STEP 8：通常描画シェーダーに影の判定を追加する

### `Assets/Shader/Simple3D.hlsl`

**グローバル変数に追加：**

```hlsl
// シャドウマップ（t1スロット。t0はモデルのテクスチャが使っている）
Texture2D    g_shadowMap     : register(t1);
SamplerState g_shadowSampler : register(s1);

// cbuffer の中に追加（isTexture の後）
float2   g_pad0;            // 詰め物
float4x4 g_matLightVP;      // 光源のビュー×プロジェクション行列
bool     g_isShadowReceiver; // この物体が影を受けるか
float3   g_pad1;            // 詰め物
```

**VS_OUT 構造体に追加：**

```hlsl
float4 shadowPos : TEXCOORD3; // 光源カメラから見たこの頂点の位置
```

**頂点シェーダーに追加：**

```hlsl
// 光源の目線でこの頂点がどこにいるかを計算してピクセルシェーダーに渡す
outData.shadowPos = mul(mul(pos, g_matWorld), g_matLightVP);
```

**ピクセルシェーダーに追加：**

```hlsl
float shadow = 1.0f; // 1.0=明るい、0.5=影の中

if (g_isShadowReceiver)
{
    // 射影座標を 0〜1 のテクスチャUVに変換する
    float2 shadowUV;
    shadowUV.x =  shadowPos.x / shadowPos.w * 0.5f + 0.5f;
    shadowUV.y = -shadowPos.y / shadowPos.w * 0.5f + 0.5f;

    if (shadowUV.x >= 0 && shadowUV.x <= 1 &&
        shadowUV.y >= 0 && shadowUV.y <= 1)
    {
        // シャドウマップから深度を読む（光源から見て何が一番手前にあるか）
        float shadowDepth  = g_shadowMap.Sample(g_shadowSampler, shadowUV).r;
        float currentDepth = shadowPos.z / shadowPos.w;

        // バイアス：「ほんのわずかな誤差」で地面が自分自身に影をつけてしまう
        //           のを防ぐための小さなずらし値
        float bias = 0.001f;

        // 今のピクセルが、シャドウマップに記録されている深度より奥にいる
        // → 光が届いていない = 影の中
        if (currentDepth > shadowDepth + bias)
        {
            shadow = 0.5f; // 影の中なので暗くする
        }
    }
}

// 最終的な色に影の暗さをかける
return (diffuse * shade + diffuse * ambient + speculer) * shadow;
```

---

## STEP 9：各オブジェクトに影の設定をする

### `Ground.cpp`（またはモデルを使っているどのクラスでも同様）

```cpp
// 地面ブロック：影を「落とさない」が「受ける」
int hGround = Model::Load("blockRounded.fbx");
Model::SetShadowCaster(hGround, false);    // 影を落とさない
Model::SetShadowReceiver(hGround, true);   // 影を受ける
Model::SetTransform(hGround, groundTransform);

// 木：影を「落とす」が「受けない」
int hTree = Model::Load("tree.fbx");
Model::SetShadowCaster(hTree, true);       // 影を落とす
Model::SetShadowReceiver(hTree, false);    // 影を受けない
Model::SetTransform(hTree, treeTransform);
```

> **注意：影を落とすモデル（ShadowCaster）は、ハンドルを1本ごとに別々に取得してください。**  
> 複数の木が同じハンドルを共有すると、`DrawShadowAll` が最後にセットした位置でしか影を描けなくなります。

---

## よくあるハマりポイント

### ① 影がまったく表示されない
→ `BeginShadowPass` の中で `SetDepthBafferWriteEnable(true)` を呼んでいませんか？  
　 あの関数は内部で `OMSetRenderTargets` を呼ぶので、  
　 セットしたシャドウDSVが画面のDSVで**上書き**されてしまいます。呼ばないでください。

### ② 影の解像度が低くてガタガタ
→ `SHADOW_MAP_SIZE` を大きくする（1024→2048）。  
　 または `XMMatrixOrthographicLH` の範囲を小さくする（シーンぴったりにする）。

### ③ 同じ木を複数本置いているのに影が1か所にしか出ない
→ `Model::Load` が同じファイル名で呼ばれると `pFbx` を使い回します。  
　 でも `transform`（位置・向き）は `ModelData` ごとに独立しています。  
　 毎回 `Load` を呼べばハンドルが別々になり、別々の位置を持てます。  
　 **忘れずに `SetTransform` を `Initialize` の時点で呼んでおいてください。**  
　 （1フレーム目の影パスはゲームループ開始直後に走るので）

### ④ 地面に近いものの影が出ない
→ バイアス値が大きすぎます。  
　 `bias = 0.005f` は `near=0.1, far=200` のとき約1ワールド単位に相当します。  
　 地面から1ワールド単位以内のオブジェクトの影は全部消えます。  
　 `0.001f` 程度に下げてください。

### ⑤ 影の位置がおかしい（ズレて見える）
→ `g_matWorld` をシェーダーに渡すときの転置を確認してください。  
　 DirectXのXMMatrixは「行優先」、HLSLのcbufferは「列優先」です。  
　 C++側で `XMMatrixTranspose()` してからセットするのが正しい手順です。

---

## まとめ：流れを一言で

```
【初期化】
  ShadowMap::Initialize() でテクスチャ・バッファを準備する

【毎フレーム：影を作る】
  BeginShadowPass()     → 描画先をシャドウマップに切り替え
  DrawShadowAll()       → 光源の目線で全モデルを描画（深度だけ記録）
  EndShadowPass()       → 描画先を画面に戻す
  BindToShader()        → 作ったシャドウマップをシェーダーに渡す

【毎フレーム：普通に描画する】
  通常通り Draw() を呼ぶだけ。シェーダー側が自動で影の判定をする

  UnbindFromShader()    → シャドウマップの割り当てを解除

【終了】
  ShadowMap::Release()
```
