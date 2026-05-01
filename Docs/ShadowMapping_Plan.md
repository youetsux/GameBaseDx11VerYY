# シャドウマッピング実装計画

## 概要

既存エンジンの流儀（`Direct3D` namespace / `Model` namespace / `.hlsl` シェーダー）を
なるべく壊さずにシャドウマッピングを追加する。

---

## 要件

| # | 要件 |
|---|---|
| 1 | シャドウマッピングによる影の描画 |
| 2 | 影のON/OFF切り替え（グローバル） |
| 3 | モデル単位でキャスター（影を落とす側）／レシーバー（影を受ける側）を指定可能 |
| 4 | 既存コードの動作を壊さない |

---

## 実装方針

### パス構成（2パス描画）

```
[Pass 1: シャドウパス]
  光源視点でシーンを描画
  → 深度値をシャドウマップ（テクスチャ）に書き込む
  → 対象：ShadowCaster フラグが立っているモデルのみ

[Pass 2: 通常描画パス]
  カメラ視点でシーンを描画
  → シャドウマップをサンプリングして影を適用
  → 対象：ShadowReceiver フラグが立っているモデルに影を描画
          それ以外は従来通り描画
```

---

## 追加・変更ファイル一覧

### 新規追加

| ファイル | 内容 |
|---|---|
| `Engine/ShadowMap.h/.cpp` | シャドウマップ管理クラス（レンダーターゲット生成・ bind・解放） |
| `Assets/Shader/ShadowMap.hlsl` | シャドウパス用シェーダー（深度値書き込みのみ） |

### 変更

| ファイル | 変更内容 |
|---|---|
| `Engine/Direct3D.h/.cpp` | `SHADER_TYPE` に `SHADER_SHADOW` 追加、シャドウパス用定数バッファ追加 |
| `Assets/Shader/Simple3D.hlsl` | シャドウマップのサンプリングと影計算を追加（`g_shadowMap`、`g_matLightVP`、`g_isShadow` フラグ追加） |
| `Engine/Model.h/.cpp` | `ModelData` に `isShadowCaster`・`isShadowReceiver` フラグ追加、`SetShadowCaster()`・`SetShadowReceiver()` 関数追加 |
| `Engine/FbxParts.h/.cpp` | `DrawShadow(Transform&, FbxTime)` 関数追加（シャドウパス用、深度のみ書き込み） |
| `Engine/Fbx.h/.cpp` | `DrawShadow(Transform&, int frame)` 関数追加 |

---

## 各ファイルの詳細

### `Engine/ShadowMap.h/.cpp`

```cpp
namespace ShadowMap
{
    // シャドウマップの解像度
    const int SHADOW_MAP_SIZE = 2048;

    // リソース
    ID3D11Texture2D*           pDepthTexture_;   // 深度テクスチャ
    ID3D11DepthStencilView*    pDSV_;            // 深度ステンシルビュー（書き込み用）
    ID3D11ShaderResourceView*  pSRV_;            // シェーダーリソースビュー（サンプリング用）

    // 光源行列
    XMMATRIX  matLightView_;
    XMMATRIX  matLightProj_;

    // グローバルON/OFF
    bool isEnabled_;

    void Initialize();
    void BeginShadowPass();   // シャドウパス開始（DSVをバインド）
    void EndShadowPass();     // シャドウパス終了（元のRTVに戻す）
    void BindToShader();      // SRVをt1スロットにバインド
    void SetLightDir(XMFLOAT3 dir);
    void SetEnable(bool enable);
    bool IsEnabled();
    XMMATRIX GetLightViewProj();
    void Release();
}
```

---

### `Assets/Shader/ShadowMap.hlsl`

- 入力：頂点位置のみ
- 出力：深度値（SV_Depth）
- スキンアニメーション対応は不要（静的メッシュ＋スキンメッシュ共通でワールド変換済み頂点を受け取る）

```hlsl
cbuffer global
{
    float4x4 g_matWVP;  // 光源WVP
};

float4 VS(float4 pos : POSITION) : SV_POSITION
{
    return mul(pos, g_matWVP);
}

// PSは何も出力しない（深度のみ）
void PS() {}
```

---

### `Simple3D.hlsl` への追加

定数バッファへの追加：

```hlsl
Texture2D       g_shadowMap   : register(t1);   // シャドウマップ
SamplerState    g_shadowSampler : register(s1);  // 比較サンプラー

cbuffer global
{
    // ...既存...
    float4x4 g_matLightVP;   // 光源ビュープロジェクション行列
    bool     g_isShadow;     // シャドウ受け取りON/OFF
};
```

VSへの追加：

```hlsl
outData.shadowPos = mul(float4(worldPos.xyz, 1), g_matLightVP);
```

PSへの追加：

```hlsl
float shadow = 1.0f;
if (g_isShadow)
{
    float2 shadowUV = shadowPos.xy / shadowPos.w * float2(0.5, -0.5) + 0.5;
    float shadowDepth = g_shadowMap.Sample(g_shadowSampler, shadowUV).r;
    float pixelDepth  = shadowPos.z / shadowPos.w - 0.001f; // バイアス
    shadow = (pixelDepth > shadowDepth) ? 0.5f : 1.0f;
}
return diffuse * shade * shadow + diffuse * ambient + speculer;
```

---

### `Engine/Model.h` への追加

`ModelData` 構造体に追加：

```cpp
bool isShadowCaster;    // 影を落とすか
bool isShadowReceiver;  // 影を受けるか
```

コンストラクタで両方 `true` をデフォルト値にする。

`Model` namespace に追加：

```cpp
void SetShadowCaster  (int handle, bool enable);
void SetShadowReceiver(int handle, bool enable);
bool IsShadowCaster   (int handle);
bool IsShadowReceiver (int handle);
```

---

### ゲームループでの描画フロー

`Engine/Main.cpp` または `SceneManager` の描画部分でこの順で呼ぶ：

```
1. ShadowMap::BeginShadowPass()
   → ShadowCaster なモデルだけ Fbx::DrawShadow() で描画
2. ShadowMap::EndShadowPass()
3. ShadowMap::BindToShader()       // t1 にシャドウマップをセット
4. 通常の BeginDraw()
   → 全モデルを Draw()
     ・ShadowReceiver なら g_isShadow = true で描画
     ・そうでなければ g_isShadow = false で描画
5. EndDraw()
```

---

## 使用イメージ（ゲーム側コード）

```cpp
void TestScene::Initialize()
{
    ShadowMap::SetEnable(true);
    ShadowMap::SetLightDir({ -1, -2, 1 });

    hGround = Model::Load("ground.fbx");
    Model::SetShadowCaster  (hGround, false);  // 地面は影を落とさない
    Model::SetShadowReceiver(hGround, true);   // 地面は影を受ける

    hEnemy = Model::Load("animal-bee.fbx");
    Model::SetShadowCaster  (hEnemy, true);    // ハチは影を落とす
    Model::SetShadowReceiver(hEnemy, false);   // ハチは影を受けない
}
```

---

## 未検討・後回し事項

- スキンアニメーションモデルのシャドウパス対応（ボーン変形後の頂点で深度描画）
- PCF（Percentage Closer Filtering）によるエッジのぼかし
- カスケードシャドウ（遠景の影精度向上）
- ポイントライトシャドウ（現状は平行光源のみ想定）
