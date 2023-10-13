#include "pch.h"
#include "AIMoveState.h"
#include "Engine.h"

/* Resource */
#include "MeshData.h"
#include "Material.h"
#include "Mesh.h"

/* Manager */
#include "PrefabManager.h"
#include "EventManager.h"
#include "Factory.h"
#include "CollisionManager.h"
#include "Input.h"
#include "SceneManager.h"
#include "Resources.h"

/* GameObject */
#include "GameObject.h"
#include "Player.h"
#include "Ground.h"
#include "DecoObject.h"
#include "WallObject.h"
#include "Npc.h"
#include "Monster.h"
#include "Grandma.h"

/* Component */
#include "Collider.h"
#include "RigidBody.h"
#include "MeshRenderer.h"
#include "Transform.h"
#include "Camera.h"
#include "Light.h"
#include "ParticleSystem.h"
#include "Animator.h"

/* Script */
#include "PlayerMoveScript.h"
#include "PlacementScript.h"
#include "TestAnimationScript.h"
#include "SwordHeavyEffect.h"
#include "PlayerSlashScript.h"
#include "OwnerFollowScript.h"

/* Event */
#include "SceneChangeEvent.h"

AIMoveState::AIMoveState()
	: State(PlayerState::AIMoveState)
{
}

void AIMoveState::Initialize()
{
}

void AIMoveState::Update()
{
	Player* pPlayer = Player::GetPlayer();
	Animator* pAni = pPlayer->GetAnimator();
	Transform* pTr = pPlayer->GetTransform();
	Vec3 myPos = pTr->GetPosition();
	Vec3 objectPos = Vec3::Zero; //NPC�� ��ǥ�� ���� �����ؾߵ�
	float mSpeed = pPlayer->GetMoveSpeed();

	Vec3 dir = objectPos - myPos;
	dir.Normalize();
	dir.y = 0;

	//if (/*Ư������ �ߵ��Ǹ� IDLE���·� �Ѿ��*/)
	//{
	//	pPlayer->StateChange(PlayerState::IdleState);
	//}



	{ // �÷��̾ ������Ʈ�� �̵��ϴ� �κ�
		Vec3 Ve = dir * mSpeed;//�̵��ӵ� ����

		pPlayer->GetRigidBody()->SetVelocity(Ve); //�׹������ΰ�����
	}



	{ //�÷��̾ ������Ʈ �������� ȸ���ϴ� �κ�
		Vec3 Rot = pTr->GetRotation();
		Vec3 rot = Vec3(0, 0, -1);
		double angleRadian = atan2(dir.x, dir.z) - atan2(rot.x, rot.z);
		float angleDegree = static_cast<float>(angleRadian) * 180.f / XM_PI;

		if (angleDegree < 0.f)
			angleDegree += 360.f;

		pTr->SetRotation(Vec3(-90.f, 0.f, angleDegree));
	}



	//MOVE �ִϸ��̼� �ݺ� 
	if (pAni->GetFrameRatio() > 0.7)
		PlayAnimation();

}

void AIMoveState::Enter()
{
	Player* pPlayer = Player::GetPlayer();
	Animator* pAni = pPlayer->GetAnimator();

	GameObject* pObj = pPlayer->GetGreatSword();
	pObj->Disable();

	int MoveToAnim = 67; 
	pAni->Play(MoveToAnim, true);
}

void AIMoveState::Exit()
{
	Player* pPlayer = Player::GetPlayer();
	GameObject* pObj = pPlayer->GetGreatSword();
	pObj->Enable();
}

void AIMoveState::PlayAnimation()
{
}