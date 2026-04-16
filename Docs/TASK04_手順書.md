# TASK-04 手順書 : InitVertex を polygon vertex 展開方式に変更

> **前提**
> - TASK-01 / 02 / 03 完了済み（コミット済み `bd3ec83`）
> - ブランチ : `b_blenderLoad`
> - 対象ファイル : `Engine/FbxParts.cpp` のみ

---

## なぜ変えるのか（背景）

### 現状の問題

```
頂点配列サイズ = GetControlPointsCount()   ← 共有頂点込みの"少ない"数

for poly:
  for vertex 0..2:
    index = GetPolygonVertex(poly, vertex)  ← control point の index
    pVertexData_[index].normal = ...        ← 同じ index に何度も上書き！
    pVertexData_[index].uv     = ...        ← 最後に書いたもので上書き！
```

- ハードエッジ・UV シームで**法線・UV が潰れる**
- `MappingMode = eByControlPoint` の UV は**完全に取れない**

### 目標の構造

```
頂点配列サイズ = polygonCount * 3          ← 全 polygon vertex を展開した数

for poly:
  for vertex 0..2:
    linearIndex = poly * 3 + vertex        ← 重複なしの通し番号
    pVertexData_[linearIndex].position = ...
    pVertexData_[linearIndex].normal   = ...
    pVertexData_[linearIndex].uv       = ...  ← MappingMode を正しく処理
```

---

## 作業手順（5 Step に細分化）

> **コミットのタイミング**  
> Step 1-A と Step 2-A は「中間状態」なのでコミットしない。  
> ビルドが通ることだけ確認してすぐ次の Step に進む。

---

### ⬜ Step 1-A : `InitVertex` を linearIndex 方式に変更（ビルド確認のみ）

**影響関数 :** `InitVertex`  
**確認方法 :** ビルドエラーが出ないことだけ確認。描画は壊れていてよい（`InitIndex` が旧来のままなので）。コミットしない。

#### 変更内容

```cpp
// --------- 変更前 ---------
pVertexData_ = new VERTEX[vertexCount_];   // vertexCount_ = controlPointCount

for (DWORD poly = 0; poly < polygonCount_; poly++)
{
    for (int vertex = 0; vertex < 3; vertex++)
    {
        int index = mesh->GetPolygonVertex(poly, vertex);  // control point index
        pVertexData_[index].position = ...;
        pVertexData_[index].normal   = ...;
        // UV: ReferenceMode だけ見ていて MappingMode を無視している
        pVertexData_[index].uv = ...;
    }
}
bd_vertex.ByteWidth = sizeof(VERTEX) * mesh->GetControlPointsCount();


// --------- 変更後 ---------
// (1) vertexCount_ を polygon vertex 展開済みサイズに上書き
vertexCount_ = polygonCount_ * 3;
pVertexData_ = new VERTEX[vertexCount_];

FbxLayerElementUV* pUV = mesh->GetLayer(0)->GetUVs();
FbxStringList uvSetNames;
mesh->GetUVSetNames(uvSetNames);
FbxString uvSetName = uvSetNames.GetStringAt(0);

for (DWORD poly = 0; poly < polygonCount_; poly++)
{
    for (int vertex = 0; vertex < 3; vertex++)
    {
        int linearIndex = (int)(poly * 3 + vertex);    // ← 通し番号
        int cpIndex     = mesh->GetPolygonVertex(poly, vertex); // 位置取得にだけ使う

        // 位置
        FbxVector4 pos = mesh->GetControlPointAt(cpIndex);
        pVertexData_[linearIndex].position = XMFLOAT3((float)pos[0], (float)pos[1], (float)pos[2]);

        // 法線
        FbxVector4 normal;
        mesh->GetPolygonVertexNormal(poly, vertex, normal);
        pVertexData_[linearIndex].normal = XMFLOAT3((float)normal[0], (float)normal[1], (float)normal[2]);

        // UV（MappingMode + ReferenceMode を両方チェック）
        FbxVector2 uv;
        bool unmapped = false;
        if (pUV->GetMappingMode() == FbxLayerElement::eByPolygonVertex)
        {
            if (pUV->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
            {
                int uvIndex = mesh->GetTextureUVIndex(poly, vertex, FbxLayerElement::eTextureDiffuse);
                uv = pUV->GetDirectArray().GetAt(uvIndex);
            }
            else // eDirect
            {
                mesh->GetPolygonVertexUV(poly, vertex, uvSetName, uv, unmapped);
            }
        }
        else // eByControlPoint
        {
            if (pUV->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
            {
                int uvIndex = pUV->GetIndexArray().GetAt(cpIndex);
                uv = pUV->GetDirectArray().GetAt(uvIndex);
            }
            else // eDirect
            {
                uv = pUV->GetDirectArray().GetAt(cpIndex);
            }
        }
        pVertexData_[linearIndex].uv = { (float)uv[0], (float)(1.0 - uv[1]), 0.0f };
    }
}

// (2) バッファサイズも展開済みサイズに変更
bd_vertex.ByteWidth = sizeof(VERTEX) * vertexCount_;
```

---

### ⬜ Step 1-B : `InitIndex` を linearIndex 連番方式に変更（静的メッシュ目視・コミット）

**影響関数 :** `InitIndex`  
**確認方法 :** ビルド確認 → 静的メッシュを D&D して形状・テクスチャが正しく出るか目視 → **コミット**

#### 変更内容

```cpp
// --------- 変更前 ---------
// GetPolygonVertex(j, k) で control point index を拾っていた

// --------- 変更後 ---------
// linearIndex = poly * 3 + vertex が頂点の実体なのでそのまま詰める

for (DWORD i = 0; i < materialCount_; i++)
{
    count = 0;
    DWORD* pIndex = new DWORD[polygonCount_ * 3];

    for (DWORD j = 0; j < polygonCount_; j++)
    {
        FbxLayerElementMaterial* mtl = mesh->GetLayer(0)->GetMaterials();
        int mtlId = mtl->GetIndexArray().GetAt(j);
        if (mtlId == (int)i)
        {
            pIndex[count++] = j * 3 + 0;
            pIndex[count++] = j * 3 + 1;
            pIndex[count++] = j * 3 + 2;
        }
    }
    // 以降のバッファ生成は変更なし
}
```

---

### ⬜ Step 2-A : `InitSkelton` に cp2linear テーブルを追加（ビルド確認のみ）

**影響関数 :** `InitSkelton`  
**確認方法 :** ビルドエラーが出ないことだけ確認。ウェイト書き込みはまだ旧来のまま。コミットしない。

#### 変更内容

`InitSkelton` の `numBone_` / `ppCluster_` を確定させた直後（`pWeightArray_` 確保の前）に追加する。

```cpp
// cp2linear[cpIndex] = { linearIndex0, linearIndex1, ... }
std::vector<std::vector<int>> cp2linear(pMesh->GetControlPointsCount());
for (DWORD poly = 0; poly < polygonCount_; poly++)
{
    for (int v = 0; v < 3; v++)
    {
        int cpIdx  = pMesh->GetPolygonVertex(poly, v);
        int linIdx = (int)(poly * 3 + v);
        cp2linear[cpIdx].push_back(linIdx);
    }
}
```

---

### ⬜ Step 2-B : `InitSkelton` のウェイト書き込みを cp2linear 経由に変更（スキンメッシュ目視・コミット）

**影響関数 :** `InitSkelton`  
**確認方法 :** ビルド確認 → スキンメッシュを D&D してバインドポーズが崩れないか目視 → **コミット**

#### 変更内容

既存のウェイト書き込みループ（`pWeightArray_[piIndex[k]]` を直接参照している箇所）を cp2linear 経由に書き換える。

```cpp
// --------- 変更前 ---------
for (int k = 0; k < numIndex; k++)
{
    for (int m = 0; m < 4; m++)
    {
        // piIndex[k] = control point index をそのまま使っている
        if (pdWeight[k] > pWeightArray_[piIndex[k]].pBoneWeight[m])
        {
            // ... pWeightArray_[piIndex[k]] に書き込み
        }
    }
}

// --------- 変更後 ---------
for (int k = 0; k < numIndex; k++)
{
    int cpIdx = piIndex[k];
    // この control point に対応する全 linearIndex にウェイトを書く
    for (int linIdx : cp2linear[cpIdx])
    {
        for (int m = 0; m < 4; m++)
        {
            if (m >= numBone_) break;
            if (pdWeight[k] > pWeightArray_[linIdx].pBoneWeight[m])
            {
                for (int n = numBone_ - 1; n > m; n--)
                {
                    pWeightArray_[linIdx].pBoneIndex[n]  = pWeightArray_[linIdx].pBoneIndex[n - 1];
                    pWeightArray_[linIdx].pBoneWeight[n] = pWeightArray_[linIdx].pBoneWeight[n - 1];
                }
                pWeightArray_[linIdx].pBoneIndex[m]  = i;
                pWeightArray_[linIdx].pBoneWeight[m] = (float)pdWeight[k];
                break;
            }
        }
    }
}
```

---

### ⬜ Step 3 : `DrawSkinAnime` の整合確認（アニメ目視・コミット）

**影響関数 :** `DrawSkinAnime`  
**確認方法 :** スキンメッシュのアニメーションを再生して変形が正しいか目視 → **コミット**

`vertexCount_` が展開済みサイズになっているだけなので、  
ループ自体は `for (DWORD i = 0; i < vertexCount_; i++)` のままで動くはず。  
変更が必要なら対応、問題なければそのままコミット。

---

## 作業チェックリスト

- [x] Step 1-A : `InitVertex` 変更・ビルド確認（コミットしない）
- [ ] Step 1-B : `InitIndex` 変更・ビルド確認・静的メッシュ目視確認・**コミット**
- [ ] Step 2-A : `InitSkelton` に cp2linear テーブル追加・ビルド確認（コミットしない）
- [ ] Step 2-B : `InitSkelton` ウェイト書き込みを cp2linear 経由に変更・ビルド確認・スキンメッシュ目視確認・**コミット**
- [ ] Step 3  : `DrawSkinAnime` 整合確認・アニメーション目視確認・**コミット**
- [ ] `TaskList.md` の TASK-04 ステータスを ✅ に更新

---

## 注意事項

- **Step 1-A 単体では描画が壊れる**（InitIndex が旧来のまま）。ビルドが通ることだけ確認して Step 1-B に進む
- **Step 2-A 単体ではウェイトがズレたまま**。ビルドが通ることだけ確認して Step 2-B に進む
- `vertexCount_` は `InitVertex` の中で `polygonCount_ * 3` に上書きする。`Init()` で `GetControlPointsCount()` を代入している箇所はそのままでよい（`InitVertex` 内で上書きされる）
- `SplitPoints()` の呼び出しは **残したままでよい**（呼んでも害はない）
