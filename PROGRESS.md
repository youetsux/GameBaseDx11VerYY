# ちょん切れ修正 進捗メモ

## 🎯 目標
**ちょん切れ（flat plane cut）** を Oden / GS_MotionSet（スキンメッシュ）で修正する

---

## ✅ 完了済み

| 内容 | ファイル |
|---|---|
| DrawSkinAnime デバッグコード全削除（useBindShape, FILE* logFile, log_*, NotifyLooped） | Engine/FbxParts.cpp, .h, Fbx.cpp, Fbx.h, Model.cpp |
| `Camera::SetProjection(nearZ, farZ)` 追加 | Engine/Camera.cpp, Camera.h |
| `FitCameraToAABB()` 内で dynamic near/far 設定 | ViewerScene.cpp |
| `DepthClipEnable = FALSE` に戻す | Engine/Direct3D.cpp（**未コミット・作業ディレクトリのみ**） |

> ⚠️ **Direct3D.cpp の変更は git にコミットされていない**

---

## ❌ 未解決

`ちょん切れ` は **上記2つの修正では直らなかった**（ユーザー確認済み）

---

## 🔬 調査済み・除外できた仮説

| 仮説 | 結論 |
|---|---|
| DepthClipEnable=TRUE が原因 | ❌ FALSE にしても直らない |
| near/far が狭すぎる | ❌ dynamic設定しても直らない |
| `VERTEX.uv = XMFLOAT3`(12B) vs input layout `R32G32`(8B) のミスマッチ | ❌ stride は `sizeof(VERTEX)=36B` で正しく渡される。UV.z が無視されるだけで破壊はない |
| DrawSkinAnime の行列式が違う | ❌ b5b34ec と HEAD で `bindShape * invBind * newPose` は同一 |

---

## 🔑 b5b34ec（動作OK）と HEAD（壊れ）の構造差分

| | b5b34ec（動作OK） | HEAD（ちょん切れ） |
|---|---|---|
| 頂点バッファ | **コントロールポイント数** (`GetControlPointsCount()`) | **linearIndex** (`polygonCount_ * 3`) |
| IndexBuffer | `GetPolygonVertex(j, 0/1/2)` = CP index | `j*3+0/1/2` = linear index |
| スキンウェイト代入 | `pWeightArray_[piIndex[k]]`（CP直接） | `cp2linear[cpIdx]` 経由で linIdx |
| デバッグコード | あった（削除済み） | なし |

→ **論理的には等価なはず**だが、どこかに実装バグがある可能性が高い

---

## 🔍 次に調べるべき箇所（未着手）

1. **`Fbx::Draw` / `DrawSkinAnime` の呼び出し経路**
   - スキンモデルが正しく `DrawSkinAnime` を呼んでいるか確認
   - `Engine/Fbx.cpp` の Draw 関数（290行〜あたり）※前回読み込み失敗

2. **`InitSkelton` の cp2linear マッピング**
   - `cp2linear[cpIdx]` に全 linIdx が正しく登録されているか
   - 同一 CP を共有する複数ポリゴン頂点が全部登録されているか

3. **`DrawSkinAnime` 内の頂点インデックス参照**
   - `pVertexData_[linIdx]` の linIdx 範囲が `vertexCount_` を超えていないか

4. **b5b34ec の `Init` 処理順序**
   - `SplitPoints()` 呼び出し前後で `GetControlPointsCount()` が変わっていないか

---

## 📁 関連ファイル

```
Engine/FbxParts.cpp   ← linearIndex / cp2linear / DrawSkinAnime 実装
Engine/FbxParts.h     ← VERTEX struct (uv=XMFLOAT3)
Engine/Direct3D.cpp   ← DepthClipEnable=FALSE（未コミット）
Engine/Fbx.cpp        ← Draw経路（未読）
Engine/Camera.cpp     ← SetProjection追加済み
ViewerScene.cpp       ← FitCameraToAABB内でSetProjection呼び出し
```

---

## 🌿 Git 状態

| | |
|---|---|
| ブランチ | `b_blenderLoad` |
| 動作確認済みコミット | `b5b34ec` |
| 現 HEAD | `0d8524e` |
| 未コミット変更 | `Direct3D.cpp`（DepthClipEnable=FALSE） |
