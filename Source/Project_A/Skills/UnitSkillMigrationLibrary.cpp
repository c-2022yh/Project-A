#include "UnitSkillMigrationLibrary.h"
#include "Engine/DataTable.h"

bool UUnitSkillMigrationLibrary::CopyLegacySkillDefaults(UDataTable* Table, FName RowName, TSubclassOf<UUnitSkillBase> SkillClass)
{
#if WITH_EDITOR
    if (!Table || Table->GetRowStruct() != FUnitDefinition::StaticStruct() || !SkillClass || SkillClass->HasAnyClassFlags(CLASS_Abstract)) return false;
    const FUnitDefinition* Row = Table->FindRow<FUnitDefinition>(RowName, TEXT("CopyLegacySkillDefaults"), false);
    if (!Row) return false;
    UUnitSkillBase* Defaults = SkillClass->GetDefaultObject<UUnitSkillBase>();
    if (Row->Skills.IsEmpty()) return Row->SkillClasses.Num() == 1 && Row->SkillClasses[0] == SkillClass;
    if (Row->Skills.Num() != 1 || !Defaults->IsA<UUnitActiveSkill>() || Row->Skills[0].Trigger != EUnitTrigger::ManaFull) return false;
    Defaults->Modify(); Defaults->Settings = Row->Skills[0]; Defaults->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
bool UUnitSkillMigrationLibrary::AttachSkillClass(UDataTable* Table, FName RowName, TSubclassOf<UUnitSkillBase> SkillClass)
{
#if WITH_EDITOR
    if (!Table || Table->GetRowStruct() != FUnitDefinition::StaticStruct() || !SkillClass || SkillClass->HasAnyClassFlags(CLASS_Abstract)) return false;
    FUnitDefinition* Row = Table->FindRow<FUnitDefinition>(RowName, TEXT("AttachSkillClass"), false);
    if (!Row) return false;
    const UUnitSkillBase* Defaults = SkillClass->GetDefaultObject<UUnitSkillBase>();
    if (!Row->Skills.IsEmpty() && (Row->Skills.Num() != 1 || !FUnitSkillDefinition::StaticStruct()->CompareScriptStruct(&Row->Skills[0], &Defaults->Settings, 0))) return false;
    Table->Modify(); Row->SkillClasses = {SkillClass}; Row->Skills.Empty(); Table->MarkPackageDirty();
    Table->HandleDataTableChanged(RowName);
    return true;
#else
    return false;
#endif
}
