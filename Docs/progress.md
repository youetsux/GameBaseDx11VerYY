# FBX読み込み修正 進捗管理

方針は `developCommand.txt` に準拠。3段階で進める。

---

## 第1段階：初期化安定化 + スキニング式確定

> control point ベースの構造は維持したまま、未初期化メンバとスキニング式を安定化する。

| # | 内容 | 状態 |
|---|---|---|
| 1-1 | `numBone_`, `pSkinInfo_`, `ppCluster_`, `pBoneArray_`, `pWeightArray_` をコンストラクタで初期化 | ✅ 完了 |
| 1-2 | `bindShapeMatrix_` を掛ける版・掛けない版を比較し、bind pose で崩れない式を確定 | ⏭️ スキップ |
| 1-3 | 確認用ログコード削除（`[AxisSystem]` `[EvaluateGlobal]` `[AABB CHECK]` + bindShapeログ一式） | 🔲 未着手 ← **次** |
| 1-4 | `Init()` の戻り値 `E_NOTIMPL` → `S_OK` | 🔲 未着手 |
| 1-5 | 未使用コード削除（`POLY_INDEX` 構造体・コメントアウトブロック） | 🔲 未着手 |

### 1-2 スキップ理由

手持ちの FBX はすべて `bindShapeMatrix` が単位行列（T=0, R=0, S=1）のため、
WITH / WITHOUT を比較しても数学的に差が出ない。Blender 未所持・FBX の種類に限りがあるため比較不可と判断。

**現状の式（WITHOUT 版）**：`diffPose = invBind * newPose`
→ 手持ち FBX では崩れが出ていないのでこのまま継続。
→ 将来 `bindShapeMatrix` が単位行列以外の FBX が手に入った時点で再検討。

### 1-3 で削除するもの（1-2 調査のために追加したコード）

| 場所 | 削除対象 |
|---|---|
| `FbxParts.h` | `log_header_written_`, `log_with_written_`, `log_without_written_`, `log_loopCount_` メンバ変数 |
| `FbxParts.cpp` | `DrawSkinAnime` 冒頭のログ書き込みブロック一式（ヘッダー・WITH・WITHOUT） |
| `FbxParts.h` / `FbxParts.cpp` | `OnLooped()` メソッド |
| `Fbx.h` / `Fbx.cpp` | `NotifyLooped()` メソッド |
| `Model.cpp` | `Draw` 内の `NotifyLooped()` 呼び出し |
| `Main.cpp` | FPS表示前の `GetWindowTextA` による `LOG:` チェック |
| `Fbx.cpp` | `[AxisSystem BEFORE/AFTER]` `[EvaluateGlobal BEFORE/AFTER]` ログブロック |
| `FbxParts.cpp` | `[AABB CHECK]` ログブロック |

---

## 第2段階：描画頂点をポリゴン頂点単位に展開

> 第1段階でスキニングが安定したことを確認後に着手。

| # | 内容 | 状態 |
|---|---|---|
| 2-1 | `vertexCount_` を `polygonCount_ * 3` に変更 | 🔲 未着手 |
| 2-2 | `pVertexData_` を展開済み頂点配列に変更（UV・法線の上書き問題を解消） | 🔲 未着手 |
| 2-3 | `expandedToControlPoint[]` 対応表を保持（スキン重みは CP 基準のまま） | 🔲 未着手 |
| 2-4 | `InitSkelton()` のウェイト設定を2段階化（CP基準テーブル → 展開頂点へコピー） | 🔲 未着手 |
| 2-5 | `InitIndex()` をポリゴン頂点単位の連番インデックスに変更 | 🔲 未着手 |

---

## 第3段階：展開頂点基準への統一

> 第2段階完了後に着手。

| # | 内容 | 状態 |
|---|---|---|
| 3-1 | AABB 計算を展開頂点基準に統一 | 🔲 未着手 |
| 3-2 | レイキャストを展開頂点基準に統一 | 🔲 未着手 |
| 3-3 | control point / polygon vertex 混在による不整合をすべて解消 | 🔲 未着手 |

---

## 完了済み（段階外）

| 内容 |
|---|
| `DeepConvertScene` を `Import` 直後・`Triangulate` より前に実行するよう順番修正 |
| 日本語パス対応（UTF-8変換・W版API） |
| `bindShapeMatrix_` を `InitSkelton` で取得・メンバ保存（`bindPose` から分離） |
| `numBone_` 等コンストラクタ未初期化修正 |
| `Fbx::_startFrame/_endFrame` コンストラクタ初期化・`SetAnimFrame` セッター追加 |
| `Model::SetAnimFrame` → `Fbx` へのフレーム伝達修正 |
| `ViewerScene::ANIM_SPEED` を `0.5f` → `1.0f` に修正（アニメ速度が半分だった） |
| `CsvReader.cpp` : `CreateFile`系 → `std::filesystem` + `std::ifstream` に移行。UTF-8 BOM 付与 |
| `Texture.cpp` : `mbstowcs_s` → `fs::path`、冒頭に `fs::exists()` 存在チェック追加。UTF-8 BOM 付与 |
| `Fbx.cpp` : ACP→UTF-8 変換ブロック（`MultiByteToWideChar` 9行）→ `fs::path().u8string()` 2行に置換。カレントディレクトリ操作（`wchar_t`配列 + `_wsplitpath_s` + Win32 API）→ `fs::current_path()` に置換 |
| `FbxParts.cpp` : `#include <filesystem>` を明示追加（`InitTexture` での `std::filesystem::path` 使用に対応） |

---

## 注意事項

### BOM の再付与が必要なケース

`replace_string_in_file` ツールでファイルを編集すると UTF-8 BOM が失われる。  
日本語の文字列リテラルを含む以下のファイルを編集した後は必ず BOM を再付与すること。

```powershell
# BOM 再付与コマンド（ファイルパスを変えて使い回す）
$raw = Get-Content "パス\ファイル名.cpp" -Raw -Encoding UTF8
$bom = [byte[]](0xEF,0xBB,0xBF)
$body = [System.Text.Encoding]::UTF8.GetBytes($raw)
[System.IO.File]::WriteAllBytes("パス\ファイル名.cpp", $bom + $body)
```

対象ファイル：`Engine/CsvReader.cpp`、`Engine/Texture.cpp`
