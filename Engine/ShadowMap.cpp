#include "ShadowMap.h"
#include "Direct3D.h"
#include "Global.h"

namespace ShadowMap
{
	//-----------------------------------------------------------
	// このファイルの中だけで使う変数（外からは見えない）
	//-----------------------------------------------------------

	// シャドウマップ本体のテクスチャ（深度を記録する）
	ID3D11Texture2D*          pDepthTexture_       = nullptr;

	// 深度バッファとして使うためのビュー
	ID3D11DepthStencilView*   pDepthStencilView_   = nullptr;

	// シェーダーで読み込むためのビュー
	ID3D11ShaderResourceView* pShaderResourceView_ = nullptr;

	// シャドウパス用のコンスタントバッファ（光源 WVP 行列を送る）
	ID3D11Buffer*             pShadowConstantBuffer_ = nullptr;

	// 影のON/OFFフラグ
	bool isEnabled_ = true;

	// 光源のビュー行列（光源の位置・向きで決まる）
	XMMATRIX matLightView_;

	// 光源のプロジェクション行列（どの範囲を映すか）
	XMMATRIX matLightProj_;

	// 通常描画パスに戻すために保存しておく変数
	ID3D11RenderTargetView*  pSavedRTV_ = nullptr;
	ID3D11DepthStencilView*  pSavedDSV_ = nullptr;


	//-----------------------------------------------------------
	// 初期化
	//-----------------------------------------------------------
	void Initialize()
	{
		//------ シャドウマップ用テクスチャの作成 ------
		// DXGI_FORMAT_R32_TYPELESS にすることで
		// 深度バッファ（DSV）としても、シェーダーリソース（SRV）としても使える
		D3D11_TEXTURE2D_DESC texDesc = {};
		texDesc.Width              = SHADOW_MAP_SIZE;
		texDesc.Height             = SHADOW_MAP_SIZE;
		texDesc.MipLevels          = 1;
		texDesc.ArraySize          = 1;
		texDesc.Format             = DXGI_FORMAT_R32_TYPELESS;	// DSV と SRV を両立させるための特殊フォーマット
		texDesc.SampleDesc.Count   = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Usage              = D3D11_USAGE_DEFAULT;
		texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;	// 両方の用途で使う
		Direct3D::pDevice_->CreateTexture2D(&texDesc, nullptr, &pDepthTexture_);

		//------ 深度バッファビュー（DSV）の作成 ------
		// テクスチャを「深度バッファとして書き込む」ためのビュー
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;	// 32bit浮動小数点で深度を記録
		dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Texture2D.MipSlice = 0;
		Direct3D::pDevice_->CreateDepthStencilView(pDepthTexture_, &dsvDesc, &pDepthStencilView_);

		//------ シェーダーリソースビュー（SRV）の作成 ------
		// テクスチャを「シェーダーで読み込む」ためのビュー
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;	// 読み込みは普通のfloat
		srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels       = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;
		Direct3D::pDevice_->CreateShaderResourceView(pDepthTexture_, &srvDesc, &pShaderResourceView_);

		//------ シャドウパス用コンスタントバッファの作成 ------
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.ByteWidth           = sizeof(SHADOW_CB);
		cbDesc.Usage               = D3D11_USAGE_DYNAMIC;
		cbDesc.BindFlags           = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		Direct3D::pDevice_->CreateBuffer(&cbDesc, nullptr, &pShadowConstantBuffer_);

		//------ シャドウマップ用のサンプラーを作成して s1 スロットにセット ------
		// シャドウマップは境界を超えたら「1.0（影なし）」を返すようにする
		D3D11_SAMPLER_DESC sampDesc = {};
		sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.BorderColor[0] = 1.0f;	// 境界外は深度1.0（最も遠い）＝影なし
		sampDesc.BorderColor[1] = 1.0f;
		sampDesc.BorderColor[2] = 1.0f;
		sampDesc.BorderColor[3] = 1.0f;
		ID3D11SamplerState* pShadowSampler = nullptr;
		Direct3D::pDevice_->CreateSamplerState(&sampDesc, &pShadowSampler);
		Direct3D::pContext_->PSSetSamplers(1, 1, &pShadowSampler);
		SAFE_RELEASE(pShadowSampler);	// PSSetSamplers が内部で参照カウントを増やすので解放してOK

		// 光源の方向を初期設定
		SetLightDir(XMFLOAT3(-1.0f, -1.0f, 1.0f));
	}


	//-----------------------------------------------------------
	// 後片付け
	//-----------------------------------------------------------
	void Release()
	{
		SAFE_RELEASE(pDepthTexture_);
		SAFE_RELEASE(pDepthStencilView_);
		SAFE_RELEASE(pShaderResourceView_);
		SAFE_RELEASE(pShadowConstantBuffer_);
	}


	//-----------------------------------------------------------
	// 光源の方向をセットして、ビュー行列とプロジェクション行列を計算する
	//-----------------------------------------------------------
	void SetLightDir(XMFLOAT3 lightDir)
	{
		// 光の向きを正規化する
		XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&lightDir));

		// 光源の位置（シーン中心から光の逆方向に50ユニット離れた場所）
		XMVECTOR lightPos = XMVectorScale(dir, -50.0f);

		// 注視点はシーンの中心（0, 0, 0）
		XMVECTOR target = XMVectorZero();

		// 上方向（通常はY上）
		XMVECTOR up = XMVectorSet(0, 1, 0, 0);

		// 光源カメラのビュー行列（「光源から見た」座標系に変換する行列）
		matLightView_ = XMMatrixLookAtLH(lightPos, target, up);

		// 光源カメラのプロジェクション行列
		// シーンの広さ（約10×10ユニット）に合わせた範囲にすることで
		// シャドウマップの解像度を無駄なく使える
		matLightProj_ = XMMatrixOrthographicLH(20.0f, 20.0f, 0.1f, 200.0f);
	}


	//-----------------------------------------------------------
	// 影のON/OFF
	//-----------------------------------------------------------
	void SetEnable(bool enable)
	{
		isEnabled_ = enable;
	}

	bool IsEnabled()
	{
		return isEnabled_;
	}


	//-----------------------------------------------------------
	// 光源のビュー×プロジェクション行列を取得
	//-----------------------------------------------------------
	XMMATRIX GetLightViewProjection()
	{
		return matLightView_ * matLightProj_;
	}


	//-----------------------------------------------------------
	// 影描画パス開始
	// レンダーターゲットをシャドウマップ用の深度バッファに切り替える
	//-----------------------------------------------------------
	void BeginShadowPass()
	{
		// 現在のレンダーターゲットと深度バッファを覚えておく（後で戻すため）
		Direct3D::pContext_->OMGetRenderTargets(1, &pSavedRTV_, &pSavedDSV_);

		// シャドウマップの深度バッファをクリア
		Direct3D::pContext_->ClearDepthStencilView(pDepthStencilView_, D3D11_CLEAR_DEPTH, 1.0f, 0);

		// レンダーターゲットを「なし」、深度バッファをシャドウマップに切り替える
		// 色は描画しなくて良い（深度だけ記録できればOK）
		ID3D11RenderTargetView* pNullRTV = nullptr;
		Direct3D::pContext_->OMSetRenderTargets(1, &pNullRTV, pDepthStencilView_);

		// シャドウマップのサイズにビューポートを合わせる
		D3D11_VIEWPORT vp = {};
		vp.Width    = (float)SHADOW_MAP_SIZE;
		vp.Height   = (float)SHADOW_MAP_SIZE;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		Direct3D::pContext_->RSSetViewports(1, &vp);

		// ※ SetDepthBafferWriteEnable は呼ばない
		// 　 あの関数は内部で OMSetRenderTargets を呼ぶので
		// 　 直前でセットしたシャドウDSVが画面DSVで上書きされてしまう

		// シャドウマップ用シェーダーに切り替える
		Direct3D::SetShader(Direct3D::SHADER_SHADOW);
	}


	//-----------------------------------------------------------
	// 影描画パス終了
	// 通常のレンダーターゲットに戻す
	//-----------------------------------------------------------
	void EndShadowPass()
	{
		// 保存しておいたレンダーターゲットと深度バッファに戻す
		Direct3D::pContext_->OMSetRenderTargets(1, &pSavedRTV_, pSavedDSV_);
		SAFE_RELEASE(pSavedRTV_);
		SAFE_RELEASE(pSavedDSV_);

		// ビューポートを画面サイズに戻す
		D3D11_VIEWPORT vp = {};
		vp.Width    = (float)Direct3D::screenWidth_;
		vp.Height   = (float)Direct3D::screenHeight_;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		Direct3D::pContext_->RSSetViewports(1, &vp);

		// 通常の3Dシェーダーに戻す
		Direct3D::SetShader(Direct3D::SHADER_3D);
	}


	//-----------------------------------------------------------
	// シャドウマップをピクセルシェーダーのt1スロットにセット
	//-----------------------------------------------------------
	void BindToShader()
	{
		Direct3D::pContext_->PSSetShaderResources(1, 1, &pShaderResourceView_);
	}


	//-----------------------------------------------------------
	// シャドウマップのシェーダーへの割り当て解除
	// 次フレームのシャドウパスでシャドウマップに書き込むために外す
	//-----------------------------------------------------------
	void UnbindFromShader()
	{
		ID3D11ShaderResourceView* pNull = nullptr;
		Direct3D::pContext_->PSSetShaderResources(1, 1, &pNull);
	}


	//-----------------------------------------------------------
	// シャドウパス中のコンスタントバッファを更新してシェーダーに渡す
	// 引数：worldMatrix　そのモデルのワールド行列
	//-----------------------------------------------------------
	void UpdateAndBindConstantBuffer(XMMATRIX worldMatrix)
	{
		SHADOW_CB cb;
		// 光源から見たワールド×ビュー×プロジェクション行列（転置してシェーダーへ）
		cb.matWVP = XMMatrixTranspose(worldMatrix * matLightView_ * matLightProj_);

		// コンスタントバッファに書き込む
		D3D11_MAPPED_SUBRESOURCE msr = {};
		Direct3D::pContext_->Map(pShadowConstantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
		memcpy_s(msr.pData, msr.RowPitch, &cb, sizeof(cb));
		Direct3D::pContext_->Unmap(pShadowConstantBuffer_, 0);

		// 頂点シェーダーのスロット0番にセット
		Direct3D::pContext_->VSSetConstantBuffers(0, 1, &pShadowConstantBuffer_);
	}
}
