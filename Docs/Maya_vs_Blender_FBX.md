# Maya製 vs Blender製 FBX 読み込み差異まとめ

現在のコードベース（`Fbx.cpp` / `FbxParts.cpp`）をベースに、  
BlenderのFBXを読み込む際に問題になりやすい差異をまとめる。

---

## 1. 座標系（軸の向き）

| 項目 | Maya | Blender |
|------|------|---------|
| 前方向 | +Z | +Y |
| 上方向 | +Y | +Z |
| 右方向 | +X | +X |
| FBX出力時の軸 | Y-up / 右手系 | Z-up / 右手系（デフォルト） |

### 現在のコードの対処
`InitVertex` でZ軸を反転している。これはMaya（右手系Y-up）→DirectX（左手系Y-up）変換。

```cpp
// FbxParts.cpp - InitVertex
pVertexData_[index].position = XMFLOAT3((float)pos[0], (float)pos[1], -(float)pos[2]);
pVertexData_[index].normal   = XMFLOAT3((float)Normal[0], (float)Normal[1], -(float)Normal[2]);
```

### Blenderで起きる問題
- Blenderはデフォルト **Z-up** のため、FBX出力時に「Apply Transform」を**しない**と軸が90度ずれる
- 「Apply Transform」を有効にしてエクスポートすれば軸は揃うが、ボーンの向きが変わることがある
- 軸変換行列がFBXに埋め込まれる場合があり、`GetNodeGlobalTransform` の結果がMayaと異なる

### 対処方針
- Blender側で **FBX Export → Apply Transform ON** を推奨
- もしApply無しで読む場合、ルートノードに90度の回転補正が必要
  ```cpp
  // 例：ルートノードへの補正行列（X軸-90度）
  XMMATRIX axisCorrection = XMMatrixRotationX(XMConvertToRadians(-90.0f));
  ```

---

## 2. ポリゴンの三角化

| 項目 | Maya | Blender |
|------|------|---------|
| デフォルト出力 | 三角形ポリゴンが多い | 四角形ポリゴンが残りやすい |
| FBX内部 | ほぼ三角化済み | 四角形・N角形が混在しやすい |

### 現在のコードの対処
`Triangulate` と `RemoveBadPolygonsFromMeshes` がコメントアウトされている。

```cpp
// Fbx.cpp - Load（現在コメントアウト中）
//geometryConverter.Triangulate(pFbxScene_, true);
//geometryConverter.RemoveBadPolygonsFromMeshes(pFbxScene_);
```

また `InitIndex` / `InitVertex` は**3頂点固定**で処理している。

```cpp
// FbxParts.cpp - InitVertex
for (int vertex = 0; vertex < 3; vertex++)  // 3頂点しか見ていない
```

### Blenderで起きる問題
- 四角形ポリゴンが来るとインデックスが壊れ、描画が崩れる
- `polygonCount_` の値はポリゴン数であり、頂点数（3の倍数）ではない

### 対処方針
Blender対応時は `Triangulate` を**必ず有効化**する。

```cpp
// Fbx.cpp - Load
geometryConverter.Triangulate(pFbxScene_, true);
geometryConverter.RemoveBadPolygonsFromMeshes(pFbxScene_);
```

---

## 3. マテリアルの種類（Phong前提問題）

| 項目 | Maya | Blender |
|------|------|---------|
| デフォルトマテリアル | Phong / Lambert | Principled BSDF（独自） |
| FBX出力時の変換 | Phong/Lambert がそのまま出る | Lambert に変換される（Phongプロパティ欠落あり） |

### 現在のコードの対処
`InitMaterial` は `FbxSurfacePhong` へのキャストを**無条件**に行っている。

```cpp
// FbxParts.cpp - InitMaterial(FbxMesh*)
FbxSurfacePhong* pPhong = (FbxSurfacePhong*)pMaterial;  // 強制キャスト
```

その後 `GetClassId().Is(FbxSurfacePhong::ClassId)` で分岐しているが、  
最初のキャスト自体が不正なポインタ操作になりうる。

### Blenderで起きる問題
- Blenderの Principled BSDF は FBX出力時に Lambert に変換される
- Specular / Shininess が `0` になる（Lambert には存在しない）
- Phongキャストのまま `pPhong->Ambient` 等にアクセスするとクラッシュの危険

### 対処方針
マテリアル取得時にクラスIDで分岐し、Lambert の場合は別処理にする。

```cpp
FbxSurfaceMaterial* pMaterial = pMesh->GetNode()->GetMaterial(i);

if (pMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
{
    FbxSurfacePhong* pPhong = static_cast<FbxSurfacePhong*>(pMaterial);
    // Phong処理
}
else if (pMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
{
    FbxSurfaceLambert* pLambert = static_cast<FbxSurfaceLambert*>(pMaterial);
    // Lambert処理（Specular/Shininess は 0 固定）
}
```

---

## 4. テクスチャパスの形式

| 項目 | Maya | Blender |
|------|------|---------|
| テクスチャパス | 相対パス or 絶対パス | 絶対パスになりやすい |
| パス区切り文字 | `/` or `\` | `/`（Unix形式） |

### 現在のコードの対処
`GetRelativeFileName()` を使い、`_splitpath_s` でファイル名だけを取り出している。

```cpp
// FbxParts.cpp - InitTexture
_splitpath_s(texture->GetRelativeFileName(), nullptr, 0, nullptr, 0, name, _MAX_FNAME, ext, _MAX_EXT);
```

### Blenderで起きる問題
- Blenderはテクスチャに**絶対パス**を埋め込むことが多い
- `GetRelativeFileName()` が空文字 or 絶対パスを返し、`_splitpath_s` でのパース結果が空になるケースがある
- その場合 `GetFileName()` にフォールバックする必要がある

### 対処方針
`GetRelativeFileName` が空の場合に `GetFileName` を使うフォールバックを追加する。

```cpp
const char* texPath = texture->GetRelativeFileName();
if (texPath == nullptr || texPath[0] == '\0')
    texPath = texture->GetFileName();

_splitpath_s(texPath, nullptr, 0, nullptr, 0, name, _MAX_FNAME, ext, _MAX_EXT);
```

---

## 5. UV のマッピングモード・参照モード

| 項目 | Maya | Blender |
|------|------|---------|
| MappingMode | `eByPolygonVertex` が主 | `eByPolygonVertex` が主 |
| ReferenceMode | `eIndexToDirect` が多い | `eDirect` が多い |

### 現在のコードの対処
`eIndexToDirect` と `eDirect` の両方を分岐して処理している。

```cpp
// FbxParts.cpp - InitVertex
if (pUV->GetReferenceMode() == FbxLayerElement::eIndexToDirect) { ... }
else if (pUV->GetReferenceMode() == FbxLayerElement::eDirect)   { ... }
```

### Blenderで起きる問題
- Blenderは `eDirect` を使うことが多いため、こちらのパスが主に使われる
- `eByControlPoint` マッピングの場合（ UV数 = 頂点数）は現在未対応
- UVセットが複数ある場合、インデックス `0` 固定なので複数UVセットは無視される

### 対処方針（現時点では軽微）
ほぼ現状のコードで動作するが、`eByControlPoint` の場合のフォールバックがあると安全。

---

## 6. スケルトン（ボーン）の構造

| 項目 | Maya | Blender |
|------|------|---------|
| ルートボーン | 1本が多い | 複数ルートになりやすい |
| ボーン名 | `joint1` 等の英数字 | `Bone`, `Bone.001` 等（日本語名もあり） |
| FBX Cluster | 1ボーン=1クラスター | 同じ |
| バインドポーズ | FBX内にあり | FBX内にある（ただし設定漏れあり） |
| ボーン先端ノード | 含まれないことが多い | `_end` サフィックスのボーンが追加される |

### 現在のコードの対処
`GetDeformer(0)` でスキン情報を取得し、クラスター（ボーン）を列挙している。  
`bonePair` に名前→Boneポインタのマップを保持している。

```cpp
// FbxParts.cpp - InitSkelton
numBone_ = pSkinInfo_->GetClusterCount();
// ...
bonePair[ppCluster_[i]->GetLink()->GetName()] = pBoneArray_ + i;
```

### Blenderで起きる問題
- Blenderは各ボーンの**先端に `_end` ボーン**を自動追加する場合がある（影響ウェイトなし）
  - クラスター数が増えるが実害は少ない
- ボーン名に **`.001` などの連番**が付くため、名前でボーンを検索する処理が壊れやすい
  - `GetBonePosition` / `GetAnimBonePosition` はボーン名での検索をしているため要注意

### 対処方針
ボーン名の命名規則をBlender側で統一するか、ボーン名検索に前方一致・正規化を加える。

---

## 7. アニメーションのタイムモード・フレームレート

| 項目 | Maya | Blender |
|------|------|---------|
| デフォルトFPS | 24fps / 30fps | 24fps（デフォルト） |
| FBX TimeMode | `eFrames30` 等 | `eFrames24` が多い |

### 現在のコードの対処
`GetTimeMode()` でシーンのタイムモードを取得して使用している。

```cpp
// Fbx.cpp - Load
_frameRate = pFbxScene_->GetGlobalSettings().GetTimeMode();
```

```cpp
// Fbx.cpp - Draw
time.SetTime(0, 0, 0, frame, 0, 0, _frameRate);
```

### Blenderで起きる問題
- FPSが異なるとアニメーションの再生速度がずれる
- `SetAnimFrame(hSilly, 0, 1200, 1.0)` のような**フレーム数指定**はFPS依存のため、  
  BlenderのFBX（24fps）をMaya前提の範囲（30fps想定）で動かすとずれる

### 対処方針
フレーム数ではなく**秒数ベース**で管理するか、FPSを取得してUI/設定に反映させる。  
`FbxTime::GetFrameRate(pFbxScene_->GetGlobalSettings().GetTimeMode())` で実際のFPS値を取得できる。

---

## 8. メッシュ取得方法（`GetSrcObject` vs ノード探索）

| 項目 | Maya | Blender |
|------|------|---------|
| メッシュ構造 | ノード階層が整理されている | フラットな構造が多い |
| `GetSrcObject<FbxMesh>` | 問題なし | 問題なし |

### 現在のコードの対処
`pFbxScene_->GetSrcObject<FbxMesh>(i)` でフラットにメッシュを列挙している。  
この方式はMaya / Blender両方で動作する。

```cpp
// Fbx.cpp - Load
int meshCount = pFbxScene_->GetSrcObjectCount<FbxMesh>();
for (int i = 0; i < meshCount; ++i)
{
    FbxMesh* mesh = pFbxScene_->GetSrcObject<FbxMesh>(i);
    // ...
}
```

特に問題はないが、ノード階層を元にした親子Transform適用が必要な場合は  
コメントアウト中の `CheckNode` ルートに切り替える必要がある。

---

## まとめ：Blender対応時の優先対応項目

| 優先度 | 項目 | 対応場所 |
|--------|------|----------|
| ★★★ | 三角化（`Triangulate`）を有効化 | `Fbx.cpp` - `Load` |
| ★★★ | マテリアルをLambert対応に修正 | `FbxParts.cpp` - `InitMaterial` |
| ★★☆ | 座標軸補正（Apply Transform確認） | Blender側 or `InitVertex` |
| ★★☆ | テクスチャパスのフォールバック | `FbxParts.cpp` - `InitTexture` |
| ★☆☆ | ボーン名の `.001` 連番対応 | `FbxParts.cpp` - `GetBonePosition` 系 |
| ★☆☆ | FPSずれの確認・対応 | `Fbx.cpp` - `Draw` / `Player.cpp` |
