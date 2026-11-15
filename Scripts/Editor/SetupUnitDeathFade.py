"""UE Python에서 실행. 기사/마법사 원본 색상은 유지하고 사망 페이드 입력만 추가한다.

열린 에디터의 미저장 머티리얼을 덮어쓰지 않도록 저장/종료 후 commandlet으로 실행한다.
재실행 시 UnitOpacity 연결을 검사하고 이미 적용된 머티리얼은 다시 만들지 않는다.
"""
import json
from pathlib import Path
import unreal

TARGET_MATERIALS = [
    '/Game/Art/Kaykit/Knight/M_Knight',
    '/Game/Art/Kaykit/Mage/M_Mage',
]
FUNCTION = '/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'
lib = unreal.MaterialEditingLibrary


def verify(material):
    assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_MASKED
    root = lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK)
    assert isinstance(root, unreal.MaterialExpressionMaterialFunctionCall)
    assert root.get_editor_property('material_function').get_path_name().startswith(FUNCTION + '.')
    inputs = lib.get_inputs_for_material_expression(material, root)
    assert any(isinstance(node, unreal.MaterialExpressionScalarParameter)
               and str(node.get_editor_property('parameter_name')) == 'UnitOpacity'
               and node.get_editor_property('default_value') == 1.0 for node in inputs)


report = []
function = unreal.load_asset(FUNCTION)
assert function, 'DitherTemporalAA function missing'
for path in TARGET_MATERIALS:
    material = unreal.load_asset(path)
    assert isinstance(material, unreal.Material), path
    if 'UnitOpacity' in [str(name) for name in lib.get_scalar_parameter_names(material)]:
        verify(material)
        report.append({'material': path, 'result': 'already configured'})
        continue
    # 예상한 불투명 머티리얼에서만 실행한다. 기존 마스크/투명도 연결은 덮어쓰지 않는다.
    assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_OPAQUE, path
    assert lib.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY_MASK) is None
    parameter = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 350)
    parameter.set_editor_property('parameter_name', 'UnitOpacity')
    parameter.set_editor_property('default_value', 1.0)
    parameter.set_editor_property('desc', '사망 외형: 1 = 표시, 0 = 숨김. 기물별 MID에서 변경.')
    dither = lib.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, -250, 350)
    dither.set_material_function(function)
    inputs = lib.get_material_expression_input_names(dither)
    alpha_input = next(name for name in inputs if 'alpha' in name.lower())
    assert lib.connect_material_expressions(parameter, '', dither, alpha_input)
    assert lib.connect_material_property(dither, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    lib.recompile_material(material)
    verify(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    report.append({'material': path, 'result': 'configured and saved'})

output = Path(unreal.Paths.project_saved_dir()) / 'UnitDeathFadeMaterialSetup.json'
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Unit death fade materials verified: ' + str(output))
