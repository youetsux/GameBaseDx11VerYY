# 変更履歴

> **ルール：このファイルは追記専用。過去の記録は絶対に削除・上書きしないこと。**
> 変更が1つでもあれば、日付・理由・変更箇所を末尾に追記する。

---

## 2025-07-?? — `SetAnimFrame` 伝達バグ修正 / bindShapeログ出力安定化

### 背景

`D:\Projects_Log` へ bindShape あり・なしの比較ログを出力しようとしたところ、
`DrawSkinAnime` 内の `endFrame` が常に `1` になっており、`loopCount` が想定外の値になってログが正しく出力されない問題が発覚した。

原因は2点：

1. `Fbx::_startFrame` / `_endFrame` がコンストラクタで未初期化（不定値または 0 のまま）
2. `_startFrame` / `_endFrame` が `private` であるため、`Model::ModelData::SetAnimFrame` からの直接アクセスがコンパイルエラーになっていた（アクセス修飾子違反）

---

### 変更ファイル一覧

| ファイル | 変更概要 |
|---|---|
| `Engine/Fbx.cpp` | コンストラクタで `_startFrame(0)`, `_endFrame(0)` を初期化 |
| `Engine/Fbx.h` | `SetAnimFrame(int start, int end)` パブリックセッターを追加 |
| `Engine/Model.h` | `ModelData::SetAnimFrame` 内の直接メンバアクセスをセッター呼び出しに変更 |
| `Engine/FbxParts.cpp` | `time.GetFrameCount()` を `totalFrame` 変数に1回取得して使い回すよう変更 |

---

### 変更詳細

#### `Engine/Fbx.cpp`

```cpp
// 変更前
Fbx::Fbx():_animSpeed(0), pFbxManager_(nullptr), pFbxScene_(nullptr), pAnimEvaluator_(nullptr)

// 変更後
Fbx::Fbx():_animSpeed(0), _startFrame(0), _endFrame(0), pFbxManager_(nullptr), pFbxScene_(nullptr), pAnimEvaluator_(nullptr)
```

#### `Engine/Fbx.h`

```cpp
// 追加（publicセクション冒頭）
void SetAnimFrame(int start, int end) { _startFrame = start; _endFrame = end; }
```

#### `Engine/Model.h`

```cpp
// 変更前
if (pFbx)
{
    pFbx->_startFrame = start;
    pFbx->_endFrame   = end;
}

// 変更後
if (pFbx)
{
    pFbx->SetAnimFrame(start, end);
}
```

#### `Engine/FbxParts.cpp` — `DrawSkinAnime` 冒頭

```cpp
// 変更前
int loopCount  = (int)(time.GetFrameCount() / endFrame);
int localFrame = (int)(time.GetFrameCount() % endFrame);
// ...ログ出力内でも time.GetFrameCount() を直接呼び出し（複数箇所）

// 変更後
long long totalFrame = time.GetFrameCount();
int loopCount  = (int)(totalFrame / endFrame);
int localFrame = (int)(totalFrame % endFrame);
// ...ログ出力内でも totalFrame を参照
```

---

### 修正後の動作

- `Model::SetAnimFrame(handle, start, end, speed)` を呼ぶと `Fbx::_startFrame/_endFrame` に正しく反映される
- `DrawSkinAnime` 内の `endFrame` が設定値通りになり、`loopCount` / `localFrame` の計算が意図通りに動く
- bindShape あり（loop:0）・なし（loop:1）のログが `D:\Projects_Log\bindshape_log.txt` に正しく出力される

---

### 残タスク（`progress.md` 第1段階）

| # | 内容 | 状態 |
|---|---|---|
| 1-2 | `bindShapeMatrix_` を掛ける版・掛けない版のログ比較で式を確定 | 🔲 未着手（ログ出力が直ったので次に実施） |
| 1-3 | 確認用ログコード削除 | 🔲 未着手 |
| 1-4 | `Init()` 戻り値 `E_NOTIMPL` → `S_OK` | 🔲 未着手 |
| 1-5 | 未使用コード削除（`POLY_INDEX` 構造体・コメントアウトブロック） | 🔲 未着手 |

---

## 2025-07-?? — bindShapeログ ループカウント方式の修正

### 背景

前回の修正後も、ログが正しく取れない構造的問題が2点残っていた。

**問題①：`loopCount` が常に 0 になる**

`Model::Draw` は `nowFrame`（`startFrame`〜`endFrame` の範囲でループする値）を
`Fbx::Draw` → `FbxTime::SetTime` に渡している。
そのため `time.GetFrameCount()` は常に `startFrame`〜`endFrame` の範囲内に収まり、
`totalFrame / endFrame` で計算した `loopCount` は **絶対に 1 以上にならない**。
→ loop:1（WITHOUT bindShape）のログが永遠に出力されない。

**問題②：`D:/Projects_Log/` ディレクトリが存在しない場合 `fopen_s` が無言で失敗する**

ディレクトリが存在しないと `logFile == nullptr` のまま進み、
ログが一切書かれない可能性があった。

### 修正内容

`loopCount` を「`totalFrame` から計算」する方式をやめ、
**アニメーションの折り返しを検出する静的カウンタ `s_loopCount`** に変更した。
前フレーム番号 `s_prevFrame` を保持し、現フレームが前フレームより小さくなった瞬間を
折り返しとして `s_loopCount++` する。

また `s_header_logged` のタイミングで `CreateDirectoryA` を呼び、
ディレクトリを自動作成するようにした。

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/FbxParts.cpp` | `DrawSkinAnime` のログ制御を `s_loopCount` / `s_prevFrame` 方式に刷新。`CreateDirectoryA` 追加。`loopCount` 変数を削除し `s_loopCount` に統一 |

### 変更詳細

#### `Engine/FbxParts.cpp` — `DrawSkinAnime` 冒頭

```cpp
// 変更前
static bool s_header_logged  = false;
static bool s_logged_with    = false;
static bool s_logged_without = false;
FILE* logFile = nullptr;
int endFrame   = parent_->_endFrame > 0 ? parent_->_endFrame : 1;
long long totalFrame = time.GetFrameCount();
int loopCount  = (int)(totalFrame / endFrame);   // ← nowFrameが折り返すため常に0
int localFrame = (int)(totalFrame % endFrame);
int midFrame   = endFrame / 2;
bool useBindShape = (loopCount % 2 == 0);

// 変更後
static bool s_header_logged  = false;
static bool s_logged_with    = false;
static bool s_logged_without = false;
static int  s_loopCount      = 0;       // 折り返し回数を静的カウンタで管理
static long long s_prevFrame = -1;      // 折り返し検出用の前フレーム番号
FILE* logFile = nullptr;
int endFrame   = parent_->_endFrame > 0 ? parent_->_endFrame : 1;
long long totalFrame = time.GetFrameCount();
bool useBindShape = (s_loopCount % 2 == 0);

// ディレクトリ自動作成を s_header_logged ブロックに追加
CreateDirectoryA("D:/Projects_Log", nullptr);

// 折り返し検出
if (s_prevFrame >= 0 && totalFrame < s_prevFrame) { s_loopCount++; }
s_prevFrame = totalFrame;
```

#### `Engine/FbxParts.cpp` — ボーンループ内のフラグ更新

```cpp
// 変更前
if (loopCount == 0) s_logged_with    = true;
if (loopCount == 1) s_logged_without = true;

// 変更後
if (s_loopCount == 0) s_logged_with    = true;
if (s_loopCount == 1) s_logged_without = true;
```

---

## 2025-07-?? — bindShapeログ 取得条件の修正（ログが止まる問題）

### 背景

実際にログファイルを確認したところ、ヘッダー行（`bindShapeMatrix` の T/R/S）しか出力されず、
WITH / WITHOUT のボーン行列ログが全く書かれていなかった。

原因を調査した結果、2点判明した。

**問題①：ViewerScene はロード直後 `animSpeed=0.0f`（静止状態）**

`animSpeed=0` では `nowFrame` が進まないため折り返しが起きず、
`s_loopCount` が 0 のまま WITH ログの条件に到達しない。

**問題②：ログ取得条件が `totalFrame == endFrame`（厳密一致）**

`animSpeed=1.0f` で再生しても整数ステップで `endFrame` をちょうど踏まない場合があり、
条件を満たすフレームが存在しないことがある。

### 修正内容

ログ取得のタイミングを「特定フレーム番号との一致」から
**「折り返し発生の瞬間（`justLooped == true`）」** に変更した。

- loop:0 → loop:1 に切り替わった瞬間（`s_loopCount == 1 && justLooped`）に WITH のログを取得
- loop:1 → loop:2 に切り替わった瞬間（`s_loopCount == 2 && justLooped`）に WITHOUT のログを取得
- フラグ更新条件も `s_loopCount == 1 / 2` に合わせて修正

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/FbxParts.cpp` | ログ取得条件を `totalFrame == endFrame` から `justLooped` フラグ方式に変更。`s_logged_with/without` のフラグ更新条件を `1/2` に修正 |

### 変更詳細

#### `Engine/FbxParts.cpp` — 折り返し検出 & ログ開始条件

```cpp
// 変更前
if (s_prevFrame >= 0 && totalFrame < s_prevFrame) { s_loopCount++; }
s_prevFrame = totalFrame;
if (!s_logged_with && s_loopCount == 0 && totalFrame == (long long)endFrame) { ... }
else if (!s_logged_without && s_loopCount == 1 && totalFrame == (long long)endFrame) { ... }

// 変更後
bool justLooped = false;
if (s_prevFrame >= 0 && totalFrame < s_prevFrame) { s_loopCount++; justLooped = true; }
s_prevFrame = totalFrame;
if (!s_logged_with    && s_loopCount == 1 && justLooped) { ... }  // loop:0完了の瞬間
else if (!s_logged_without && s_loopCount == 2 && justLooped) { ... }  // loop:1完了の瞬間
```

#### `Engine/FbxParts.cpp` — フラグ更新

```cpp
// 変更前
if (s_loopCount == 0) s_logged_with    = true;
if (s_loopCount == 1) s_logged_without = true;

// 変更後
if (s_loopCount == 1) s_logged_with    = true;
if (s_loopCount == 2) s_logged_without = true;
```

---

## 2025-07-?? — bindShapeログ 3点修正（静止中スキップ・文字化け・animSpeed伝達）

### 背景

ログを実際に確認したところ、以下の3点の問題が見つかった。

1. **`lastFrame:0` かつ単位行列** — `animSpeed=0.0f`（静止中）でも折り返し検出が即発動し、frame=0（バインドポーズ）の行列が記録されていた。アニメーションが動いていないため比較に意味がない
2. **文字化け** — ログ文字列に日本語（「完了」）を使っていたため Shift-JIS→UTF-8 変換で文字化けしていた
3. **`_animSpeed` が `private` でアクセス不可** — `FbxParts` から `parent_->_animSpeed` を参照できず、静止判定ができなかった。また `Model::SetAnimFrame` から speed が `Fbx` に伝わっていなかった

### 修正内容

- `Fbx::SetAnimFrame` に `speed` 引数を追加し、`_animSpeed` も同時に更新するよう変更
- `Model::ModelData::SetAnimFrame` から `speed` を `Fbx::SetAnimFrame` に渡すよう変更
- `FbxParts::DrawSkinAnime` で `isPlaying`（`_animSpeed != 0.0f`）フラグを導入し、静止中は折り返し検出・`s_prevFrame` 更新をスキップ
- ログ文字列を英語化（文字化け解消）

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/Fbx.h` | `SetAnimFrame` に `float speed` 引数を追加。`_animSpeed` も更新するよう変更 |
| `Engine/Model.h` | `ModelData::SetAnimFrame` から `pFbx->SetAnimFrame(start, end, speed)` に変更 |
| `Engine/FbxParts.cpp` | `isPlaying` フラグ導入・静止中スキップ・ログ文字列英語化 |

### 変更詳細

#### `Engine/Fbx.h`

```cpp
// 変更前
void SetAnimFrame(int start, int end) { _startFrame = start; _endFrame = end; }

// 変更後
void SetAnimFrame(int start, int end, float speed = -1.0f)
{
    _startFrame = start;
    _endFrame   = end;
    if (speed >= 0.0f) _animSpeed = speed;
}
```

#### `Engine/Model.h`

```cpp
// 変更前
pFbx->SetAnimFrame(start, end);

// 変更後
pFbx->SetAnimFrame(start, end, speed);
```

#### `Engine/FbxParts.cpp` — `DrawSkinAnime` 冒頭

```cpp
// 追加
bool isPlaying = (parent_->_animSpeed != 0.0f);

// 変更前：常に折り返し検出・s_prevFrame 更新
if (s_prevFrame >= 0 && totalFrame < s_prevFrame) { s_loopCount++; justLooped = true; }
s_prevFrame = totalFrame;

// 変更後：再生中のみ検出・更新
if (isPlaying && s_prevFrame >= 0 && totalFrame < s_prevFrame) { s_loopCount++; justLooped = true; }
if (isPlaying) s_prevFrame = totalFrame;

// ログ文字列も英語化
// 変更前: "=== loop:0完了(WITH bindShape) ..."
// 変更後: "=== loop:0 done (WITH bindShape) ..."
```

---

## 2025-07-?? — bindShapeログ staticローカル変数 → メンバ変数化

### 背景

ログを確認しても `lastFrame:0`・単位行列のままだった。
コードとログファイルのタイムスタンプを比較した結果、修正済みexeで書かれたログであることが判明。

**根本原因：`static` ローカル変数の共有**

`DrawSkinAnime` 内の `static` 変数（`s_loopCount`、`s_prevFrame` 等）は
関数につき1つしか存在しない。
`Player.cpp` の `skg.fbx`（`animSpeed=1.0`）と
`ViewerScene.cpp` の `SquarePrism2.fbx`（`animSpeed=0.0`）が
同じ `DrawSkinAnime` を呼ぶため、変数が混線していた。

`skg.fbx` 側の折り返しで `s_loopCount` が進み、
`SquarePrism2.fbx` 側が `justLooped` になった瞬間（frame=0）に
ログが取られていた。

### 修正内容

`static` ローカル変数 5 つをすべて `FbxParts` のメンバ変数に変更し、
インスタンスごとに独立したカウンタを持つようにした。

| 旧 static 変数 | 新メンバ変数 |
|---|---|
| `s_header_logged` | `log_header_written_` |
| `s_logged_with` | `log_with_written_` |
| `s_logged_without` | `log_without_written_` |
| `s_loopCount` | `log_loopCount_` |
| `s_prevFrame` | `log_prevFrame_` |

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/FbxParts.h` | ログ制御用メンバ変数 5 つを追加 |
| `Engine/FbxParts.cpp` | コンストラクタ 2 つで初期化。`DrawSkinAnime` 内の `static` 変数参照をメンバ変数参照に置き換え |

---

## 2025-07-?? — ループ検出を Model::Draw 側に移管（NotifyLooped 方式）

### 背景

メンバ変数化しても「`Model::Draw` 内のループ折り返し検出と、`DrawSkinAnime` 内の独自折り返し検出が二重に存在する」構造的な問題は残っていた。
`Model::Draw` は既に `nowFrame > endFrame` を検出して `startFrame` に戻しているため、
そこで通知するのが最もシンプルかつ確実という判断。

### 修正内容

- `Fbx::NotifyLooped()` を追加：全 `FbxParts` に `OnLooped()` を送る
- `FbxParts::OnLooped()` を追加：`log_loopCount_` をインクリメントするだけ
- `Model::Draw` の折り返し時（`animSpeed != 0` のときのみ）に `pFbx->NotifyLooped()` を呼ぶ
- `DrawSkinAnime` 内の折り返し検出コード（`isPlaying`・`log_prevFrame_`・`justLooped`）を全削除
- 不要になった `log_prevFrame_` メンバをヘッダー・コンストラクタから削除

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/Fbx.h` | `NotifyLooped()` を public に追加 |
| `Engine/Fbx.cpp` | `NotifyLooped()` 実装：全パーツに `OnLooped()` を転送 |
| `Engine/FbxParts.h` | `OnLooped()` を public に追加。`log_prevFrame_` を削除 |
| `Engine/FbxParts.cpp` | コンストラクタから `log_prevFrame_` 初期化を削除。`DrawSkinAnime` の折り返し検出コードを削除 |
| `Engine/Model.cpp` | `Draw` の折り返し時に `pFbx->NotifyLooped()` を呼ぶよう変更 |

---

## 2025-07-?? — ログ取得タイミングを中間フレームに変更

### 背景

ログを確認したところ、両ファイルとも `bindShapeMatrix` が単位行列（T=0, R=0, S=1）だったため
WITH / WITHOUT の差が出なかった。
加えてログ取得が `frame:0`（ループ直後）だったため、
アニメーション中の意味のある姿勢ではなくバインドポーズの行列しか見えていなかった。

手持ちの FBX に限りがあり `bindShapeMatrix` が単位行列以外のファイルを用意できないため、
ログ取得タイミングを `endFrame/2`（中間フレーム）に変更して
アニメーション中の実際の姿勢で WITH / WITHOUT を比較できるようにした。

### 修正内容

- ログ取得条件を `OnLooped` 直後（`loopCount==1/2`）から
  **`totalFrame == midFrame`（中間フレーム到達時）** に変更
- loop:0 の中間フレーム → WITH bindShape のログ
- loop:1 の中間フレーム → WITHOUT bindShape のログ
- フラグ更新条件を `loopCount == 0/1` に修正

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/FbxParts.cpp` | `DrawSkinAnime` のログ開始条件を `loopCount==1/2` から `totalFrame==midFrame` に変更。フラグ更新を `loopCount==0/1` に修正 |

---

## 2025-07-?? — ログ取得完了をタイトルバーに表示

### 背景

ログがいつ取れたか画面上で分からないため、実行中に確認できない問題があった。

### 修正内容

`DrawSkinAnime` でログ書き込み完了時に `SetWindowTextA(GetActiveWindow(), ...)` でタイトルバーを変更するようにした。

| タイミング | タイトルバー表示 |
|---|---|
| loop:0 中間フレーム（WITH）書き込み完了 | `LOG:1/2 OK - WITH bindShape logged` |
| loop:1 中間フレーム（WITHOUT）書き込み完了 | `LOG:2/2 OK - WITH+WITHOUT both logged` |

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/FbxParts.cpp` | ログ書き込み完了後に `SetWindowTextA` でタイトルバーを更新 |

---

## 2025-07-?? — アニメーション速度が遅い問題を修正

### 背景

60fps のアニメーションがくっそ遅いという報告。

`ViewerScene::ANIM_SPEED = 0.5f` になっていたため、
`Model::Draw` で `nowFrame += animSpeed` するたびに 0.5 しか進まず、
実際の再生速度が半分（30fps相当）になっていた。

### 修正内容

`ANIM_SPEED` を `0.5f` → `1.0f` に変更。

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `ViewerScene.cpp` | `ANIM_SPEED` を `0.5f` → `1.0f` に変更 |

---

## 2025-07-?? — FPS表示がログメッセージを上書きする問題を修正

### 背景

`FbxParts::DrawSkinAnime` でログ書き込み完了時にタイトルバーに `LOG:1/2 OK` 等を表示したが、
`Main.cpp` のFPS表示ロジックが毎秒 `SetWindowText` で上書きするため、
ログメッセージが即座に消えて確認できなかった。

### 修正内容

FPS表示の `SetWindowText` 呼び出し前に `GetWindowTextA` で現在のタイトルを取得し、
`"LOG:"` が含まれている場合は上書きしないようにした。

### 変更ファイル

| ファイル | 変更概要 |
|---|---|
| `Engine/Main.cpp` | FPS表示前に現在タイトルを確認し、`LOG:` を含む場合は上書きをスキップ |
