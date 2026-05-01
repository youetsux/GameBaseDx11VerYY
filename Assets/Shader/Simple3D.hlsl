//───────────────────────────────────────
 // テクスチャ＆サンプラーデータのグローバル変数定義
//───────────────────────────────────────
Texture2D		g_texture: register(t0);	//テクスチャー
SamplerState	g_sampler : register(s0);	//サンプラー

// シャドウマップ（光源から見た深度テクスチャ）
// t1スロットに差し込む（t0はすでにモデルのテクスチャが使っている）
Texture2D		g_shadowMap    : register(t1);
SamplerState	g_shadowSampler: register(s1);

//───────────────────────────────────────
 // コンスタントバッファ
// DirectX 側から送信されてくる、ポリゴン頂点以外の諸情報の定義
//───────────────────────────────────────
cbuffer global
{
	float4x4	g_matWVP;			// ワールド・ビュー・プロジェクションの合成行列
	float4x4	g_matNormalTrans;	// 法線の変換行列（回転行列と拡大の逆行列）
	float4x4	g_matWorld;			// ワールド変換行列
	float4		g_vecLightDir;		// ライトの方向ベクトル
	float4		g_vecDiffuse;		// ディフューズカラー（マテリアルの色）
	float4		g_vecAmbient;		// アンビエントカラー（影の色）
	float4		g_vecSpeculer;		// スペキュラーカラー（ハイライトの色）
	float4		g_vecCameraPosition;// 視点（カメラの位置）
	float		g_shuniness;		// ハイライトの強さ（テカリ具合）
	bool		g_isTexture;		// テクスチャ貼ってあるかどうか

	// ↓ここからシャドウマップ用に追加したデータ
	float2		g_pad0;				// 16バイト境界に合わせるための詰め物（使わない）
	float4x4	g_matLightVP;		// 光源カメラのビュー×プロジェクション行列
	bool		g_isShadowReceiver;	// この物体が影を受けるかどうか
	float3		g_pad1;				// 詰め物

};

//───────────────────────────────────────
// 頂点シェーダー出力＆ピクセルシェーダー入力データ構造体
//───────────────────────────────────────
struct VS_OUT
{
	float4 pos       : SV_POSITION;	//位置
	float4 normal    : TEXCOORD2;	//法線
	float2 uv	     : TEXCOORD0;	//UV座標
	float4 eye	     : TEXCOORD1;	//視線
	float4 shadowPos : TEXCOORD3;	//光源カメラから見たこの頂点の位置（影計算に使う）
};

//───────────────────────────────────────
// 頂点シェーダ
//───────────────────────────────────────
VS_OUT VS(float4 pos : POSITION, float4 Normal : NORMAL, float2 Uv : TEXCOORD)
{
	//ピクセルシェーダーへ渡す情報
	VS_OUT outData;

	//ローカル座標に、ワールド・ビュー・プロジェクション行列をかけて
	//スクリーン座標に変換し、ピクセルシェーダーへ
	outData.pos = mul(pos, g_matWVP);		

	//法線の変形
	Normal.w = 0;					//4次元目は使わないので0
	Normal = mul(Normal, g_matNormalTrans);		//オブジェクトが変形すれば法線も変形
	outData.normal = Normal;		//これをピクセルシェーダーへ

	//視線ベクトル（ハイライトの計算に必要
	float4 worldPos = mul(pos, g_matWorld);					//ローカル座標にワールド行列をかけてワールド座標へ
	outData.eye = normalize(g_vecCameraPosition - worldPos);	//視点から頂点位置を引き算し視線を求めてピクセルシェーダーへ

	//UV「座標
	outData.uv = Uv;	//そのままピクセルシェーダーへ

	// 光源カメラから見た位置を計算してピクセルシェーダーへ渡す
	// これを使って「光が届いているか」をピクセルシェーダーで判定する
	outData.shadowPos = mul(mul(pos, g_matWorld), g_matLightVP);

	//まとめて出力
	return outData;
}

//───────────────────────────────────────
// ピクセルシェーダ
//───────────────────────────────────────
float4 PS(VS_OUT inData) : SV_Target
{
	//ライトの向き
	float4 lightDir = g_vecLightDir;	//グルーバル変数は変更できないので、いったんローカル変数へ
	lightDir = normalize(lightDir);	//向きだけが必要なので正規化

	//法線はピクセルシェーダーに持ってきた時点で補完され長さが変わっている
	//正規化しておかないと面の明るさがおかしくなる
	inData.normal = normalize(inData.normal);

	//拡散反射光（ディフューズ）
	//法線と光のベクトルの内積が、そこの明るさになる
	float4 shade = saturate(dot(inData.normal, -lightDir));
	shade.a = 1;	//暗いところが透明になるので、強制的にアルファは1

	float4 diffuse;
	//テクスチャ有無
	if (g_isTexture == true)
	{
		//テクスチャの色
		diffuse = g_texture.Sample(g_sampler, inData.uv);
	}
	else
	{
		//マテリアルの色
		diffuse = g_vecDiffuse;
        //diffuse = float4(0, 0, 0, 0);
    }

	//環境光（アンビエント）
	//これはMaya側で指定し、グローバル変数で受け取ったものをそのまま
	float4 ambient = g_vecAmbient;

	//鏡面反射光（スペキュラー）
	float4 speculer = float4(0, 0, 0, 0);	//とりあえずハイライトは無しにしておいて…
	if (g_vecSpeculer.a != 0)	//スペキュラーの情報があれば
	{
		float4 R = reflect(lightDir, inData.normal);			//正反射ベクトル
		speculer = pow(saturate(dot(R, inData.eye)), g_shuniness) * g_vecSpeculer;	//ハイライトを求める
	}

	// ===== シャドウマップを使った影の計算 =====
	float shadow = 1.0f;	// 1.0 = 明るい（影なし）、0.5 = 暗い（影あり）

	if (g_isShadowReceiver)
	{
		// 射影変換後の座標を -1〜1 から 0〜1 のテクスチャ座標に変換する
		float2 shadowUV;
		shadowUV.x =  inData.shadowPos.x / inData.shadowPos.w * 0.5f + 0.5f;
		shadowUV.y = -inData.shadowPos.y / inData.shadowPos.w * 0.5f + 0.5f;

		// テクスチャ範囲内のときだけ影判定する
		if (shadowUV.x >= 0.0f && shadowUV.x <= 1.0f &&
			shadowUV.y >= 0.0f && shadowUV.y <= 1.0f)
		{
			// シャドウマップから深度値を取得（光源からの距離）
			float shadowDepth = g_shadowMap.Sample(g_shadowSampler, shadowUV).r;

			// この頂点の光源からの深度値
			float currentDepth = inData.shadowPos.z / inData.shadowPos.w;

			// シャドウバイアス：浮動小数点誤差によるセルフシャドウを防ぐ小さなオフセット
			// 大きくしすぎると地面に近い物体の影が消えるので注意
			float bias = 0.001f;

			// 光源より遠い（奥にある）場合 → 影の中にいる
			if (currentDepth > shadowDepth + bias)
			{
				shadow = 0.5f;	// 影の中なので暗くする
			}
		}
	}

	//最終的な色（影の中なら shadow=0.5f 倍暗くなる）
	return (diffuse * shade + diffuse * ambient + speculer) * shadow;
}