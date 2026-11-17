"""UE 5.7: 기존 DT 스킬을 스킬 BP 기본값으로 이전한다. 레벨/유닛 BP는 수정하지 않는다."""
import json
import os
import unreal

TABLE_PATH = '/Game/Data/Units/DT_Units'
FOLDER = '/Game/BluePrints/Units/Skills'
SAVED = os.path.abspath(unreal.Paths.project_saved_dir() + 'SkillMigration')
os.makedirs(SAVED, exist_ok=True)
table = unreal.load_asset(TABLE_PATH)
assert table, 'DT_Units missing'
library = unreal.DataTableFunctionLibrary
names = [str(x) for x in library.get_data_table_row_names(table)]
assert names, 'No unit rows'
columns = ['DisplayName', 'Description', 'Icon', 'Cost', 'RoleTags', 'BaseStats',
           'StarHealthMultipliers', 'StarAttackMultipliers', 'bIsBoss', 'BasicAttack', 'Visuals', 'Animations']
def snapshot():
    return {col: list(library.get_data_table_column_as_string(table, col)) for col in columns}
before = snapshot()

parents = {
    'Knight': ('BP_Skill_KnightShield', unreal.KnightShieldSkill),
    'Mage': ('BP_Skill_MageFireball', unreal.MageFireballSkill),
    'Archer': ('BP_Skill_ArcherRapidFire', unreal.ArcherRapidFireSkill),
    'Rogue': ('BP_Skill_RogueWeakPoint', unreal.RogueWeakPointSkill),
}
unreal.EditorAssetLibrary.make_directory(FOLDER)
tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {'units': {}, 'unchanged_columns': []}
pending = []
for name in names:
    if name not in parents:
        continue
    bp_name, parent = parents[name]
    path = FOLDER + '/' + bp_name
    bp = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if bp is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', parent.static_class())
        bp = tools.create_asset(bp_name, FOLDER, unreal.Blueprint, factory)
    assert bp, 'Could not create ' + path
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    assert isinstance(cdo, parent), 'Existing skill BP has a different native parent: ' + path
    assert unreal.UnitSkillMigrationLibrary.copy_legacy_skill_defaults(table, name, bp.generated_class()), 'Could not copy settings: ' + name
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False), path
    pending.append((name, bp.generated_class()))
    report['units'][name] = {'class': bp.generated_class().get_path_name(), 'settings': cdo.get_editor_property('settings').export_text()}

# BP 준비가 모두 성공한 뒤 스킬 필드만 변경한다. 다른 필드는 바이트 해석/재입력하지 않는다.
for name, cls in pending:
    assert unreal.UnitSkillMigrationLibrary.attach_skill_class(table, name, cls), 'Could not attach skill: ' + name
assert snapshot() == before, 'A non-skill field changed'
report['unchanged_columns'] = columns
assert unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False), 'DT save failed'
with open(os.path.join(SAVED, 'MigrationReport.json'), 'w', encoding='utf-8') as f:
    json.dump(report, f, ensure_ascii=False, indent=2)
unreal.log('UNIT_SKILL_MIGRATION_OK: ' + ', '.join(report['units']))
