#include "UI/PoiseAVHUD.h"

#include "Hooks/PoiseAV.h"


float PoiseAVHUD::GetMaxSpecial(RE::Actor* a_actor)
{
	if (a_actor) {
		return PoiseAV::GetSingleton()->GetPassiveMax(a_actor);
	}
	return 1.0f;
}

float PoiseAVHUD::GetCurrentSpecial(RE::Actor* a_actor)
{
	if (a_actor) {
		return PoiseAV::GetSingleton()->GetPassiveCurrent(a_actor);
	}
	return 1.0f;
}
