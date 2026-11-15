#include "ShopTypes.h"

void FItemBonuses::Add(const FItemBonuses& Other)
{
    HP += Other.HP;
    AttackDamage += Other.AttackDamage;
    AbilityPower += Other.AbilityPower;
    Armor += Other.Armor;
    AttackSpeed += Other.AttackSpeed;
}
