# Execution Log

## 2024 — DeepConvertScene への切り替え

### 概要
FBXロード時の座標系変換を、手動のZ反転処理から `FbxAxisSystem::DirectX.DeepConvertScene()` による一括変換に移行した。

### 変更ファイル
- `Engine/Fbx.cpp`
- `Engine/FbxParts.cpp`

### 変更内容

| 箇所 | 変更前 | 変更後 |
|------|--------|--------|
| `Fbx.cpp` — `Load()` | 変換なし | `FbxAxisSystem::DirectX.DeepConvertScene(pFbxScene_)` を追加 |
| `FbxParts.cpp` — `InitVertex` 頂点位置 | `-(float)pos[2]` | `(float)pos[2]` |
| `FbxParts.cpp` — `InitVertex` 法線 | `-(float)Normal[2]` | `(float)Normal[2]` |
| `FbxParts.cpp` — `InitIndex` ワインディング | `GetPolygonVertex(j, 2-k)` | `GetPolygonVertex(j, k)` |
| `FbxParts.cpp` — `DrawSkinAnime` | `mMirror` 行列（`m[2][2] = -1`）を生成してボーン行列に乗算 | 削除。そのままポーズ計算 |

### 変更しなかった箇所
- UV の V反転 `(1.0 - uv.mData[1])` — テクスチャ軸の違いによる正しい挙動のため維持

### ビルド結果
- エラー: 0件
- 警告: 5件（変更前と同一。対応不要として合意済み）

### 動作確認
モデルの見た目・スキンアニメーション正常を確認済み。
