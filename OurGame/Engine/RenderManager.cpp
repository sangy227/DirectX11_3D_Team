#include "pch.h"
#include "RenderManager.h"
#include "Engine.h"
#include "SceneManager.h"
#include "Light.h"
#include "Camera.h"
#include "Resources.h"
#include "ImageFilter.h"
#include "BlurFilter.h"
#include "CombineFilter.h"
#include "MeshRenderer.h"
#include "Transform.h"
#include "Material.h"
#include "StructuredBuffer.h"


namespace hm
{
	RenderManager::RenderManager()
		: mWidth(0)
		, mHeight(0)
		, mbEnablePostProcessing(false)
		, mbEnableHDR(false)
		, mbEnableRim(false)
		, mpDownScaleBuffer(nullptr)
		, mpAvgLumBuffer(nullptr)
		, mpPrevAdaptionBuffer(nullptr)
	{
		mDOFFarStart = 0.f;
		mDOFFarRange = 0.f;
		mBloomThreshold = 0.f;
		mBloomScale = 0.f;

		mWidth = 0;
		mHeight = 0;
		mDomain = 0;
		mDownScaleGroups = 0;
		mAdatation = 0.f;
		mMiddleGrey = 0.f;
		mWhite = 0.f;
	}

	RenderManager::~RenderManager()
	{
		for (auto& pair : mBuffers)
		{
			InstancingBuffer* buffer = pair.second;
			SAFE_DELETE(buffer);
		}
	}

	void RenderManager::Initialize()
	{
		mWidth = gpEngine->GetWindowInfo().width;
		mHeight = gpEngine->GetWindowInfo().height;

		PostProcessInit();
	}

	void RenderManager::Render(Scene* _pScene)
	{
		ClearInstancingBuffer();

		PushLightData(_pScene);
		SortGameObject(_pScene);

		RenderDeferred(_pScene);
		RenderLight(_pScene);

		if (true == mbEnableRim)
			RenderRimLighting();

		RenderLightBlend();
		RenderFinal();

		if (true == mbEnablePostProcessing)
			PostProcessing();

		RenderForward(_pScene);

		
	}

	void RenderManager::ClearInstancingBuffer()
	{
		for (auto& pair : mBuffers)
		{
			InstancingBuffer* buffer = pair.second;
			buffer->ClearData();
		}
	}

	void RenderManager::Clear()
	{
		mBuffers.clear();
	}

	void RenderManager::ClearRenderTargets()
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::SwapChain)->ClearRenderTargetView();
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::G_Buffer)->ClearRenderTargetView();
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::Light)->ClearRenderTargetView();
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::RimLighting)->ClearRenderTargetView();
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::LightBlend)->ClearRenderTargetView();
	}

	void RenderManager::SortGameObject(Scene* _pScene)
	{
		AssertEx(_pScene->mpMainCamera, L"RenderManager::SortGameObject() - MainCamera is empty");
		_pScene->mpMainCamera->GetCamera()->SortGameObject();

		for (GameObject* pCameraObject : _pScene->mCameraObjects)
		{
			if (pCameraObject == _pScene->mpMainCamera)
				continue;

			pCameraObject->GetCamera()->SortGameObject();
		}
	}

	void RenderManager::RenderForward(Scene* _pScene)
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::SwapChain)->OMSetRenderTarget(1);
		RenderInstancing(_pScene->mpMainCamera->GetCamera(), _pScene->mpMainCamera->GetCamera()->GetForwardObjects());
		_pScene->mpMainCamera->GetCamera()->RenderParticle();

		for (GameObject* pCameraObject : _pScene->mCameraObjects)
		{
			if (pCameraObject == _pScene->mpMainCamera)
				continue;

			RenderInstancing(pCameraObject->GetCamera(), pCameraObject->GetCamera()->GetForwardObjects());
			pCameraObject->GetCamera()->RenderParticle();
		}
	}

	void RenderManager::RenderDeferred(Scene* _pScene)
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::G_Buffer)->OMSetRenderTarget();
		RenderInstancing(_pScene->mpMainCamera->GetCamera(), _pScene->mpMainCamera->GetCamera()->GetDeferredObjects());

		for (GameObject* pCameraObject : _pScene->mCameraObjects)
		{
			if (pCameraObject == _pScene->mpMainCamera)
				continue;

			RenderInstancing(pCameraObject->GetCamera(), pCameraObject->GetCamera()->GetDeferredObjects());
		}
	}

	void RenderManager::RenderLight(Scene* _pScene)
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::Light)->OMSetRenderTarget();
		for (GameObject* pLightObject : _pScene->mLightObjects)
		{
			pLightObject->GetLight()->Render(_pScene->mpMainCamera->GetCamera());
		}
	}

	void RenderManager::RenderRimLighting()
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::RimLighting)->OMSetRenderTarget();

		GET_SINGLE(Resources)->Get<Material>(L"RimLighting")->PushGraphicData();
		GET_SINGLE(Resources)->Get<Material>(L"RimLighting")->SetFloat(0, 1.f);
		GET_SINGLE(Resources)->Get<Material>(L"RimLighting")->SetFloat(1, 6.f);

		GET_SINGLE(Resources)->LoadRectMesh()->Render();
	}

	void RenderManager::RenderLightBlend()
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::LightBlend)->OMSetRenderTarget();

		GET_SINGLE(Resources)->Get<Material>(L"LightBlend")->PushGraphicData();
		GET_SINGLE(Resources)->LoadRectMesh()->Render();
	}

	void RenderManager::RenderFinal()
	{
		gpEngine->GetMultiRenderTarget(MultiRenderTargetType::SwapChain)->OMSetRenderTarget(1);

		GET_SINGLE(Resources)->Get<Material>(L"Final")->PushGraphicData();
		GET_SINGLE(Resources)->LoadRectMesh()->Render();
	}

	void RenderManager::PostProcessing()
	{
		if (true == mbEnableHDR)
			ComputeHDR();

		ComputeSSAO();
		Bloom();
	}

	void RenderManager::SetPostProcessing(bool _bFlag)
	{
		mbEnablePostProcessing = _bFlag;
	}

	void RenderManager::SetHDR(bool _bFlag)
	{
		mbEnableHDR = _bFlag;
	}

	void RenderManager::SetRimLighting(bool _bFlag)
	{
		mbEnableRim = _bFlag;
	}

	void RenderManager::PushLightData(Scene* _pScene)
	{
		LightParams lightParams = {};

		for (GameObject* pLightObject : _pScene->mLightObjects)
		{
			const LightInfo& lightInfo = pLightObject->GetLight()->GetLightInfo();

			pLightObject->GetLight()->SetLightIndex(lightParams.lightCount);

			lightParams.lights[lightParams.lightCount] = lightInfo;
			lightParams.lightCount++;
		}

		CONST_BUFFER(ConstantBufferType::Light)->PushData(&lightParams, sizeof(lightParams));
		CONST_BUFFER(ConstantBufferType::Light)->Mapping();
	}

	void RenderManager::DownScale()
	{
		mpDownScaleBuffer->PushComputeUAVData(RegisterUAV::u0);
		mpDownScaleSceneTexture->PushUAV(RegisterUAV::u3);

		GET_SINGLE(Resources)->Get<Texture>(L"LightBlendTarget")->PushSRV(RegisterSRV::t5);

		mpDownScaleFirstPassMaterial->SetInt(0, mWidth / 4);
		mpDownScaleFirstPassMaterial->SetInt(1, mHeight / 4);
		mpDownScaleFirstPassMaterial->SetInt(2, mDomain);
		mpDownScaleFirstPassMaterial->SetInt(3, mDownScaleGroups);
		mpDownScaleFirstPassMaterial->SetFloat(0, mAdatation); // Adaptation
		mpDownScaleFirstPassMaterial->SetFloat(1, mBloomThreshold); // Bloom Threshold

		mpDownScaleFirstPassMaterial->Dispatch(mDownScaleGroups, 1, 1);

		ID3D11UnorderedAccessView* pResetUAV = nullptr;
		CONTEXT->CSSetUnorderedAccessViews(0, 1, &pResetUAV, nullptr);
		CONTEXT->CSSetUnorderedAccessViews(3, 1, &pResetUAV, nullptr);

		mpAvgLumBuffer->PushComputeUAVData(RegisterUAV::u0);
		mpDownScaleBuffer->PushGraphicsData(RegisterSRV::t6);
		mpPrevAdaptionBuffer->PushGraphicsData(RegisterSRV::t7);

		GET_SINGLE(Resources)->Get<Texture>(L"LightBlendTarget")->PushSRV(RegisterSRV::t0);
		mpDownScaleSecondPassMaterial->SetInt(0, mWidth / 4);
		mpDownScaleSecondPassMaterial->SetInt(1, mHeight / 4);
		mpDownScaleSecondPassMaterial->SetInt(2, mDomain);
		mpDownScaleSecondPassMaterial->SetInt(3, mDownScaleGroups);
		mpDownScaleSecondPassMaterial->SetFloat(0, mAdatation); // Adaptation
		mpDownScaleSecondPassMaterial->SetFloat(1, mBloomThreshold); // Bloom Threshold

		mpDownScaleSecondPassMaterial->Dispatch(1, 1, 1);

		pResetUAV = nullptr;
		CONTEXT->CSSetUnorderedAccessViews(0, 1, &pResetUAV, nullptr);

		ID3D11ShaderResourceView* pNullSRV = nullptr;
		CONTEXT->CSSetShaderResources(6, 1, &pNullSRV);
		CONTEXT->CSSetShaderResources(7, 1, &pNullSRV);
	}

	void RenderManager::Blur()
	{
		shared_ptr<Texture> pBackTexture = nullptr;
		shared_ptr<Texture> pSwapChainTex = GET_SINGLE(Resources)->Get<Texture>(L"SwapChainTarget_0");

		mpCopyFilter->SetSRV(0, pSwapChainTex);
		mpCopyFilter->Render();

		mpSamplingFilter->SetSRV(0, pSwapChainTex);
		mpSamplingFilter->Render();

		mpBlurYFilter->SetSRV(0, mpSamplingFilter->GetTexture());
		mpBlurYFilter->Render();

		mpBlurXFilter->SetSRV(0, mpBlurYFilter->GetTexture());
		mpBlurXFilter->Render();
	}

	void RenderManager::Bloom()
	{
		Blur();

		mpCombineFilter->SetSRV(0, mpCopyFilter->GetTexture());
		mpCombineFilter->SetSRV(1, mpBlurXFilter->GetTexture());
		mpCombineFilter->Render();
	}

	void RenderManager::ComputeBloom()
	{
		mpDownScaleSceneTexture->PushSRV(RegisterSRV::t5);
		mpAvgLumBuffer->PushGraphicsData(RegisterSRV::t6);
		mpTempFirstTexture->PushUAV(RegisterUAV::u0);

		Vec2 resolution = RESOLUTION;
		float totalPixels = static_cast<float>((resolution.x * resolution.y) / 16);

		mpBritePassMaterial->Dispatch(static_cast<UINT32>(totalPixels / 1024), 1, 1);

		// ����
		ID3D11ShaderResourceView* pNullSRV = nullptr;
		ID3D11UnorderedAccessView* pNullUAV = nullptr;
		CONTEXT->CSSetShaderResources(5, 1, &pNullSRV);
		CONTEXT->CSSetShaderResources(6, 1, &pNullSRV);
		CONTEXT->CSSetUnorderedAccessViews(0, 1, &pNullUAV, NULL);
	}

	void RenderManager::ComputeBlur()
	{
		ID3D11ShaderResourceView* pNullSRV = nullptr;
		ID3D11UnorderedAccessView* pNullUAV = nullptr;

		mpTempSecondTexture->PushUAV(RegisterUAV::u0);
		mpTempFirstTexture->PushSRV(RegisterSRV::t0); // BritePass Texture

		mpVerticalBlurMaterial->Dispatch(static_cast<UINT32>(mDomain / 1024), 1, 1);

		CONTEXT->CSSetShaderResources(0, 1, &pNullSRV);
		CONTEXT->CSSetUnorderedAccessViews(0, 1, &pNullUAV, NULL);


		mpBloomTexture->PushUAV(RegisterUAV::u1);
		mpTempSecondTexture->PushSRV(RegisterSRV::t1);

		mpVerticalBlurMaterial->Dispatch(static_cast<UINT32>(mDomain / 1024), 1, 1);

		CONTEXT->CSSetShaderResources(1, 1, &pNullSRV);
		CONTEXT->CSSetUnorderedAccessViews(1, 1, &pNullUAV, NULL);
	}

	void RenderManager::ToneMapping()
	{
		// DOF �߰�
		shared_ptr<Texture> pDepthTarget = GET_SINGLE(Resources)->Get<Texture>(L"DepthTarget");
		shared_ptr<Texture> pLightBlendTarget = GET_SINGLE(Resources)->Get<Texture>(L"LightBlendTarget");

		GET_SINGLE(Resources)->Get<Texture>(L"LightBlendTarget")->PushSRV(RegisterSRV::t5);
		mpAvgLumBuffer->PushGraphicsData(RegisterSRV::t6);
		mpBloomTexture->PushSRV(RegisterSRV::t8);
		mpDownScaleSceneTexture->PushSRV(RegisterSRV::t9);
		GET_SINGLE(Resources)->Get<Texture>(L"DepthTarget")->PushSRV(RegisterSRV::t10);

		mpHDRMaterial->SetFloat(0, mMiddleGrey);
		mpHDRMaterial->SetFloat(1, mWhite * mMiddleGrey * (mWhite * mMiddleGrey));
		mpHDRMaterial->SetFloat(2, mBloomScale);
		mpHDRMaterial->SetVec2(0, Vec2(mDOFFarStart, 0.f));
		mpHDRMaterial->SetVec2(1, Vec2(mDOFFarStart, mDOFFarRange));

		mpHDRMaterial->PushGraphicDataExceptForTextures();
		CONTEXT->IASetInputLayout(NULL);
		UINT32 offSet = 0;
		CONTEXT->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		CONTEXT->IASetVertexBuffers(0, 0, nullptr, 0, &offSet);
		CONTEXT->IASetIndexBuffer(NULL, DXGI_FORMAT_UNKNOWN, 0);
		CONTEXT->Draw(4, 0);

		ID3D11ShaderResourceView* pNullSRV = nullptr;
		CONTEXT->PSSetShaderResources(6, 1, &pNullSRV);
		CONTEXT->PSSetShaderResources(8, 1, &pNullSRV);
		CONTEXT->PSSetShaderResources(9, 1, &pNullSRV);
	}

	void RenderManager::ComputeHDR()
	{
		DownScale();
		ComputeBloom();
		ComputeBlur();
		ToneMapping();
	}

	void RenderManager::ComputeSSAO()
	{

	}

	void RenderManager::AddParam(UINT64 _instanceID, InstancingParams& _params)
	{
		if (mBuffers.find(_instanceID) == mBuffers.end())
		{
			mBuffers[_instanceID] = new InstancingBuffer;
			mBuffers[_instanceID]->Initialize(sizeof(InstancingParams));
		}

		mBuffers[_instanceID]->AddData(_params);
	}
	void RenderManager::RenderInstancing(Camera* _pCamera, const std::vector<GameObject*> _gameObjects)
	{
		std::map<UINT64, std::vector<GameObject*>> tempMap;

		for (GameObject* pGameObject : _gameObjects)
		{
			//pGameObject->GetMeshRenderer()->Render(_pCamera);
			const UINT64 instanceID = pGameObject->GetMeshRenderer()->GetInstanceID();
			tempMap[instanceID].push_back(pGameObject);
		}

		for (auto& pair : tempMap)
		{
			const std::vector<GameObject*>& gameObjects = pair.second;

			if (gameObjects.size() == 1)
			{
				gameObjects[0]->GetMeshRenderer()->GetMaterial()->SetIntAllSubset(0, 0);
				gameObjects[0]->GetMeshRenderer()->Render(_pCamera);
			}

			else
			{
				const UINT64 instanceId = pair.first;

				for (GameObject* pGameObject : gameObjects)
				{
					InstancingParams params;
					params.matWorld = pGameObject->GetTransform()->GetWorldMatrix();
					params.matWV = params.matWorld * _pCamera->GetViewMatrix();
					params.matWVP = params.matWorld * _pCamera->GetViewMatrix() * _pCamera->GetProjectionMatrix();

					AddParam(instanceId, params);
				}

				InstancingBuffer* pBuffer = mBuffers[instanceId];
				gameObjects[0]->GetMeshRenderer()->GetMaterial()->SetIntAllSubset(0, 1);
				gameObjects[0]->GetMeshRenderer()->Render(_pCamera, pBuffer);
			}
		}
	}
	void RenderManager::PostProcessInit()
	{
		mDOFFarStart = 30.0f;				// 40 ~ 400
		mDOFFarRange = 1.0f / std::fmaxf(60.0f, 0.001f);			// 80 -> 60 ~150

		mMiddleGrey = 12.f;
		mWhite = 10.0f;

		mWidth = static_cast<UINT32>(RESOLUTION.x);
		mHeight = static_cast<UINT32>(RESOLUTION.y);
		mDomain = (UINT)((float)(mWidth * mHeight / 16));
		mDownScaleGroups = (UINT)((float)(mWidth * mHeight / 16) / 1024.0f);
		mAdatation = 5.0f;

		mBloomThreshold = 2.0f;
		mBloomScale = 0.2f;

		// LDR
		mpCopyFilter = make_shared<ImageFilter>(GET_SINGLE(Resources)->Get<Material>(L"Copy"), mWidth, mHeight);
		mpSamplingFilter = make_shared<ImageFilter>(GET_SINGLE(Resources)->Get<Material>(L"Sampling"), mWidth, mHeight);
		mpBlurYFilter = make_shared<BlurFilter>(GET_SINGLE(Resources)->Get<Material>(L"BlurY"), mWidth / 6, mHeight / 6);
		mpBlurXFilter = make_shared<BlurFilter>(GET_SINGLE(Resources)->Get<Material>(L"BlurX"), mWidth / 6, mHeight / 6);
		mpCombineFilter = make_shared<CombineFilter>(GET_SINGLE(Resources)->Get<Material>(L"Combine"), mWidth, mHeight);

		// HDR
		mpDownScaleFirstPassMaterial = GET_SINGLE(Resources)->Get<Material>(L"DownScaleFirst");
		mpDownScaleSecondPassMaterial = GET_SINGLE(Resources)->Get<Material>(L"DownScaleSecond");
		mpVerticalBlurMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeVerticalBlur");
		mpHorizonBlurMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeHorizonBlur");
		mpBritePassMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeBloom");
		mpHDRMaterial = GET_SINGLE(Resources)->Get<Material>(L"HDR");

		mpDownScaleBuffer = make_shared<StructuredBuffer>();
		mpDownScaleBuffer->Create(4, ((static_cast<int>(RESOLUTION.x) * static_cast<int>(RESOLUTION.y)) / (16 * 1024)));

		mpAvgLumBuffer = make_shared<StructuredBuffer>();
		mpAvgLumBuffer->Create(4, 1);

		mpPrevAdaptionBuffer = make_shared<StructuredBuffer>();
		mpPrevAdaptionBuffer->Create(4, 1);

		mpDownScaleSceneTexture =
			GET_SINGLE(Resources)->CreateTexture(
				L"DownScaleSceneTexture", DXGI_FORMAT_R32G32B32A32_TYPELESS,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				mWidth / 4, mHeight / 4);

		mpTempFirstTexture = 
			GET_SINGLE(Resources)->CreateTexture(
				L"PostProcessTempFirst", DXGI_FORMAT_R32G32B32A32_TYPELESS,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				mWidth / 4, mHeight / 4);

		mpTempSecondTexture =
			GET_SINGLE(Resources)->CreateTexture(
				L"PostProcessTempSecond", DXGI_FORMAT_R32G32B32A32_TYPELESS,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				mWidth / 4, mHeight / 4);

		mpBloomTexture =
			GET_SINGLE(Resources)->CreateTexture(
				L"PostProcessBloom", DXGI_FORMAT_R32G32B32A32_TYPELESS,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
				mWidth / 4, mHeight / 4);
	}
}

