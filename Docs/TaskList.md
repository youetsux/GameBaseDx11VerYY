# FBX Loader 修正タスクリスト

> **ルール**
> - 作業完了のたびに必ずこのファイルを更新する
> - ファイルが大きくなり読みにくくなった時点で分割する
> - 変更箇所（ファイル名・関数名・行番号）を完了時に追記する

---

## セッションメモ（2025-xx-xx 確認済み）

### 現状確認で分かったこと

#### ✅ すでに対応済み
| 項目 | 場所 |
|------|------|
| `numBone_` の未初期化 | 両コンストラクタで `numBone_(0)` 済み |
| 座標系変換 (`DeepConvertScene`) | `Fbx::Load` 内で Maya/Blender 共通に実施 |
| 自動三角化 (非三角ポリゴン検出) | `Fbx::Load` 内で `Triangulate` を条件付き実行 |
| 日本語パス対応 (Shift-JIS → UTF-8) | `Fbx::Load` 内で変換済み |
| AABB 計算 | `FbxParts::Init(FbxMesh*)` で `numBone_==0` 分岐済み |
| `FbxChecker` (7項目事前チェック) | `ViewerScene::LoadFbx` で実行・MessageBox 表示済み |
| アニメフレーム数取得 | `ViewerScene::LoadFbx` で `AnimStack` から取得済み |

#### ❌ 未修正の問題
| # | 問題 | 場所 | リスク |
|---|------|------|--------|
| TASK-01 | `Init()` 成功時も `E_NOTIMPL` を返す | `FbxParts.cpp` 末尾 | 低 |
| TASK-02 | `InitMaterial` で Phong に C スタイル強制キャスト | `FbxParts.cpp` 両オーバーロード | **高・Blender でクラッシュ** |
| TASK-03 | `InitTexture` で `GetRelativeFileName` のみ使用、フォールバックなし | `FbxParts.cpp` | 中・Blender でテクスチャ未表示 |
| TASK-04 | `InitVertex` が control point 基準で法線・UV を上書き | `FbxParts.cpp` | 中・ハードエッジ・UVシームで壊れる |
| TASK-05 | スキン行列式 `bindShape * inv(bindPose) * newPose` が不正 | `FbxParts.cpp DrawSkinAnime` | 高・スキンメッシュの変形がずれる |
| TASK-06 | 調査用 `Debug::Log` が大量に残存 | `Fbx.cpp` / `FbxParts.cpp` | 低・ログ汚染 |

### 方針決定
- **入り口（`Fbx::Load`）は Maya / Blender で分けない**
- 差異は `DeepConvertScene` / `Triangulate` / `InitMaterial` / `InitTexture` / `InitVertex` 内で吸収
- 別クラス・別エントリーポイントは作らない

---

## 凡例

| 記号 | 意味 |
|------|------|
| ⬜ | 未着手 |
| 🔄 | 作業中 |
| ✅ | 完了 |

---

## 全体方針

入り口（`Fbx::Load`）は **Maya / Blender で共通**。
差異は `DeepConvertScene` / `Triangulate` / `InitMaterial` / `InitTexture` / `InitVertex` の中で吸収する。
ソースコンバーターを別クラスにはしない。

---

## タスク一覧

優先度は「壊れない順番」で並べてある。
上のタスクが下のタスクの土台になっている。

---

### TASK-01 　`Init()` の戻り値を `S_OK` に修正
**優先度 : ★☆☆ / リスク : 低 / 副作用 : なし**

#### 背景
`FbxParts::Init(FbxNode*)` と `Init(FbxMesh*)` が成功時も `E_NOTIMPL` を返している。
現状は呼び出し側が戻り値を無視しているため表面化していないが、
将来エラーハンドリングを追加したときに誤検知する。

#### 修正内容
- `FbxParts.cpp` の `Init(FbxNode*)` 末尾 `return E_NOTIMPL;` → `return S_OK;`
- `FbxParts.cpp` の `Init(FbxMesh*)` 末尾 `return E_NOTIMPL;` → `return S_OK;`

#### 変更ファイル
| ファイル | 関数 | 変更箇所 |
|----------|------|----------|
| `Engine/FbxParts.cpp` | `Init(FbxNode*)` | 末尾 return |
| `Engine/FbxParts.cpp` | `Init(FbxMesh*)` | 末尾 return |

#### ステータス
⬜ 未着手

---

### TASK-02 　`InitMaterial` の Phong 強制キャストを安全な分岐に修正
**優先度 : ★★★ / リスク : 高（Blenderでクラッシュ） / 副作用 : Mayaは変わらず動く**

#### 背景
`InitMaterial(FbxNode*)` / `InitMaterial(FbxMesh*)` の両方で
`FbxSurfacePhong* pPhong = (FbxSurfacePhong*)pMaterial;` と C スタイルキャストをしている。
Blenderの FBX は Lambert マテリアルで出力されるため、このキャストは不正なポインタ操作になる。
クラッシュするか、マテリアル色がゴミ値になる。

#### 修正内容
- クラスIDで `Phong` / `Lambert` / 不明 を分岐する
- `Lambert` の場合は `Ambient` / `Diffuse` だけ取り、`Specular` / `Shininess` は 0 固定
- `FbxSurfacePhong*` へのキャストは `Phong` 確認後に `static_cast` で行う
- 両オーバーロード（`FbxNode*` 版・`FbxMesh*` 版）を同じロジックで修正する

#### 変更ファイル
| ファイル | 関数 | 変更箇所 |
|----------|------|----------|
| `Engine/FbxParts.cpp` | `InitMaterial(FbxNode*)` | Phong 強制キャスト → ClassId 分岐 |
| `Engine/FbxParts.cpp` | `InitMaterial(FbxMesh*)` | 同上 |

#### ステータス
⬜ 未着手

---

### TASK-03 　`InitTexture` のテクスチャパス・フォールバック追加
**優先度 : ★★☆ / リスク : 中（Blenderでテクスチャが出ない） / 副作用 : なし**

#### 背景
`InitTexture` は `GetRelativeFileName()` だけを使っている。
Blenderはテクスチャに絶対パスを埋め込むため、相対パスが空文字になる場合がある。
その場合 `_splitpath_s` でパースしても空文字になりテクスチャが未ロードになる。

#### 修正内容
- `GetRelativeFileName()` が空のとき `GetFileName()` にフォールバックする
- どちらもパスに `\` / `/` が混在するケースに備え `_splitpath_s` に渡す前にパスを正規化する

#### 変更ファイル
| ファイル | 関数 | 変更箇所 |
|----------|------|----------|
| `Engine/FbxParts.cpp` | `InitTexture` | `GetRelativeFileName` → フォールバック追加 |

#### ステータス
⬜ 未着手

---

### TASK-04 　`InitVertex` を polygon vertex 展開方式に変更
**優先度 : ★★☆ / リスク : 中（法線・UVが潰れる） / 副作用 : TASK-05（スキン）に影響するので先に完了すること**

#### 背景
現状は `pVertexData_[controlPointIndex]` に法線・UV を上書きしている。
同一 control point を複数 polygon vertex が共有している場合（ハードエッジ・UV シーム）、
最後に書き込んだ値で前の値が潰れる。
MayaのFBXでは `SplitPoints` によりある程度展開されるが、一般の FBX では保証されない。

#### 修正内容
- 頂点配列を `polygonVertexCount_`（= ポリゴン数 × 3）サイズに変更する
- ループを `poly` × `vertex` で回し、各 polygon vertex ごとに1エントリ書く
- インデックスは `poly * 3 + vertex` を直接使う（`GetPolygonVertex` の結果は使わない）
- `vertexCount_` をこの展開済みサイズで上書きする
- `InitSkelton` / `DrawSkinAnime` / `RayCast` / `AABB` 計算も影響を受けるので整合を取る

#### 変更ファイル
| ファイル | 関数 | 変更箇所 |
|----------|------|----------|
| `Engine/FbxParts.cpp` | `InitVertex` | control point 基準 → polygon vertex 展開 |
| `Engine/FbxParts.cpp` | `InitIndex` | インデックス生成をシンプルな連番に変更 |
| `Engine/FbxParts.cpp` | `InitSkelton` | ウェイト配列のサイズ・マッピングを展開済みに合わせる |
| `Engine/FbxParts.cpp` | `DrawSkinAnime` | 展開済み頂点数ループに合わせる |

#### ステータス
⬜ 未着手

---

### TASK-05 　スキン行列式の修正（bindShapeMatrix の扱い）
**優先度 : ★★☆ / リスク : 高（スキンメッシュの変形がずれる） / 副作用 : TASK-04 完了後に着手すること**

#### 背景
現在の式：
```
diffPose = bindShape * inv(bindPose) * newPose
```
`bindShape` は `GetTransformMatrix()`（= メッシュのバインド時グローバル変換）。
`bindPose` は `GetTransformLinkMatrix()`（= ボーンのバインド時グローバル変換）。
`newPose`  は `GetNodeGlobalTransform(link, time)`（= ボーンの現在グローバル変換）。

正しい式は：
```
diffPose = inv(bindShape) × inv(bindPose) × newPose
```
あるいはメッシュノードに変換がなければ `bindShape ≈ Identity` なので前掛けを外す。
どちらにせよ「前掛け」ではなく「逆行列で打ち消す」側に使うのが正しい。
メッシュノードに移動・回転・スケールがあると1段ずれた変形になる。

#### 修正内容
- `bindShapeMatrix_` の使い方を `inv(bindShape)` に変更する
  - または `GetTransformMatrix` と `GetTransformLinkMatrix` の両方を `inv` を挟んで正しく合成する
- Maya / Blender 両方で単純な回転アニメを目視確認する

#### 変更ファイル
| ファイル | 関数 | 変更箇所 |
|----------|------|----------|
| `Engine/FbxParts.cpp` | `DrawSkinAnime` | `diffPose` の計算式 |
| `Engine/FbxParts.cpp` | `InitSkelton` | `bindShapeMatrix_` の取得方法の見直し |

#### ステータス
⬜ 未着手

---

### TASK-06 　デバッグログの整理
**優先度 : ★☆☆ / リスク : 低 / 副作用 : なし**

#### 背景
`Fbx.cpp` / `FbxParts.cpp` に調査用の `Debug::Log` が多数残っている。
- `[AxisSystem BEFORE/AFTER]`
- `[EvaluateGlobal BEFORE/AFTER]`
- `[AABB CHECK]`
など。リリース・他の人が読む前に除去が必要。

#### 修正内容
- 調査用 `Debug::Log` を削除する
- ただし `[Fbx] Non-triangle polygons detected` のような運用上必要なログは残す

#### 変更ファイル
| ファイル | 変更箇所 |
|----------|----------|
| `Engine/Fbx.cpp` | `Load` 内のデバッグログブロック |
| `Engine/FbxParts.cpp` | `Init(FbxMesh*)` 内のデバッグログブロック |

#### ステータス
⬜ 未着手

---

## 依存関係

```
TASK-01  (戻り値)        → 単独で実施可
TASK-02  (マテリアル)    → 単独で実施可
TASK-03  (テクスチャ)    → 単独で実施可
TASK-04  (頂点展開)      → TASK-01,02,03 の後が望ましい（ビルドが通ってから大きい変更）
TASK-05  (スキン行列)    → TASK-04 完了後に実施
TASK-06  (ログ整理)      → TASK-04,05 完了後が望ましい（調査中のログを消さないため）
```

---

## 変更履歴

| 日付 | タスク | 変更内容 | 変更ファイル |
|------|--------|----------|-------------|
| 2025-xx-xx | -      | タスク表新規作成・セッションメモ追記 | `Docs/TaskList.md` |
