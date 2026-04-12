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

## 作業手順（3 Step に分割）

### ✅ Step 1 : `InitVertex` + `InitIndex` を変える（静的メッシュ確認）

**影響関数 :** `InitVertex` / `InitIndex`  
**確認方法 :** スキンなし静的メッシュの FBX をビューワーに D&D して形状・テクスチャが正しく出るか目視

#### InitVertex の変更点

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
        // UV: eIndexToDirect / eDirect の分岐（MappingMode未チェック）
        pVertexData_[index].uv = ...;
    }
}

// バッファサイズも controlPointCount 基準
bd_vertex.ByteWidth = sizeof(VERTEX) * mesh->GetControlPointsCount();


// --------- 変更後 ---------
// vertexCount_ を polygon vertex 展開済みサイズに上書き
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
        int linearIndex = (int)(poly * 3 + vertex);
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

// バッファサイズも展開済みサイズ
bd_vertex.ByteWidth = sizeof(VERTEX) * vertexCount_;
```

#### InitIndex の変更点

インデックスが単純な連番になるので大幅にシンプルになる。

```cpp
// --------- 変更前 ---------
// マテリアル毎にポリゴンを走査して GetPolygonVertex で index を拾う

// --------- 変更後 ---------
// linearIndex = poly * 3 + vertex が頂点の実体なので
// マテリアルが一致するポリゴンの linearIndex をそのまま詰める

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

**Step 1 完了後にコミットすること。**

---

### ⬜ Step 2 : `InitSkelton` のウェイトマッピングを展開済みに合わせる（スキンメッシュ確認）

**影響関数 :** `InitSkelton`  
**確認方法 :** スキンメッシュ FBX をビューワーに D&D してバインドポーズが崩れないか目視

#### 変更の考え方

FBX SDK のウェイト情報は **controlPoint 基準**で持っている。  
展開後は頂点が `linearIndex = poly * 3 + vertex` になるので、  
`controlPoint → [linearIndex, ...]` の逆引きテーブルが必要。

```cpp
// Step 2 での追加処理（InitSkelton 冒頭）

// controlPoint → linearIndex の逆引きテーブルを作成
// cp2linear[cpIndex] = { linearIndex0, linearIndex1, ... }
std::vector<std::vector<int>> cp2linear(mesh->GetControlPointsCount());
for (DWORD poly = 0; poly < polygonCount_; poly++)
{
    for (int v = 0; v < 3; v++)
    {
        int cpIdx  = mesh->GetPolygonVertex(poly, v);
        int linIdx = (int)(poly * 3 + v);
        cp2linear[cpIdx].push_back(linIdx);
    }
}

// ウェイト配列を展開済みサイズで確保
pWeightArray_ = new FbxParts::Weight[vertexCount_];  // vertexCount_ は展開済みサイズ
for (DWORD i = 0; i < vertexCount_; i++)
{
    pWeightArray_[i].posOrigin    = pVertexData_[i].position;
    pWeightArray_[i].normalOrigin = pVertexData_[i].normal;
    pWeightArray_[i].pBoneIndex   = new int[numBone_];
    pWeightArray_[i].pBoneWeight  = new float[numBone_];
    for (int j = 0; j < numBone_; j++)
    {
        pWeightArray_[i].pBoneIndex[j]  = -1;
        pWeightArray_[i].pBoneWeight[j] = 0.0f;
    }
}

// ボーンのウェイトを controlPoint → linearIndex に展開して書き込む
for (int i = 0; i < numBone_; i++)
{
    int    numIdx  = ppCluster_[i]->GetControlPointIndicesCount();
    int*   piIndex = ppCluster_[i]->GetControlPointIndices();
    double* pdWeight = ppCluster_[i]->GetControlPointWeights();

    for (int k = 0; k < numIdx; k++)
    {
        int cpIdx = piIndex[k];
        // このcontrolPointに対応する全linearIndexにウェイトを書く
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
}
```

**Step 2 完了後にコミットすること。**

---

### ⬜ Step 3 : `DrawSkinAnime` の整合確認

**影響関数 :** `DrawSkinAnime`  
**確認方法 :** スキンメッシュのアニメーションを再生して変形が正しいか目視

`vertexCount_` が展開済みサイズになっているだけなので、  
ループ自体は `for (DWORD i = 0; i < vertexCount_; i++)` のままで動くはず。  
動作確認して問題なければそのままコミット。

---

## 作業チェックリスト

- [ ] Step 1 : `InitVertex` 変更・ビルド確認・静的メッシュ目視確認・コミット
- [ ] Step 1 : `InitIndex` 変更・ビルド確認・静的メッシュ目視確認・コミット
- [ ] Step 2 : `InitSkelton` 変更・ビルド確認・スキンメッシュ目視確認・コミット
- [ ] Step 3 : `DrawSkinAnime` 整合確認・コミット
- [ ] `TaskList.md` の TASK-04 ステータスを ✅ に更新

---

## 注意事項

- **Step 1 が通ってからStep 2 に進むこと**（スキンと頂点のバグを混ぜない）
- `vertexCount_` は `InitVertex` の中で `polygonCount_ * 3` に上書きする。`Init()` で `GetControlPointsCount()` を代入している箇所はそのままでよい（`InitVertex` 内で上書きされる）
- `SplitPoints()` の呼び出しは **残したままでよい**（呼んでも害はない）
