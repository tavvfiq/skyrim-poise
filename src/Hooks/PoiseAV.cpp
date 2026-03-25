#include "Hooks/PoiseAV.h"

#include "ActorValues/AVManager.h"
#include "Storage/ActorCache.h"
#include "Storage/Settings.h"
#include "UI/PoiseAVHUD.h"

namespace
{
	constexpr const char* kMeter_PassiveCur = "PassiveCur";
	constexpr const char* kMeter_PassiveMax = "PassiveMax";
	constexpr const char* kMeter_StanceCur = "StanceCur";
	constexpr const char* kMeter_StanceMax = "StanceMax";

	constexpr const char* KW_ActorTypeBoss = "ActorTypeBoss";
	constexpr const char* KW_ActorTypeNPC = "ActorTypeNPC";
	constexpr const char* KW_CreatureFaction = "CreatureFaction";
}

bool PoiseAV::CanDamageActor(RE::Actor* a_actor)
{
	if (a_actor && a_actor->currentProcess && !a_actor->IsChild()) {
		switch (Settings::GetSingleton()->Modes.StaggerMode) {
		case 0:
			return true;
		case 1:
			return !a_actor->actorState2.staggered;
		}
	}
	return false;
}

bool PoiseAV::IsPassiveTarget(RE::Actor* a_actor) const
{
	if (!a_actor) {
		return false;
	}
	if (a_actor->IsPlayerRef()) {
		return true;
	}
	// Humanoid NPCs opted-in via keyword.
	return a_actor->HasKeywordString(KW_ActorTypeNPC);
}

bool PoiseAV::IsStanceTarget(RE::Actor* a_actor) const
{
	if (!a_actor) {
		return false;
	}
	// Bosses and creatures use Stance (posture-break).
	return a_actor->HasKeywordString(KW_ActorTypeBoss) || a_actor->HasKeywordString(KW_CreatureFaction);
}

float PoiseAV::GetStoredMeter(RE::Actor* a_actor, const char* a_meterKey, float a_default) const
{
	if (!a_actor || !a_meterKey) {
		return a_default;
	}
	auto* avManager = AVManager::GetSingleton();
	const std::string formID = std::to_string(a_actor->formID);

	// avStorage[formID][a_meterKey] = number
	if (avManager->avStorage[formID][a_meterKey] == nullptr) {
		return a_default;
	}
	return static_cast<float>(avManager->avStorage[formID][a_meterKey]);
}

void PoiseAV::SetStoredMeter(RE::Actor* a_actor, const char* a_meterKey, float a_value)
{
	if (!a_actor || !a_meterKey) {
		return;
	}
	auto* avManager = AVManager::GetSingleton();
	const std::string formID = std::to_string(a_actor->formID);
	avManager->avStorage[formID][a_meterKey] = a_value;
}

float PoiseAV::ComputePassiveMax(RE::Actor* a_actor) const
{
	// PassivePoise max is armor-driven (anti-flinch).
	// Reuse existing base/armor scaling for now.
	return GetBaseActorValue(a_actor);
}

float PoiseAV::ComputeStanceMax(RE::Actor* a_actor) const
{
	// Stance max is authored per race/actor where desired; fallback to a default.
	auto* settings = Settings::GetSingleton();
	constexpr float kDefaultStance = 80.0f;

	if (!a_actor) {
		return kDefaultStance;
	}

	// JSON: "Stance": { "Default": 80, "Races": { "DragonRace": 120 } }
	try {
		if (settings->JSONSettings["Stance"]["Default"] != nullptr) {
			const float def = static_cast<float>(settings->JSONSettings["Stance"]["Default"]);
			if (def > 0.0f) {
				// keep def
				;
			}
		}
	} catch (...) {
		// ignore parse issues
	}

	float stance = kDefaultStance;
	if (settings->JSONSettings["Stance"]["Default"] != nullptr) {
		stance = static_cast<float>(settings->JSONSettings["Stance"]["Default"]);
	}

	const std::string editorID = a_actor->GetRace() ? a_actor->GetRace()->GetFormEditorID() : "";
	if (!editorID.empty() && settings->JSONSettings["Stance"]["Races"][editorID] != nullptr) {
		stance = static_cast<float>(settings->JSONSettings["Stance"]["Races"][editorID]);
	}
	return std::clamp(stance, 1.0f, FLT_MAX);
}

float PoiseAV::GetBaseActorValue(RE::Actor* a_actor)
{
	auto        settings = Settings::GetSingleton();
	std::string editorID = a_actor->GetRace()->GetFormEditorID();

	float health;
	if (!editorID.empty() && (settings->JSONSettings["Races"][editorID] != nullptr))
		health = (float)settings->JSONSettings["Races"][editorID];
	else
		health = a_actor->GetBaseActorValue(RE::ActorValue::kMass);

	health *= settings->Health.BaseMult;
	health += ActorCache::GetSingleton()->GetOrCreateCachedWeight(a_actor) * Settings::GetSingleton()->Health.ArmorMult;

	//if (auto levelledModifier = (RE::ExtraLevCreaModifier*)a_actor->extraList.GetByType(RE::ExtraDataType::kLevCreaModifier)) {
	//	auto modifier = levelledModifier->modifier;
	//	if (modifier.any(RE::LEV_CREA_MODIFIER::kEasy)) {
	//		health *= 0.75;
	//	} else if (modifier.any(RE::LEV_CREA_MODIFIER::kMedium)) {
	//		health *= 1.00;
	//	} else if (modifier.any(RE::LEV_CREA_MODIFIER::kHard)) {
	//		health *= 1.25;
	//	} else if (modifier.any(RE::LEV_CREA_MODIFIER::kVeryHard)) {
	//		health *= 1.50;
	//	}
	//}

	return std::clamp(health, 0.0f, FLT_MAX);
}

float PoiseAV::GetActorValueMax([[maybe_unused]] RE::Actor* a_actor)
{
	return GetBaseActorValue(a_actor);
}

bool PoiseAV::ApplyPoiseDamage(RE::Actor* a_target, RE::Actor* a_aggressor, float a_poiseDamage)
{
	if (!a_target || a_poiseDamage <= 0.0f) {
		return false;
	}

	auto* settings = Settings::GetSingleton();
	auto* avManager = AVManager::GetSingleton();
	std::lock_guard<std::shared_mutex> lk(avManager->mtx);

	const double nowSec = NowSeconds();

	bool suppressFlinch = false;

	// PassivePoise (anti-flinch)
	if (IsPassiveTarget(a_target)) {
		float maxP = ComputePassiveMax(a_target);
		float curP = GetStoredMeter(a_target, kMeter_PassiveCur, maxP);
		float storedMax = GetStoredMeter(a_target, kMeter_PassiveMax, maxP);

		// Refresh max when armor changes (or on first init).
		if (storedMax <= 0.0f || std::abs(storedMax - maxP) > 0.01f) {
			storedMax = maxP;
			SetStoredMeter(a_target, kMeter_PassiveMax, storedMax);
			if (curP <= 0.0f) {
				curP = storedMax;
			} else {
				curP = std::min(curP, storedMax);
			}
		}

		curP -= a_poiseDamage;
		SetStoredMeter(a_target, kMeter_PassiveCur, curP);

		{
			std::unique_lock<std::shared_mutex> ul(_rtMtx);
			auto& st = _rt[a_target->formID];
			st.lastPassiveHitSec = nowSec;
		}

		// If still above 0, absorb the hit (no flinch).
		if (curP > 0.0f) {
			suppressFlinch = true;
		} else {
			// Broken: leave at/below 0; refill handled by Update after delay.
			suppressFlinch = false;
		}
	}

	// Stance (posture-break) for bosses/creatures.
	if (IsStanceTarget(a_target)) {
		float maxS = ComputeStanceMax(a_target);
		float curS = GetStoredMeter(a_target, kMeter_StanceCur, maxS);
		float storedMaxS = GetStoredMeter(a_target, kMeter_StanceMax, maxS);
		if (storedMaxS <= 0.0f || std::abs(storedMaxS - maxS) > 0.01f) {
			storedMaxS = maxS;
			SetStoredMeter(a_target, kMeter_StanceMax, storedMaxS);
			curS = std::min(std::max(curS, 0.0f), storedMaxS);
			if (curS <= 0.0f) {
				curS = storedMaxS;
			}
		}

		double immuneUntil = 0.0;
		{
			std::shared_lock<std::shared_mutex> sl(_rtMtx);
			if (_rt.contains(a_target->formID)) {
				immuneUntil = _rt.at(a_target->formID).stanceBreakImmuneUntilSec;
			}
		}

		if (nowSec >= immuneUntil) {
			curS -= a_poiseDamage;
			SetStoredMeter(a_target, kMeter_StanceCur, curS);

			{
				std::unique_lock<std::shared_mutex> ul(_rtMtx);
				auto& st = _rt[a_target->formID];
				st.lastStanceHitSec = nowSec;
			}

			if (curS <= 0.0f) {
				// Stance break: full-body stagger + cooldown + reset.
				a_target->AddToFaction(ForceFullBodyStagger, 0);
				TryStagger(a_target, 1.0f, a_aggressor);

				{
					std::unique_lock<std::shared_mutex> ul(_rtMtx);
					auto& st = _rt[a_target->formID];
					st.stanceBreakImmuneUntilSec = nowSec + settings->Stance.BreakCooldownSeconds;
				}

				SetStoredMeter(a_target, kMeter_StanceCur, storedMaxS);
			}
		}
	}

	// Keep original faction cleanup logic in Update.
	logger::debug(FMT_STRING("Target {} PoiseDamage {} PassiveCur {} / {} StanceCur {} / {} suppress={}"),
		a_target->GetName(),
		a_poiseDamage,
		GetStoredMeter(a_target, kMeter_PassiveCur, 0.0f),
		GetStoredMeter(a_target, kMeter_PassiveMax, 0.0f),
		GetStoredMeter(a_target, kMeter_StanceCur, 0.0f),
		GetStoredMeter(a_target, kMeter_StanceMax, 0.0f),
		suppressFlinch);

	return suppressFlinch;
}

void PoiseAV::Update(RE::Actor* a_actor, [[maybe_unused]] float a_delta)
{
	if (a_actor->currentProcess && a_actor->currentProcess->InHighProcess() && a_actor->Is3DLoaded()) {
		auto settings = Settings::GetSingleton();

		if (PoiseAVHUD::trueHUDInterface && settings->TrueHUD.SpecialBar) {
			if (!CanDamageActor(a_actor))
				PoiseAVHUD::trueHUDInterface->OverrideSpecialBarColor(a_actor->GetHandle(), TRUEHUD_API::BarColorType::BarColor, 0x808080);
			else
				PoiseAVHUD::trueHUDInterface->RevertSpecialBarColor(a_actor->GetHandle(), TRUEHUD_API::BarColorType::BarColor);
		}

		auto* avManager = AVManager::GetSingleton();
		std::lock_guard<std::shared_mutex> lk(avManager->mtx);

		const double nowSec = NowSeconds();

		// PassivePoise refill-after-delay.
		if (IsPassiveTarget(a_actor)) {
			const float maxP = ComputePassiveMax(a_actor);
			float curP = GetStoredMeter(a_actor, kMeter_PassiveCur, maxP);
			SetStoredMeter(a_actor, kMeter_PassiveMax, maxP);

			double lastHit = 0.0;
			{
				std::shared_lock<std::shared_mutex> sl(_rtMtx);
				if (_rt.contains(a_actor->formID)) {
					lastHit = _rt.at(a_actor->formID).lastPassiveHitSec;
				}
			}
			if (lastHit <= 0.0) {
				lastHit = nowSec;
				std::unique_lock<std::shared_mutex> ul(_rtMtx);
				_rt[a_actor->formID].lastPassiveHitSec = lastHit;
			}

			if ((nowSec - lastHit) >= settings->PassivePoise.RefillDelaySeconds) {
				curP = std::min(maxP, curP + settings->PassivePoise.RefillRatePerSecond * a_delta);
				SetStoredMeter(a_actor, kMeter_PassiveCur, curP);
			}
		}

		// Stance refill-after-delay.
		if (IsStanceTarget(a_actor)) {
			const float maxS = ComputeStanceMax(a_actor);
			float curS = GetStoredMeter(a_actor, kMeter_StanceCur, maxS);
			SetStoredMeter(a_actor, kMeter_StanceMax, maxS);

			double lastHit = 0.0;
			{
				std::shared_lock<std::shared_mutex> sl(_rtMtx);
				if (_rt.contains(a_actor->formID)) {
					lastHit = _rt.at(a_actor->formID).lastStanceHitSec;
				}
			}
			if (lastHit <= 0.0) {
				lastHit = nowSec;
				std::unique_lock<std::shared_mutex> ul(_rtMtx);
				_rt[a_actor->formID].lastStanceHitSec = lastHit;
			}

			if ((nowSec - lastHit) >= settings->Stance.RefillDelaySeconds) {
				curS = std::min(maxS, curS + settings->Stance.RefillRatePerSecond * a_delta);
				SetStoredMeter(a_actor, kMeter_StanceCur, curS);
			}
		}

		// Faction cleanup after stagger ends.
		if (!a_actor->actorState2.staggered) {
			RemoveFromFaction(a_actor, ForceFullBodyStagger);
		}
	}
}

void PoiseAV::GarbageCollection()
{
	auto                               avManager = AVManager::GetSingleton();
	std::lock_guard<std::shared_mutex> lk(avManager->mtx);

	json temporaryJson = avManager->avStorage;
	for (auto& el : avManager->avStorage.items()) {
		std::string sformID = el.key();
		try {
			if (auto form = RE::TESForm::LookupByID(static_cast<RE::FormID>(std::stoul(sformID)))) {
				if (auto actor = RE::TESForm::LookupByID(static_cast<RE::FormID>(std::stoul(sformID)))->As<RE::Actor>()) {
					if (actor->currentProcess && actor->currentProcess->InHighProcess() && actor->Is3DLoaded())
						continue;
				}
			}
			temporaryJson.erase(sformID);
		} catch (std::invalid_argument const&) {
			logger::error("Bad input: std::invalid_argument thrown");
		} catch (std::out_of_range const&) {
			logger::error("Integer overflow: std::out_of_range thrown");
		}
	}
	avManager->avStorage = temporaryJson;
}

float PoiseAV::GetPassiveMax(RE::Actor* a_actor)
{
	if (!a_actor) {
		return 1.0f;
	}
	auto* avManager = AVManager::GetSingleton();
	std::lock_guard<std::shared_mutex> lk(avManager->mtx);
	return std::max(1.0f, ComputePassiveMax(a_actor));
}

float PoiseAV::GetPassiveCurrent(RE::Actor* a_actor)
{
	if (!a_actor) {
		return 1.0f;
	}
	auto* avManager = AVManager::GetSingleton();
	std::lock_guard<std::shared_mutex> lk(avManager->mtx);
	const float maxP = ComputePassiveMax(a_actor);
	const float curP = GetStoredMeter(a_actor, kMeter_PassiveCur, maxP);
	return std::max(0.0f, curP) + FLT_MIN;
}
