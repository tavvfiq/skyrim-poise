#include "Hooks/HitEventHandler.h"

#include "Hooks/PoiseAV.h"
#include "Storage/Settings.h"

namespace
{
	constexpr const char* KW_ERCF_STD = "ERCF.DamageType.Phys.Standard";
	constexpr const char* KW_ERCF_STRIKE = "ERCF.DamageType.Phys.Strike";
	constexpr const char* KW_ERCF_SLASH = "ERCF.DamageType.Phys.Slash";
	constexpr const char* KW_ERCF_PIERCE = "ERCF.DamageType.Phys.Pierce";
}

float HitEventHandler::GetWeaponDamage(RE::TESObjectWEAP* a_weapon)
{
	auto settings = Settings::GetSingleton();
	if (!a_weapon) {
		return 0.0f;
	}

	// Prefer ERCF physical subtype keyword tags if present.
	if (a_weapon->HasKeywordString(KW_ERCF_STD) && settings->JSONSettings["Poise"]["BaseBySubtype"]["Standard"] != nullptr)
		return static_cast<float>(settings->JSONSettings["Poise"]["BaseBySubtype"]["Standard"]);
	if (a_weapon->HasKeywordString(KW_ERCF_STRIKE) && settings->JSONSettings["Poise"]["BaseBySubtype"]["Strike"] != nullptr)
		return static_cast<float>(settings->JSONSettings["Poise"]["BaseBySubtype"]["Strike"]);
	if (a_weapon->HasKeywordString(KW_ERCF_SLASH) && settings->JSONSettings["Poise"]["BaseBySubtype"]["Slash"] != nullptr)
		return static_cast<float>(settings->JSONSettings["Poise"]["BaseBySubtype"]["Slash"]);
	if (a_weapon->HasKeywordString(KW_ERCF_PIERCE) && settings->JSONSettings["Poise"]["BaseBySubtype"]["Pierce"] != nullptr)
		return static_cast<float>(settings->JSONSettings["Poise"]["BaseBySubtype"]["Pierce"]);

	for (int index = a_weapon->numKeywords - 1; index >= 0; index--) {
		if (a_weapon->keywords[index]) {
			std::string keyword = a_weapon->keywords[index]->formEditorID.c_str();
			auto        pos = keyword.find("WeapType");
			if (pos != 0)
				continue;
			std::string type = keyword.substr(pos + 8, keyword.length());
			if (type == "Bow" && a_weapon->weaponData.animationType == RE::WEAPON_TYPE::kCrossbow)
				type = "Crossbow";
			if (!type.empty()) {
				auto weaponDamage = settings->JSONSettings["Weapons"]["Damage"][type];
				if (weaponDamage != nullptr)
					return std::lerp(static_cast<float>(weaponDamage), a_weapon->weight, settings->Damage.WeightContribution);
			}
		}
	}

	// Vanilla fallback based on weapon type buckets.
	// JSON path: Weapons/Damage/<ClassName>
	using WT = RE::TESObjectWEAP::WEAPON_TYPE;
	switch (a_weapon->GetWeaponType()) {
	case WT::kOneHandDagger:
		if (settings->JSONSettings["Weapons"]["Damage"]["Dagger"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Dagger"]);
		break;
	case WT::kOneHandSword:
		if (settings->JSONSettings["Weapons"]["Damage"]["Sword"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Sword"]);
		break;
	case WT::kTwoHandSword:
		if (settings->JSONSettings["Weapons"]["Damage"]["Greatsword"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Greatsword"]);
		break;
	case WT::kOneHandAxe:
		if (settings->JSONSettings["Weapons"]["Damage"]["WarAxe"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["WarAxe"]);
		break;
	case WT::kTwoHandAxe:
		if (settings->JSONSettings["Weapons"]["Damage"]["Battleaxe"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Battleaxe"]);
		break;
	case WT::kOneHandMace:
		if (settings->JSONSettings["Weapons"]["Damage"]["Mace"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Mace"]);
		break;
	case WT::kTwoHandMace:
		if (settings->JSONSettings["Weapons"]["Damage"]["Warhammer"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["Warhammer"]);
		break;
	case WT::kBow:
		{
			const char* key = (a_weapon->weaponData.animationType == RE::WEAPON_TYPE::kCrossbow) ? "Crossbow" : "Bow";
			if (settings->JSONSettings["Weapons"]["Damage"][key] != nullptr)
				return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"][key]);
		}
		break;
	case WT::kHandToHandMelee:
		if (settings->JSONSettings["Weapons"]["Damage"]["HandToHandMelee"] != nullptr)
			return static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["HandToHandMelee"]);
		break;
	default:
		break;
	}

	return a_weapon->weight;
}

float HitEventHandler::GetUnarmedDamage(RE::Actor* a_actor)
{
	auto settings = Settings::GetSingleton();

	auto unarmedDamage = std::lerp(static_cast<float>(settings->JSONSettings["Weapons"]["Damage"]["HandToHandMelee"]), a_actor->GetActorValue(RE::ActorValue::kUnarmedDamage), settings->Damage.UnarmedSkillContribution);
	auto gauntlet = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHands);

	return gauntlet ? std::lerp(unarmedDamage, unarmedDamage + gauntlet->weight, settings->Damage.GauntletWeightContribution) : unarmedDamage;
}

float HitEventHandler::GetShieldDamage(RE::TESObjectARMO* a_shield)
{
	auto settings = Settings::GetSingleton();
	auto shieldDamage = settings->JSONSettings["Weapons"]["Damage"]["Shield"];
	if (shieldDamage != nullptr)
		return std::lerp(static_cast<float>(shieldDamage), a_shield->weight, settings->Damage.WeightContribution);
	return a_shield->weight;
}

float HitEventHandler::GetMiscDamage()
{
	auto settings = Settings::GetSingleton();
	auto miscDamage = settings->JSONSettings["Weapons"]["Damage"]["Misc"];
	if (miscDamage != nullptr)
		return static_cast<float>(miscDamage);
	return 5.0f;
}

float HitEventHandler::RecalculateStagger(RE::Actor* target, RE::Actor* aggressor, RE::HitData* hitData)
{
	auto* settings = Settings::GetSingleton();
	if (!hitData || !aggressor) {
		return 0.0f;
	}

	// ER-style poise damage: Base (weapon class) × Motion value.
	float base = 0.0f;

	auto sourceRef = hitData->sourceRef.get().get();
	if (sourceRef && sourceRef->AsProjectile() && sourceRef->AsProjectile()->weaponSource) {
		base = GetWeaponDamage(sourceRef->AsProjectile()->weaponSource);
	} else if (hitData->weapon) {
		if (hitData->weapon->As<RE::TESObjectWEAP>()->IsHandToHandMelee()) {
			base = GetUnarmedDamage(aggressor);
		} else {
			base = GetWeaponDamage(hitData->weapon);
		}
	} else if (hitData->skill == RE::ActorValue::kBlock) {
		auto leftHand = aggressor->GetEquippedObject(true);
		auto rightHand = aggressor->GetEquippedObject(false);
		if (leftHand && leftHand->formType == RE::FormType::Armor) {
			base = GetShieldDamage(leftHand->As<RE::TESObjectARMO>());
		} else if (rightHand && rightHand->formType == RE::FormType::Weapon) {
			base = GetWeaponDamage(rightHand->As<RE::TESObjectWEAP>());
		} else {
			base = GetMiscDamage();
		}
	} else {
		// Creature attacks: use physicalDamage as a baseline if nothing else exists.
		base = std::max(0.0f, hitData->physicalDamage);
	}

	float mv = 1.0f;  // light

	// Sprint detection: use actor state if available.
	const bool sprinting = aggressor->actorState1.sprinting;

	// Power attack detection (CommonLibSSE-NG): HitData::Flag::kPowerAttack.
	const bool power = hitData->flags.any(RE::HitData::Flag::kPowerAttack);

	if (sprinting) {
		mv = power ? 3.0f : 1.5f;
	} else {
		mv = power ? 2.0f : 1.0f;
	}

	// Block reduces effective poise damage (optional); keep simple for now.
	mv *= (1.0f - hitData->percentBlocked);

	// Optional perk entrypoints remain available; keep them as multipliers on MV.
	float perkMult = 1.0f;
	PoiseAV::ApplyPerkEntryPoint(34, aggressor, target, &perkMult);
	PoiseAV::ApplyPerkEntryPoint(33, target, aggressor, &perkMult);
	mv *= perkMult;

	return std::max(0.0f, base * mv);
}

void HitEventHandler::PreProcessHit(RE::Actor* target, RE::HitData* hitData)
{
	auto poiseAV = PoiseAV::GetSingleton();
	auto aggressor = hitData->aggressor ? hitData->aggressor.get().get() : nullptr;
	if (aggressor && poiseAV->CanDamageActor(target)) {
		auto poiseDamage = RecalculateStagger(target, aggressor, hitData);
		const bool suppress = poiseAV->ApplyPoiseDamage(target, aggressor, poiseDamage);
		if (suppress) {
			hitData->stagger = static_cast<uint32_t>(0.00);
		}
		return;
	}
	// Default: do not force stagger suppression if we didn't process poise.
}
